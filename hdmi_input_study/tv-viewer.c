/* UVC TV viewer: one newest-frame mailbox, planar JPEG decode, direct NV12 VOUT.
 * Uses the established display lease. Does not change firmware or USB drivers. */
#define _GNU_SOURCE
#include <libuvc/libuvc.h>
#include <stdio.h>
#include <jpeglib.h>
#include <libyuv/convert.h>
#include <libyuv/planar_functions.h>
#include <libyuv/scale.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <ucontext.h>
#include <dirent.h>
#ifndef TV_CAPTURE_WIDTH
#define TV_CAPTURE_WIDTH 1280
#endif
#ifndef TV_CAPTURE_HEIGHT
#define TV_CAPTURE_HEIGHT 720
#endif
#define W TV_CAPTURE_WIDTH
#define H TV_CAPTURE_HEIGHT
#define PANEL_W 1280
#define PANEL_H 720
#define MAXJPEG (4*1024*1024)
typedef struct {uint8_t *data;uint32_t physical,bytes,width,height,stride,aligned_height;
 uint32_t magic1,ion_handle,magic2,allocated,magic3,magic4;} VoBuffer;
_Static_assert(sizeof(VoBuffer)==52,"stock VOUT ABI");
static volatile sig_atomic_t ended;
static double clock_s(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void stop(int sig){(void)sig;ended=1;}
static void crash(int sig,siginfo_t *info,void *context){ucontext_t *u=context;Dl_info d={0};dladdr((void*)u->uc_mcontext.arm_pc,&d);dprintf(2,"TV_CRASH sig=%d address=%p pc=%08lx lr=%08lx library=%s base=%p offset=%lx\n",sig,info->si_addr,u->uc_mcontext.arm_pc,u->uc_mcontext.arm_lr,d.dli_fname?d.dli_fname:"?",d.dli_fbase,u->uc_mcontext.arm_pc-(unsigned long)d.dli_fbase);_exit(139);}
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t available=PTHREAD_COND_INITIALIZER;
static uint8_t *pending,*working;static size_t pending_size;
static unsigned received,sequence,consumed,overwritten,bad,shown,display_busy,late_frames;static uint64_t compressed_bytes;static size_t largest_jpeg;
static double arrival,first,last,maxgap,total_decode,total_age,maxage;
static unsigned input_w,input_h;
static unsigned decoded_w,decoded_h;static int half_decode;
static void got_frame(uvc_frame_t *f,void *unused){
 (void)unused;if(f->frame_format!=UVC_FRAME_FORMAT_MJPEG||!f->data_bytes||f->data_bytes>MAXJPEG||f->width>W||f->height>H)return;
 const uint8_t *p=f->data;size_t size=f->data_bytes;
 if(size<4||p[0]!=0xff||p[1]!=0xd8)return;
 /* A truncated JPEG can stall the stock JPU. Never submit an incomplete image. */
 while(size>3&&(p[size-2]!=0xff||p[size-1]!=0xd9))size--;
 if(size<=3)return;
 double t=clock_s();pthread_mutex_lock(&mutex);
 if(!first)first=t;if(last&&t-last>maxgap)maxgap=t-last;last=t;
 if(sequence!=consumed)overwritten++;
 memcpy(pending,f->data,size);pending_size=size;input_w=f->width;input_h=f->height;arrival=t;compressed_bytes+=size;if(size>largest_jpeg)largest_jpeg=size;received++;sequence++;
 pthread_cond_signal(&available);pthread_mutex_unlock(&mutex);
}
struct JpegError {struct jpeg_error_mgr mgr;jmp_buf jump;};
static void jpeg_error(j_common_ptr c){longjmp(((struct JpegError*)c->err)->jump,1);}
static uint8_t *planes[3],*i420,*nv12;static unsigned plane_stride[3];
#include "jpeg-profile.h"
#include "jpeg-hardware.h"
#include "frame-timing.h"
/* Read raw Y/Cb/Cr blocks. This avoids JPEG->RGB->YUV colour conversions. */
static int decode(const uint8_t *data,size_t size,unsigned expected_w,unsigned expected_h){
 struct jpeg_decompress_struct j={0};struct JpegError e;
 j.err=jpeg_std_error(&e.mgr);e.mgr.error_exit=jpeg_error;
 if(setjmp(e.jump)){jpeg_destroy_decompress(&j);return -1;}
 jpeg_create_decompress(&j);jpeg_mem_src(&j,data,size);
 if(jpeg_read_header(&j,TRUE)!=JPEG_HEADER_OK||j.image_width!=expected_w||j.image_height!=expected_h||j.num_components!=3||j.jpeg_color_space!=JCS_YCbCr){jpeg_destroy_decompress(&j);return -1;}
 j.raw_data_out=TRUE;j.out_color_space=JCS_YCbCr;j.dct_method=JDCT_IFAST;j.do_fancy_upsampling=FALSE;
 j.scale_num=1;j.scale_denom=half_decode?2:1;
 jpeg_start_decompress(&j);unsigned w=j.output_width,h=j.output_height;
 if(w>W||h>H||w%2||h%2||j.max_h_samp_factor>2||j.max_v_samp_factor>2){jpeg_destroy_decompress(&j);return -1;}
 for(int c=0;c<3;c++)plane_stride[c]=j.comp_info[c].width_in_blocks*j.comp_info[c].DCT_h_scaled_size;
 while(j.output_scanline<h){JSAMPROW rows[3][16];JSAMPARRAY array[3]={rows[0],rows[1],rows[2]};
  unsigned line=j.output_scanline;
  for(int c=0;c<3;c++){unsigned base=line*j.comp_info[c].v_samp_factor*j.comp_info[c].DCT_v_scaled_size/(j.max_v_samp_factor*j.min_DCT_v_scaled_size);
   for(int r=0;r<j.comp_info[c].v_samp_factor*j.comp_info[c].DCT_v_scaled_size;r++)rows[c][r]=planes[c]+(base+r)*plane_stride[c];}
  if(!jpeg_read_raw_data(&j,array,j.max_v_samp_factor*j.min_DCT_v_scaled_size)){jpeg_destroy_decompress(&j);return -1;}
 }
 unsigned cw=j.comp_info[1].downsampled_width,ch=j.comp_info[1].downsampled_height;
 if(cw!=j.comp_info[2].downsampled_width||ch!=j.comp_info[2].downsampled_height){jpeg_destroy_decompress(&j);return -1;}
 jpeg_finish_decompress(&j);jpeg_destroy_decompress(&j);
 uint8_t *y=i420,*u=y+w*h,*v=u+w*h/4;int rc;
 if(cw==w/2&&ch==h/2)rc=I420Copy(planes[0],plane_stride[0],planes[1],plane_stride[1],planes[2],plane_stride[2],y,w,u,w/2,v,w/2,w,h);
 else if(cw==w/2&&ch==h)rc=I422ToI420(planes[0],plane_stride[0],planes[1],plane_stride[1],planes[2],plane_stride[2],y,w,u,w/2,v,w/2,w,h);
 else if(cw==w&&ch==h)rc=I444ToI420(planes[0],plane_stride[0],planes[1],plane_stride[1],planes[2],plane_stride[2],y,w,u,w/2,v,w/2,w,h);
 else return -1;
 if(rc)return -1;
 /* JPEG is full range; VOUT's established NV12 path uses video range. */
 static uint8_t luma[256],chroma[256];static int tables;
 if(!tables){for(int n=0;n<256;n++){luma[n]=16+(n*219+127)/255;chroma[n]=16+(n*224+127)/255;}tables=1;}
 for(size_t n=0;n<(size_t)w*h;n++)y[n]=luma[y[n]];
 for(size_t n=0;n<(size_t)w*h/4;n++){u[n]=chroma[u[n]];v[n]=chroma[v[n]];}
 decoded_w=w;decoded_h=h;return I420ToNV12(y,w,u,w/2,v,w/2,nv12,w,nv12+w*h,w,w,h);
}
static void *vlib;static VoBuffer buffers[3];static unsigned used[3],next_slot;
static int (*vo_status)(int),(*vo_command)(int,void*,int),(*vo_stop)(void),(*vo_deinit)(void),(*vo_free)(VoBuffer*);
static void (*vo_set_status)(int,int);static uint32_t (*vo_lock)(int),(*vo_received)(int),(*vo_version)(void);
static int active,claimed,power_claimed,hardware_mode;static uint8_t *fb,*saved;static size_t fb_bytes;
static int power_claim(void){
 const char *path="/tmp/doveos-usb-video-active";int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(fd<0&&errno==EEXIST){FILE *f=fopen(path,"r");long owner=0;if(f){if(fscanf(f,"%ld",&owner)!=1)owner=0;fclose(f);}if(owner>1&&kill(owner,0)<0&&errno==ESRCH){unlink(path);fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);}}
 if(fd<0)return -1;char text[40];int len=snprintf(text,sizeof text,"%ld\n",(long)getpid());int rc=write(fd,text,len)==len?0:-1;close(fd);power_claimed=1;return rc;
}
static void power_release(void){if(!power_claimed)return;FILE *f=fopen("/tmp/doveos-usb-video-active","r");long owner=0;if(f){if(fscanf(f,"%ld",&owner)!=1)owner=0;fclose(f);}if(owner==getpid())unlink("/tmp/doveos-usb-video-active");}
static int claim(void){
 int fd=open("/tmp/doveos-direct-display",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(fd<0)return -1;
 char text[40];int len=snprintf(text,sizeof text,"%ld\n",(long)getpid());int rc=write(fd,text,len)==len?0:-1;close(fd);claimed=1;
 if(rc)return -1;
 for(int n=0;n<100&&!ended;n++){FILE *f=fopen("/tmp/doveos-direct-display.ready","r");long owner=0;if(f){if(fscanf(f,"%ld",&owner)!=1)owner=0;fclose(f);}if(owner==getpid())return 0;usleep(50000);}return -1;
}
static void release(void){if(!claimed)return;FILE *f=fopen("/tmp/doveos-direct-display","r");long owner=0;if(f){if(fscanf(f,"%ld",&owner)!=1)owner=0;fclose(f);}if(owner==getpid())unlink("/tmp/doveos-direct-display");claimed=0;}
static int display_open(void){
 vlib=dlopen("/usr/lib/libVOutWrapper.so",RTLD_LAZY|RTLD_LOCAL|RTLD_DEEPBIND);if(!vlib){fprintf(stderr,"VOUT_LIBRARY %s\n",dlerror());return -1;}
 int (*init)(void)=dlsym(vlib,"vo_wrap_init"),(*force)(void)=dlsym(vlib,"vo_wrap_v1_force_on"),(*alloc)(VoBuffer*)=dlsym(vlib,"vo_wrap_alloc_nv12_buffer");
 vo_status=dlsym(vlib,"rvsd_vo_PLockGetStatus");vo_set_status=dlsym(vlib,"rvsd_vo_PLockSetStatus");vo_lock=dlsym(vlib,"rvsd_vo_PLockGetPhyAddr");vo_received=dlsym(vlib,"rvsd_vo_PLockGetReceivedPhyAddr");vo_version=dlsym(vlib,"rvsd_vo_PLockGetVersion");vo_command=dlsym(vlib,"process_InbandCmd");vo_stop=dlsym(vlib,"vo_wrap_v1_stop");vo_deinit=dlsym(vlib,"vo_wrap_deinit");vo_free=dlsym(vlib,"vo_wrap_free_nv12_buffer");
 if(!init||!force||!alloc||!vo_status||!vo_set_status||!vo_lock||!vo_received||!vo_version||!vo_command||!vo_stop||!vo_deinit||!vo_free||init())return -1;
 active=1;if(!hardware_mode||getenv("DOVE_CAPTURE_COPY_DISPLAY"))for(int n=0;n<3;n++){buffers[n].width=W;buffers[n].height=H;buffers[n].stride=W;buffers[n].aligned_height=H;if(alloc(&buffers[n])||!buffers[n].data||buffers[n].bytes<W*H*3/2)return -1;memset(buffers[n].data,16,W*H);memset(buffers[n].data+W*H,128,W*H/2);}
 if((unsigned)force()!=0x10000000u)return -1;
 int fd=open("/dev/fb0",O_RDWR|O_CLOEXEC);struct fb_fix_screeninfo fix;struct fb_var_screeninfo var;
 if(fd<0)return -1;
 if(ioctl(fd,FBIOGET_FSCREENINFO,&fix)||ioctl(fd,FBIOGET_VSCREENINFO,&var)||var.xres!=PANEL_W||var.yres!=PANEL_H){close(fd);return -1;}
 fb_bytes=(size_t)fix.line_length*PANEL_H;fb=mmap(NULL,fb_bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);close(fd);if(fb==MAP_FAILED){fb=NULL;return -1;}
 saved=malloc(fb_bytes);if(!saved)return -1;memcpy(saved,fb,fb_bytes);memset(fb,0,fb_bytes);
 printf("TV_DISPLAY_READY NV12 source=%ux%u desktop=%ux%u buffers=3\n",W,H,PANEL_W,PANEL_H);return 0;
}
static int choose_slot(void){for(int n=0;n<3;n++){unsigned s=next_slot;next_slot=(next_slot+1)%3;if(!used[s]||vo_status(s)==3)return (int)s;}return -1;}
static int present(int slot,unsigned w,unsigned h){
 VoBuffer *b=&buffers[slot];
 if(!w||!h||w>W||h>H)return -1;
 /* VOUT scales the 16:9 frame to the panel. Copy only the decoded image. */
 if((uint64_t)w*H==(uint64_t)h*W){
  memcpy(b->data,nv12,w*h*3/2);__sync_synchronize();uint32_t cmd[28]={0};cmd[0]=vo_version();cmd[1]=4;cmd[2]=b->physical;cmd[3]=b->physical+w*h;cmd[4]=vo_lock(slot);cmd[5]=w;cmd[6]=h;cmd[7]=cmd[8]=w;cmd[21]=cmd[22]=cmd[23]=UINT32_MAX;cmd[26]=vo_received(slot);
  vo_set_status(slot,1);__sync_synchronize();if(!vo_command(39,cmd,sizeof cmd))return -1;used[slot]=1;return 0;
 }
 unsigned dw=W,dh=H;if((uint64_t)w*H>(uint64_t)h*W)dh=((uint64_t)W*h/w)&~1u;else if((uint64_t)w*H<(uint64_t)h*W)dw=((uint64_t)H*w/h)&~1u;
 if(dw<2||dh<2||dw>W||dh>H)return -1;
 unsigned x=((W-dw)/2)&~1u,y=((H-dh)/2)&~1u;
 if(dw!=W||dh!=H){memset(b->data,16,W*H);memset(b->data+W*H,128,W*H/2);}
 if(NV12Scale(nv12,w,nv12+w*h,w,w,h,b->data+y*W+x,W,b->data+W*H+(y/2)*W+x,W,dw,dh,kFilterNone))return -1;
 __sync_synchronize();uint32_t cmd[28]={0};cmd[0]=vo_version();cmd[1]=4;cmd[2]=b->physical;cmd[3]=b->physical+W*H;cmd[4]=vo_lock(slot);cmd[5]=W;cmd[6]=H;cmd[7]=cmd[8]=W;cmd[21]=cmd[22]=cmd[23]=UINT32_MAX;cmd[26]=vo_received(slot);
 vo_set_status(slot,1);__sync_synchronize();int rc=vo_command(39,cmd,sizeof cmd);if(!rc)return -1;used[slot]=1;return 0;
}
static OMX_BUFFERHEADERTYPE *hardware_held[64];static unsigned hardware_fence;
static unsigned held_count(void){unsigned count=0;for(unsigned n=0;n<64;n++)if(hardware_held[n])count++;return count;}
static int hardware_recycle(void){for(unsigned n=0;n<64;n++)if(hardware_held[n]&&vo_status(n)==3){OMX_BUFFERHEADERTYPE *b=hardware_held[n];hardware_held[n]=NULL;if(jpeg_hw_refill(b))return -1;}return 0;}
static double max_present_ms,total_present_ms;static unsigned present_calls;
static int hardware_present(void){
 /* Keep the decoder's buffers independent of the display's three lock slots.
    Returning every decoded buffer immediately prevents decoder starvation when
    the display retains a frame. Only the newest frame is copied for display. */
 if(getenv("DOVE_CAPTURE_COPY_DISPLAY")){
  OMX_BUFFERHEADERTYPE *b=jcurrent;OMX_VIDEO_PORTDEFINITIONTYPE *v=&jports[1].format.video;
  int slot=choose_slot();if(slot<0){display_busy++;return jpeg_hw_refill(b);}
  if(v->nStride!=W||jpeg_hw_storage_height(b)!=H)return -1;
  uint8_t *previous=nv12;nv12=b->pBuffer+b->nOffset;int rc=present(slot,W,H);nv12=previous;
  if(jpeg_hw_refill(b))return -1;return rc;
 }
 OMX_BUFFERHEADERTYPE *b=jcurrent;int slot=-1;
 /* Rotate display fences independently of decoder buffer indices. Rapid reuse
    of a small fence ring loses acknowledgements on this vendor display path;
    the verified Moonlight HEVC output uses the same 64-fence rotation. */
 for(unsigned n=0;n<64;n++){unsigned s=hardware_fence;hardware_fence=(hardware_fence+1)%64;if(!hardware_held[s]){slot=s;break;}}
 if(slot<0){display_busy++;return jpeg_hw_refill(b);}if(!b->pPlatformPrivate)return -1;
 OMX_VIDEO_PORTDEFINITIONTYPE *v=&jports[1].format.video;uint32_t physical=(uint32_t)(uintptr_t)b->pPlatformPrivate+b->nOffset;
 unsigned storage_height=jpeg_hw_storage_height(b);if(!storage_height)return -1;
 uint32_t cmd[28]={0};cmd[0]=vo_version();cmd[1]=4;cmd[2]=physical;cmd[3]=physical+v->nStride*storage_height;cmd[4]=vo_lock(slot);cmd[5]=W;cmd[6]=H;cmd[7]=cmd[8]=v->nStride;cmd[21]=cmd[22]=cmd[23]=UINT32_MAX;cmd[26]=vo_received(slot);
 if(!present_calls)printf("TV_FRAME_LAYOUT visible=%ux%u stride=%d storage_height=%u chroma_offset=%u\n",W,H,v->nStride,storage_height,v->nStride*storage_height);
 vo_set_status(slot,1);__sync_synchronize();double before=clock_s();int rc=vo_command(39,cmd,sizeof cmd);double elapsed=(clock_s()-before)*1000;total_present_ms+=elapsed;present_calls++;if(elapsed>max_present_ms)max_present_ms=elapsed;if(getenv("DOVE_CAPTURE_PROFILE")&&elapsed>10)printf("VOUT_PROFILE call_ms=%.2f\n",elapsed);if(!rc)return -1;hardware_held[slot]=b;return hardware_recycle();
}
static void display_close(void){
 if(active){int rc=vo_stop();printf("TV_DISPLAY_STOP rc=%08x\n",rc);if((unsigned)rc==0x10000000u)for(int n=0;n<3;n++)if(buffers[n].data)vo_free(&buffers[n]);vo_deinit();active=0;}
 if(fb){if(saved)memcpy(fb,saved,fb_bytes);munmap(fb,fb_bytes);}free(saved);if(vlib)dlclose(vlib);release();
}
static int keyboard[24],keyboard_count;static int ctrl,alt,shift;
static void input_open(void){for(int n=0;n<24;n++){char path[50];snprintf(path,sizeof path,"/dev/input/event%d",n);int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(fd>=0)keyboard[keyboard_count++]=fd;}}
static void input_check(void){for(int n=0;n<keyboard_count;n++){struct input_event e;while(read(keyboard[n],&e,sizeof e)==sizeof e){if(e.type!=EV_KEY)continue;int down=e.value!=0;
 if(e.code==KEY_LEFTCTRL||e.code==KEY_RIGHTCTRL)ctrl=down;if(e.code==KEY_LEFTALT||e.code==KEY_RIGHTALT)alt=down;if(e.code==KEY_LEFTSHIFT||e.code==KEY_RIGHTSHIFT)shift=down;
 if(down&&(e.code==KEY_F10||e.code==KEY_ESC||(e.code==KEY_Q&&ctrl&&alt&&shift)))ended=1;}}}
static pid_t audio_pid;
static void worker_priority(void){DIR *d=opendir("/proc/self/task");if(!d)return;struct dirent *e;struct sched_param p={.sched_priority=1};while((e=readdir(d))){int tid=atoi(e->d_name);if(tid>0&&tid!=getpid())printf("TV_WORKER_PRIORITY tid=%d rc=%d\n",tid,sched_setscheduler(tid,SCHED_FIFO,&p));}closedir(d);}
static void audio_start(uvc_device_t *dev){if(getenv("DOVE_CAPTURE_NO_AUDIO"))return;uvc_device_descriptor_t *d=NULL;if(uvc_get_device_descriptor(dev,&d)<0)return;
 char bus[16],address[16],vid[16],pid[16];snprintf(bus,sizeof bus,"%u",uvc_get_bus_number(dev));snprintf(address,sizeof address,"%u",uvc_get_device_address(dev));snprintf(vid,sizeof vid,"%u",d->idVendor);snprintf(pid,sizeof pid,"%u",d->idProduct);uvc_free_device_descriptor(d);
 audio_pid=fork();if(audio_pid==0){execl("/tmp/doveos-v2-root/lib/ld-linux-armhf.so.3","ld-linux-armhf.so.3","--library-path","/tmp/doveos-uvc-audio-runtime/lib:/tmp/doveos-v2-root/lib/arm-linux-gnueabihf","/tmp/doveos-capture-audio",bus,address,vid,pid,"0",(char*)NULL);_exit(127);}
}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IOLBF,0);signal(SIGINT,stop);signal(SIGTERM,stop);signal(SIGPIPE,SIG_IGN);
 int benchmark=argc>1&&!strcmp(argv[1],"--benchmark");int hardware=getenv("DOVE_CAPTURE_HARDWARE")!=NULL;int bus=argc>2?atoi(argv[2]):0,address=argc>3?atoi(argv[3]):0,fps=argc>4?atoi(argv[4]):60,duration=argc>5?atoi(argv[5]):1800;
 int diagnostic_late=getenv("DOVE_CAPTURE_DIAGNOSTIC_SHOW_LATE")!=NULL;
 half_decode=getenv("DOVE_CAPTURE_HALF")!=NULL;
 if(benchmark){struct sigaction diagnostic={0};diagnostic.sa_sigaction=crash;diagnostic.sa_flags=SA_SIGINFO;sigaction(SIGSEGV,&diagnostic,NULL);}
 if((!benchmark&&(argc<2||strcmp(argv[1],"--watch")))||fps<1||fps>60||duration<0||duration>1800)return 2;
 hardware_mode=hardware;pending=malloc(MAXJPEG);working=malloc(MAXJPEG);
 if(!hardware){i420=malloc(W*H*3/2);nv12=malloc(W*H*3/2);for(int n=0;n<3;n++)planes[n]=malloc((W+32)*(H+32));}
 if(!pending||!working||(!hardware&&(!i420||!nv12||!planes[0]||!planes[1]||!planes[2])))return 3;
 uvc_context_t *context=NULL;uvc_device_t **devices=NULL,*device=NULL;uvc_device_handle_t *handle=NULL;int result=4,streaming=0;
 if(uvc_init(&context,NULL)<0||uvc_get_device_list(context,&devices)<0)goto done;
 for(int n=0;devices[n];n++)if((!bus||uvc_get_bus_number(devices[n])==bus)&&(!address||uvc_get_device_address(devices[n])==address)){device=devices[n];break;}
 if(!device||uvc_open(device,&handle)<0)goto done;
 uvc_stream_ctrl_t control;int rc=uvc_get_stream_ctrl_format_size(handle,&control,UVC_FRAME_FORMAT_MJPEG,W,H,fps);
 if(rc<0||!control.dwMaxVideoFrameSize||control.dwMaxVideoFrameSize>MAXJPEG||control.dwMaxPayloadTransferSize>128*1024){fprintf(stderr,"TV_MODE_UNAVAILABLE rc=%d\n",rc);goto done;}
 if(!benchmark){if(power_claim()||claim()||display_open()){puts("TV_DISPLAY_UNAVAILABLE");goto done;}input_open();}
 if(hardware&&jpeg_hw_open()){puts("JPEG_HW_INIT_FAILED");goto done;}
 rc=uvc_start_streaming(handle,&control,got_frame,NULL,0);if(rc<0){fprintf(stderr,"TV_STREAM_ERROR rc=%d\n",rc);goto done;}streaming=1;
 if(!benchmark||getenv("DOVE_CAPTURE_BENCH_AUDIO"))audio_start(device);
 printf("TV_STARTED %ux%u@%d latest_frame=1 planar_decode=1 benchmark=%d\n",W,H,fps,benchmark);
 if(diagnostic_late)puts("TV_DIAGNOSTIC Showing late frames for observation; this is not a low-latency acceptance test. Decoder and source stall watchdogs remain active.");
 if(getenv("DOVE_CAPTURE_WORKER_PRIORITY"))worker_priority();
 if(getenv("DOVE_CAPTURE_PRIORITY")&&!getenv("DOVE_CAPTURE_NORMAL_PRIORITY")){struct sched_param priority={.sched_priority=2};printf("TV_PRIORITY rc=%d\n",sched_setscheduler(0,SCHED_FIFO,&priority));}
 double started=clock_s(),last_report=started,last_output=started,last_visible=started;unsigned interval_shown=0,input_demands_served=0;
 while(!ended&&(!duration||clock_s()-started<duration)){
  if(!benchmark)input_check();
  if(hardware&&!benchmark&&hardware_recycle()){result=8;break;}
  if(hardware&&getenv("DOVE_CAPTURE_PIPELINE")){
   pthread_mutex_lock(&mutex);
   /* Feed the engine immediately when its bitstream ring requests more data.
      Waiting for a timer here turns a small ring boundary into lasting lag. */
   unsigned outstanding=jsubmitted-jacknowledged;
   unsigned input_demands=__atomic_load_n(&jpeg_need_input,__ATOMIC_ACQUIRE);
   int needs_input=input_demands!=input_demands_served;
   /* Some ring-boundary inputs are consumed without an output picture.
      Keep the timestamp window bounded, but let a fresh frame repair a
      missing-output dependency after 25 ms instead of freezing forever. */
   int progress_probe=jconfigured&&clock_s()-last_output>.025;
   int ready=sequence!=consumed&&__atomic_load_n(&jreturned,__ATOMIC_ACQUIRE)>=jsubmitted&&(outstanding<2||((needs_input||progress_probe)&&outstanding<6));
   size_t size=0;unsigned w=0,h=0;double at=0;
   if(ready){uint8_t *swap=working;working=pending;pending=swap;size=pending_size;w=input_w;h=input_h;at=arrival;consumed=sequence;}
   unsigned rec=received,repl=overwritten;double gap=maxgap,last_input=last;
   pthread_mutex_unlock(&mutex);
   if(clock_s()-started>6&&(!last_input||clock_s()-last_input>5)){puts("TV_NO_SIGNAL_SOURCE_DISCONNECTED");result=6;break;}
   if(clock_s()-last_output>5&&jsubmitted){puts("TV_DECODER_STALLED");result=8;break;}
   if(shown&&clock_s()-last_visible>1){printf("TV_FRESH_PICTURE_STALLED submitted=%u returned=%u acknowledged=%u decoded=%u need_input=%u empty_events=%u held=%u ready=%u sequence=%u consumed=%u latest_age_ms=%.2f\n",jsubmitted,jreturned,jacknowledged,jdecoded,jpeg_need_input,jpeg_empty_events,held_count(),jwrite-jread,sequence,consumed,jlatest_age*1000);result=8;break;}
   if(ready){if(jpeg_hw_submit(working,size,w,h,at)<0){result=8;break;}input_demands_served=input_demands;}
   int got=jpeg_hw_poll();if(got<0){result=8;break;}
   if(got){last_output=clock_s();double age=last_output-(double)jcurrent->nTimeStamp/1000000;
    /* Production drops old frames. The explicit observation diagnostic
       displays them and reports their measured age without claiming a fix. */
    if(age>.050)late_frames++;
    if(age>.050&&!diagnostic_late){if(jpeg_hw_refill(jcurrent)){result=7;break;}display_busy++;}
    else{latency_sample();if(benchmark?jpeg_hw_refill(jcurrent):hardware_present()){result=7;break;}last_visible=clock_s();total_age+=age;if(age>maxage)maxage=age;shown++;result=0;}}
   if(clock_s()-last_report>=5){printf("TV_PIPELINE shown=%u submitted=%u returned=%u acknowledged=%u received=%u replaced=%u fps=%.2f callback_to_submit_ms=%.2f max_age_ms=%.2f input_gap_ms=%.2f held=%u ready=%u latest_decode_ms=%.2f\n",shown,jsubmitted,jreturned,jacknowledged,rec,repl,(shown-interval_shown)/(clock_s()-last_report),shown?total_age*1000/shown:0,maxage*1000,gap*1000,held_count(),jwrite-jread,jlatest_age*1000);last_report=clock_s();interval_shown=shown;}
   usleep(1000);continue;
  }
  int slot=benchmark||hardware?0:choose_slot();if(slot<0){display_busy++;usleep(1000);continue;}
  pthread_mutex_lock(&mutex);
  if(sequence==consumed){struct timespec wait;clock_gettime(CLOCK_REALTIME,&wait);wait.tv_nsec+=5000000;if(wait.tv_nsec>=1000000000){wait.tv_nsec-=1000000000;wait.tv_sec++;}pthread_cond_timedwait(&available,&mutex,&wait);}
  if(sequence==consumed){pthread_mutex_unlock(&mutex);if(clock_s()-started>6&&(!last||clock_s()-last>5)){puts("TV_NO_SIGNAL_SOURCE_DISCONNECTED");result=6;break;}continue;}
  uint8_t *swap=working;working=pending;pending=swap;size_t size=pending_size;unsigned w=input_w,h=input_h;double at=arrival;consumed=sequence;pthread_mutex_unlock(&mutex);
  double t=clock_s();if(hardware?jpeg_hw_decode(working,size,w,h):decode(working,size,w,h)){bad++;if(hardware){puts("JPEG_HW_DECODE_FAILED");result=8;break;}continue;}total_decode+=clock_s()-t;
  if(!benchmark&&(hardware?hardware_present():present(slot,decoded_w,decoded_h))){puts("TV_RENDER_FAILED");result=7;break;}
  if(benchmark&&hardware&&jpeg_hw_refill(jcurrent)){result=8;break;}
  double age=clock_s()-at;total_age+=age;if(age>maxage)maxage=age;shown++;
  if(clock_s()-last_report>=5){printf("TV_STATS shown=%u received=%u replaced=%u bad=%u fps=%.2f mean_decode_ms=%.2f mean_callback_to_submit_ms=%.2f max_callback_to_submit_ms=%.2f max_input_gap_ms=%.2f\n",shown,received,overwritten,bad,(shown-interval_shown)/(clock_s()-last_report),total_decode*1000/shown,total_age*1000/shown,maxage*1000,maxgap*1000);last_report=clock_s();interval_shown=shown;}
  result=0;
 }
