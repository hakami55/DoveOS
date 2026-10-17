#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <jpeglib.h>
#include <libuvc/libuvc.h>
#include <libyuv/convert_argb.h>
#include <libyuv/convert_from_argb.h>
#include <libyuv/planar_functions.h>
#include <libyuv/scale_argb.h>
#include <libyuv/scale.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "font.h"

#define W 1280
#define H 720
#define COMPRESSED_MAX (2*1024*1024)
#define NO_SIGNAL_HASH 0x48a6a670a7732bc3ull
typedef struct {
    uint8_t *data;uint32_t physical,bytes,width,height,stride,aligned_height;
    uint32_t magic1,ion_handle,magic2,allocated,magic3,magic4;
} vo_buffer;
_Static_assert(sizeof(vo_buffer)==52,"Vendor VOUT ABI");
static void *vo_lib;
static vo_buffer buffers[4];
static vo_buffer *video=&buffers[0];
static unsigned next_slot=1;
static unsigned char slot_used[4];
static int (*vo_render)(vo_buffer*),(*vo_stop)(void),(*vo_deinit)(void),(*vo_free)(vo_buffer*);
static int (*vo_status)(int),(*vo_command)(int,void*,int);
static void (*vo_set_status)(int,int);
static uint32_t (*vo_lock_address)(int),(*vo_received_address)(int),(*vo_version)(void);
static int vo_active;
static uint32_t *fb,*fb_saved,*fb_back;
static size_t fb_size;
static int fb_stride;
static volatile sig_atomic_t quit;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned char *latest,*copybuf,*rgb,*argb_src,*nv12_small;
static size_t latest_size;
static unsigned latest_seq;
static time_t latest_time;
static int capture_yuyv;
static void on_signal(int n){(void)n;quit=1;}
static uint64_t tick_ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
static uint64_t decode_ns,convert_ns,copy_ns,render_ns;
static void clear_screen(void){
    memset(video->data,24,(size_t)W*H);
    memset(video->data+(size_t)W*H,128,(size_t)W*H/2);
    if(fb_back)for(int y=0;y<H;y++)for(int x=0;x<W;x++)fb_back[(size_t)y*fb_stride+x]=0x000d1820u;
}
static void text(const char *value,int x,int y,int scale,uint8_t color){
    for(int ch=0;value[ch];ch++){
        unsigned char c=(unsigned char)value[ch];if(c>=128)c='?';
        for(int row=0;row<16;row++)for(int col=0;col<8;col++)
            if(font8x16[c][row]&(0x80u>>col))for(int yy=0;yy<scale;yy++)for(int xx=0;xx<scale;xx++){
                int px=x+ch*8*scale+col*scale+xx,py=y+row*scale+yy;
                if(px>=0&&px<W&&py>=0&&py<H){
                    video->data[(size_t)py*W+px]=color;
                    if(fb_back)fb_back[(size_t)py*fb_stride+px]=0x00e8f3f2u;
                }
            }
    }
}
static void status_screen(const char *title,const char *detail){
    video=&buffers[0];
    clear_screen();
    text("HDMI IN",42,32,3,220);
    text(title,90,272,3,235);
    text(detail,90,346,2,175);
    text("F10 - return to Files / Notes",42,670,1,180);
    if(fb_back)memcpy(fb,fb_back,fb_size);
    __sync_synchronize();
    int rc=vo_render(video);if(rc)fprintf(stderr,"HDMI_RENDER_ERROR=%d\n",rc);
}
static int framebuffer_open(void){
    int fd=open("/dev/fb0",O_RDWR|O_CLOEXEC);
    if(fd<0)return -1;
    struct fb_var_screeninfo var={0};struct fb_fix_screeninfo fix={0};
    if(ioctl(fd,FBIOGET_VSCREENINFO,&var)||ioctl(fd,FBIOGET_FSCREENINFO,&fix)||
       var.xres!=W||var.yres!=H||var.bits_per_pixel!=32||fix.line_length<W*4){close(fd);return -1;}
    fb_stride=(int)fix.line_length/4;fb_size=(size_t)fix.line_length*H;
    fb=mmap(NULL,fb_size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);close(fd);
    if(fb==MAP_FAILED){fb=NULL;return -1;}
    fb_saved=malloc(fb_size);fb_back=malloc(fb_size);
    if(!fb_saved||!fb_back){free(fb_saved);free(fb_back);munmap(fb,fb_size);fb=NULL;fb_saved=NULL;fb_back=NULL;return -1;}
    memcpy(fb_saved,fb,fb_size);
    fprintf(stderr,"HDMI_FRAMEBUFFER_READY stride=%d\n",fb_stride);return 0;
}
static void framebuffer_close(void){
    if(fb){memcpy(fb,fb_saved,fb_size);munmap(fb,fb_size);fb=NULL;}
    free(fb_saved);fb_saved=NULL;
    free(fb_back);fb_back=NULL;
}
static int display_open(void){
    vo_lib=dlopen("/usr/lib/libVOutWrapper.so",RTLD_LAZY|RTLD_LOCAL|RTLD_DEEPBIND);
    if(!vo_lib){fprintf(stderr,"HDMI_VOUT_LIB_MISSING\n");return -1;}
    int (*init)(void)=dlsym(vo_lib,"vo_wrap_init");
    int (*force)(void)=dlsym(vo_lib,"vo_wrap_v1_force_on");
    int (*alloc)(vo_buffer*)=dlsym(vo_lib,"vo_wrap_alloc_nv12_buffer");
    vo_render=dlsym(vo_lib,"vo_wrap_render");
    vo_stop=dlsym(vo_lib,"vo_wrap_v1_stop");
    vo_deinit=dlsym(vo_lib,"vo_wrap_deinit");
    vo_free=dlsym(vo_lib,"vo_wrap_free_nv12_buffer");
    vo_status=dlsym(vo_lib,"rvsd_vo_PLockGetStatus");
    vo_set_status=dlsym(vo_lib,"rvsd_vo_PLockSetStatus");
    vo_lock_address=dlsym(vo_lib,"rvsd_vo_PLockGetPhyAddr");
    vo_received_address=dlsym(vo_lib,"rvsd_vo_PLockGetReceivedPhyAddr");
    vo_version=dlsym(vo_lib,"rvsd_vo_PLockGetVersion");
    vo_command=dlsym(vo_lib,"process_InbandCmd");
    if(!init||!force||!alloc||!vo_render||!vo_stop||!vo_deinit||!vo_free||
       !vo_status||!vo_set_status||!vo_lock_address||!vo_received_address||!vo_version||!vo_command||init())return -1;
    vo_active=1;
    for(int i=0;i<4;i++){
        buffers[i].width=W;buffers[i].height=H;buffers[i].stride=W;buffers[i].aligned_height=H;
        if(alloc(&buffers[i])||!buffers[i].data||buffers[i].bytes<(W*H*3/2))return -1;
    }
    if((unsigned)force()!=0x10000000u)return -1;
    fprintf(stderr,"HDMI_VOUT_DIRECT_READY buffers=4 bytes=%u\n",video->bytes);return 0;
}
static void display_close(void){
    if(vo_active){
        int rc=vo_stop();
        if((unsigned)rc==0x10000000u)for(int i=0;i<4;i++)if(buffers[i].data)vo_free(&buffers[i]);
        vo_deinit();vo_active=0;
    }
    if(vo_lib){dlclose(vo_lib);vo_lib=NULL;}
}
struct jpeg_error {struct jpeg_error_mgr base;jmp_buf jump;};
static void jpeg_fail(j_common_ptr c){longjmp(((struct jpeg_error*)c->err)->jump,1);}
static int decode(size_t size,unsigned *width,unsigned *height,uint64_t *hash){
    struct jpeg_decompress_struct j={0};struct jpeg_error error;
    j.err=jpeg_std_error(&error.base);error.base.error_exit=jpeg_fail;
    if(setjmp(error.jump)){jpeg_destroy_decompress(&j);return -1;}
    jpeg_create_decompress(&j);jpeg_mem_src(&j,copybuf,size);
    if(jpeg_read_header(&j,TRUE)!=JPEG_HEADER_OK){jpeg_destroy_decompress(&j);return -1;}
    j.out_color_space=JCS_RGB;jpeg_start_decompress(&j);
    *width=j.output_width;*height=j.output_height;
    if(!*width||!*height||*width>1920||*height>1200){jpeg_destroy_decompress(&j);return -1;}
    size_t stride=(size_t)*width*3;
    uint64_t h=1469598103934665603ull;
    while(j.output_scanline<*height){
        unsigned y=j.output_scanline;
        JSAMPROW row[1]={rgb+(size_t)y*stride};
        jpeg_read_scanlines(&j,row,1);
        if((y+1)%16==0)for(unsigned x=0;x<*width;x+=16)
            for(int c=0;c<3;c++)h=(h^row[0][x*3+c])*1099511628211ull;
    }
    jpeg_finish_decompress(&j);jpeg_destroy_decompress(&j);
    *hash=h;return 0;
}
static void draw_frame(unsigned width,unsigned height,int argb_ready){
    int slot=0;
    for(int attempts=0;attempts<3;attempts++){
        int candidate=(int)next_slot;
        next_slot=(next_slot%3)+1;
        if(!slot_used[candidate]||vo_status(candidate)==3){slot=candidate;break;}
    }
    if(!slot)return;
    video=&buffers[slot];
    uint64_t t0=tick_ns();
    if(!fb_back||(!argb_ready&&RAWToARGB(rgb,(int)width*3,argb_src,(int)width*4,(int)width,(int)height))||
       ARGBScale(argb_src,(int)width*4,(int)width,(int)height,
                 (uint8_t*)fb_back,fb_stride*4,W,H,kFilterNone)){
        fprintf(stderr,"HDMI_LIBYUV_CONVERT_FAILED\n");return;
    }
    int convert_error;
    if(capture_yuyv){
        convert_error=YUY2ToNV12(copybuf,(int)width*2,nv12_small,(int)width,
                                 nv12_small+(size_t)width*height,(int)width,(int)width,(int)height)||
            NV12Scale(nv12_small,(int)width,nv12_small+(size_t)width*height,(int)width,
                      (int)width,(int)height,video->data,W,video->data+(size_t)W*H,W,W,H,kFilterNone);
    }else{
        convert_error=ARGBToNV12((uint8_t*)fb_back,fb_stride*4,video->data,W,
                                 video->data+(size_t)W*H,W,W,H);
    }
    if(convert_error){fprintf(stderr,"HDMI_NV12_CONVERT_FAILED\n");return;}
    uint64_t t1=tick_ns();
    if(fb_back)memcpy(fb,fb_back,fb_size);
    uint64_t t2=tick_ns();
    __sync_synchronize();
    uint32_t command[28]={0};
    command[0]=vo_version();command[1]=4;command[2]=video->physical;
    command[3]=video->physical+video->stride*video->aligned_height;
    command[4]=vo_lock_address(slot);command[5]=video->width;command[6]=video->height;
    command[7]=command[8]=video->stride;
    command[21]=command[22]=command[23]=UINT32_MAX;
    command[26]=vo_received_address(slot);
    vo_set_status(slot,1);__sync_synchronize();
    int rc=vo_command(39,command,sizeof command);
    if(!rc)fprintf(stderr,"HDMI_DIRECT_RENDER_ERROR=%d\n",rc);
    else slot_used[slot]=1;
    uint64_t t3=tick_ns();
    convert_ns+=t1-t0;copy_ns+=t2-t1;render_ns+=t3-t2;
    static unsigned drawn;static uint64_t last_report_ns;
    if(++drawn%60==0){
        struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
        uint64_t now_ns=(uint64_t)ts.tv_sec*1000000000ull+ts.tv_nsec;
        unsigned fps100=last_report_ns?(unsigned)(6000000000000ull/(now_ns-last_report_ns)):0;
        fprintf(stderr,"HDMI_RENDERED_FRAMES=%u source=%ux%u fps_x100=%u\n",drawn,width,height,fps100);
        fprintf(stderr,"HDMI_FRAME_MS_X100 decode=%llu convert=%llu copy=%llu render=%llu\n",
                (unsigned long long)(decode_ns/600000),
                (unsigned long long)(convert_ns/600000),
                (unsigned long long)(copy_ns/600000),
                (unsigned long long)(render_ns/600000));
        decode_ns=convert_ns=copy_ns=render_ns=0;
        last_report_ns=now_ns;
    }
}
static void frame_callback(uvc_frame_t *frame,void *unused){
    (void)unused;
    if(frame->frame_format!=(capture_yuyv?UVC_FRAME_FORMAT_YUYV:UVC_FRAME_FORMAT_MJPEG)||
       frame->data_bytes>COMPRESSED_MAX)return;
    pthread_mutex_lock(&lock);
    memcpy(latest,frame->data,frame->data_bytes);
    latest_size=frame->data_bytes;latest_seq++;latest_time=time(NULL);
    pthread_mutex_unlock(&lock);
}
static void capture_close(uvc_context_t *ctx,uvc_device_t *dev,uvc_device_handle_t *handle,int streaming){
    if(handle){if(streaming)uvc_stop_streaming(handle);uvc_close(handle);}
    if(dev)uvc_unref_device(dev);
    if(ctx)uvc_exit(ctx);
}
int main(int argc,char **argv){
    if(argc>1&&!strcmp(argv[1],"--check")){
        if(sizeof(vo_buffer)!=52||W!=1280||H!=720)return 2;
        puts("HDMI_VIEWER_CHECK_OK");return 0;
    }
    latest=malloc(COMPRESSED_MAX);copybuf=malloc(COMPRESSED_MAX);
    rgb=malloc((size_t)1920*1200*3);argb_src=malloc((size_t)1920*1200*4);
    nv12_small=malloc((size_t)640*480*3/2);
    if(!latest||!copybuf||!rgb||!argb_src||!nv12_small)return 2;
    signal(SIGINT,on_signal);signal(SIGTERM,on_signal);
    if(framebuffer_open())fprintf(stderr,"HDMI_FRAMEBUFFER_UNAVAILABLE\n");
    if(display_open()){display_close();framebuffer_close();return 3;}
    int keyboard=open("/dev/input/event1",O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    if(keyboard<0){display_close();framebuffer_close();return 4;}
    status_screen("Connect USB HDMI capture","Waiting for the capture card on the dock hub...");
    time_t end=time(NULL)+3600,retry=0,shown=0;
    uvc_context_t *ctx=NULL;uvc_device_t *dev=NULL;uvc_device_handle_t *handle=NULL;int streaming=0;
    unsigned displayed=0;const char *state="Connect USB HDMI capture";
    while(!quit&&time(NULL)<end){
        struct input_event event;
        while(read(keyboard,&event,sizeof event)==sizeof event)
            if(event.type==EV_KEY&&event.code==KEY_F10&&event.value==1)quit=1;
        if(quit)break;
        time_t now=time(NULL);
        if(!streaming&&now>=retry){
            capture_close(ctx,dev,handle,0);ctx=NULL;dev=NULL;handle=NULL;
            uvc_stream_ctrl_t ctrl;
            if(uvc_init(&ctx,NULL)<0||uvc_find_device(ctx,&dev,0x534d,0x2109,NULL)<0){
                state="Connect USB HDMI capture";retry=now+3;
            } else {
                int started=0;
                if(uvc_open(dev,&handle)>=0){
                    capture_yuyv=1;
                    if(uvc_get_stream_ctrl_format_size(handle,&ctrl,UVC_FRAME_FORMAT_YUYV,640,480,30)>=0&&
                       uvc_start_streaming(handle,&ctrl,frame_callback,NULL,0)>=0)started=1;
                    if(!started){
                        capture_yuyv=0;
                        if(uvc_get_stream_ctrl_format_size(handle,&ctrl,UVC_FRAME_FORMAT_MJPEG,640,480,30)>=0&&
                           uvc_start_streaming(handle,&ctrl,frame_callback,NULL,0)>=0)started=1;
                    }
                }
                if(started){
                    streaming=1;latest_time=now;state="No Video Signal";
                    fprintf(stderr,"HDMI_CAPTURE_STARTED 640x480@30 format=%s\n",capture_yuyv?"YUYV":"MJPEG");
                }else{state="Capture card unavailable";retry=now+3;}
            }
        }
        unsigned seq;size_t size;
        pthread_mutex_lock(&lock);
        seq=latest_seq;size=latest_size;
        if(seq!=displayed&&size)memcpy(copybuf,latest,size);
        time_t last=latest_time;
        pthread_mutex_unlock(&lock);
        if(streaming&&seq!=displayed&&size){
            unsigned w=0,h=0;uint64_t hash=0;displayed=seq;
            uint64_t decode_start=tick_ns();
            int decoded;
            if(capture_yuyv){
                w=640;h=480;
                decoded=size!=(size_t)w*h*2||YUY2ToARGB(copybuf,(int)w*2,argb_src,(int)w*4,(int)w,(int)h);
            }else decoded=decode(size,&w,&h,&hash);
            decode_ns+=tick_ns()-decode_start;
            if(!decoded){
                if(!capture_yuyv&&hash==NO_SIGNAL_HASH){state="No Video Signal";}
                else {draw_frame(w,h,capture_yuyv);state=NULL;}
            }
        }
        if(streaming&&now-last>4){
            state="No Video Signal";
            capture_close(ctx,dev,handle,streaming);ctx=NULL;dev=NULL;handle=NULL;
            streaming=0;retry=now+2;
        }
        if(state&&now!=shown){
            status_screen(state,!strcmp(state,"No Video Signal")?
                          "Check the HDMI source and cable.":
                          "Keep the capture card connected to the dock hub.");
            shown=now;
        }
        usleep(20000);
    }
    capture_close(ctx,dev,handle,streaming);
    close(keyboard);display_close();framebuffer_close();free(latest);free(copybuf);free(rgb);free(argb_src);free(nv12_small);
    puts("HDMI_VIEWER_DONE");return 0;
}
