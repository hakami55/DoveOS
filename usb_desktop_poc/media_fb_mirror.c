#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <libyuv/convert_from_argb.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define W 1280
#define H 720
typedef struct {
    uint8_t *data;uint32_t physical,bytes,width,height,stride,aligned_height;
    uint32_t magic1,ion_handle,magic2,allocated,magic3,magic4;
} vo_buffer;
_Static_assert(sizeof(vo_buffer)==52,"Vendor VOUT ABI");
static volatile sig_atomic_t stop;
static void on_signal(int unused){(void)unused;stop=1;}

int main(int argc,char **argv){
    if(argc>1&&!strcmp(argv[1],"--check")){puts("MEDIA_MIRROR_CHECK_OK");return 0;}
    setvbuf(stdout,NULL,_IONBF,0);
    signal(SIGTERM,on_signal);signal(SIGINT,on_signal);
    int fd=open("/dev/fb0",O_RDONLY|O_CLOEXEC);
    if(fd<0){perror("MEDIA_FB_OPEN");return 1;}
    struct fb_var_screeninfo var={0};struct fb_fix_screeninfo fix={0};
    if(ioctl(fd,FBIOGET_VSCREENINFO,&var)||ioctl(fd,FBIOGET_FSCREENINFO,&fix)||
       var.xres!=W||var.yres!=H||var.bits_per_pixel!=32||fix.line_length<W*4){
        puts("MEDIA_FB_FORMAT_INVALID");close(fd);return 2;
    }
    size_t size=(size_t)fix.line_length*H;
    const uint8_t *fb=mmap(NULL,size,PROT_READ,MAP_SHARED,fd,0);close(fd);
    if(fb==MAP_FAILED){puts("MEDIA_FB_MAP_FAILED");return 3;}
    void *lib=dlopen("/usr/lib/libVOutWrapper.so",RTLD_LAZY|RTLD_LOCAL|RTLD_DEEPBIND);
    if(!lib){puts("MEDIA_VOUT_LIB_FAILED");munmap((void*)fb,size);return 4;}
    int (*init)(void)=dlsym(lib,"vo_wrap_init");
    int (*force)(void)=dlsym(lib,"vo_wrap_v1_force_on");
    int (*alloc)(vo_buffer*)=dlsym(lib,"vo_wrap_alloc_nv12_buffer");
    int (*vo_stop)(void)=dlsym(lib,"vo_wrap_v1_stop");
    int (*deinit)(void)=dlsym(lib,"vo_wrap_deinit");
    int (*free_buffer)(vo_buffer*)=dlsym(lib,"vo_wrap_free_nv12_buffer");
    int (*status)(int)=dlsym(lib,"rvsd_vo_PLockGetStatus");
    void (*set_status)(int,int)=dlsym(lib,"rvsd_vo_PLockSetStatus");
    uint32_t (*lock_address)(int)=dlsym(lib,"rvsd_vo_PLockGetPhyAddr");
    uint32_t (*received_address)(int)=dlsym(lib,"rvsd_vo_PLockGetReceivedPhyAddr");
    uint32_t (*version)(void)=dlsym(lib,"rvsd_vo_PLockGetVersion");
    int (*command)(int,void*,int)=dlsym(lib,"process_InbandCmd");
    if(!init||!force||!alloc||!vo_stop||!deinit||!free_buffer||!status||
       !set_status||!lock_address||!received_address||!version||!command||init()){
        puts("MEDIA_VOUT_INIT_FAILED");dlclose(lib);munmap((void*)fb,size);return 5;
    }
    vo_buffer buffers[4]={0};int count=0,ok=1;unsigned frames=0;
    for(int i=0;i<4;i++){
        buffers[i].width=W;buffers[i].height=H;buffers[i].stride=W;buffers[i].aligned_height=H;
        if(alloc(&buffers[i])||!buffers[i].data||buffers[i].bytes<W*H*3/2){ok=0;break;}
        count++;
    }
    if(ok&&(unsigned)force()!=0x10000000u)ok=0;
    if(!ok){puts("MEDIA_VOUT_ALLOC_FAILED");goto done;}
    puts("MEDIA_MIRROR_READY");
    unsigned slot=1;unsigned char used[4]={0};
    time_t end=time(NULL)+(argc>=4?atoi(argv[3]):3600);
    while(!stop&&time(NULL)<end){
        int chosen=0;
        for(int attempt=0;attempt<3;attempt++){
            int candidate=(int)slot;slot=(slot%3)+1;
            if(!used[candidate]||status(candidate)==3){chosen=candidate;break;}
        }
        if(chosen){
            vo_buffer *v=&buffers[chosen];
            if(ARGBToNV12(fb,(int)fix.line_length,v->data,W,
                          v->data+(size_t)W*H,W,W,H)){
                puts("MEDIA_MIRROR_CONVERT_FAILED");break;
            }
            __sync_synchronize();
            uint32_t words[28]={0};
            words[0]=version();words[1]=4;words[2]=v->physical;
            words[3]=v->physical+v->stride*v->aligned_height;
            words[4]=lock_address(chosen);words[5]=v->width;words[6]=v->height;
            words[7]=words[8]=v->stride;
            words[21]=words[22]=words[23]=UINT32_MAX;
            words[26]=received_address(chosen);
            set_status(chosen,1);__sync_synchronize();
            if(command(39,words,sizeof words)){
                used[chosen]=1;frames++;
                if(frames%120==0)printf("MEDIA_MIRROR_FRAMES=%u\n",frames);
            }
        }
        usleep(20000);
    }
done:
    if((unsigned)vo_stop()==0x10000000u)
        for(int i=0;i<count;i++)free_buffer(&buffers[i]);
    deinit();dlclose(lib);munmap((void*)fb,size);
    printf("MEDIA_MIRROR_DONE frames=%u\n",frames);return ok?0:6;
}