done:
 printf("TV_LATE_FRAMES count=%u diagnostic_show_late=%d\n",late_frames,diagnostic_late);
 latency_report();
 if(getenv("DOVE_CAPTURE_PROFILE"))jpeg_profile_report();
 printf("JPEG_INPUT_SIZE mean_bytes=%llu max_bytes=%zu\n",(unsigned long long)(received?compressed_bytes/received:0),largest_jpeg);
 printf("VOUT_TIMING calls=%u mean_ms=%.3f max_ms=%.3f\n",present_calls,present_calls?total_present_ms/present_calls:0,max_present_ms);
 if(audio_pid>0){kill(audio_pid,SIGTERM);for(int n=0;n<80;n++){if(waitpid(audio_pid,NULL,WNOHANG)==audio_pid){audio_pid=0;break;}usleep(25000);}if(audio_pid>0){kill(audio_pid,SIGKILL);waitpid(audio_pid,NULL,0);}}
 if(handle){if(streaming)uvc_stop_streaming(handle);uvc_close(handle);}if(devices)uvc_free_device_list(devices,1);if(context)uvc_exit(context);
 if(!benchmark){display_close();power_release();}
 if(hardware)jpeg_hw_close();for(int n=0;n<keyboard_count;n++)close(keyboard[n]);
 printf("TV_DONE result=%d shown=%u received=%u replaced=%u decode_errors=%u active_fps=%.2f decode_ms=%.2f callback_to_submit_ms=%.2f max_age_ms=%.2f busy_polls=%u\n",result,shown,received,overwritten,bad,received>1?(received-1)/(last-first):0,shown?total_decode*1000/shown:0,shown?total_age*1000/shown:0,maxage*1000,display_busy);
 free(pending);free(working);free(i420);free(nv12);for(int n=0;n<3;n++)free(planes[n]);return result;
}
