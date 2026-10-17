/* Hardware VOUT bridge. No CPU pixel conversion or framebuffer mode change. */
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>
#define HW_STATIC_TEST 0
#define HW_COPY_TEST 0
typedef struct {
    uint8_t *data;uint32_t physical,bytes,width,height,stride,aligned_height;
    uint32_t magic1,ion,magic2,allocated,magic3,magic4;
} hw_vo_buffer;
_Static_assert(sizeof(hw_vo_buffer)==52,"VOUT ABI");
static void *vo_library;
static int (*vo_render)(hw_vo_buffer*),(*vo_stop)(void),(*vo_deinit)(void);
static int (*vo_status)(int);
static void (*vo_set_status)(int,int);
static uint32_t (*vo_lock_address)(int),(*vo_received_address)(int),(*vo_version)(void);
static int (*vo_command)(int,void*,int);
static int (*vo_alloc)(hw_vo_buffer*),(*vo_free)(hw_vo_buffer*);
static hw_vo_buffer display_buffers[8];
static int vo_active,hw_width,hw_height,pmfd=-1;
static unsigned displayed,render_dropped;
static int audio_mode,audio_volume=70;
static uint8_t *fb_map,*fb_backup;
static int fbfd=-1;
#define FB_BYTES (1280*720*4)
static uint32_t physical_buffer(const uint8_t *ptr,size_t bytes) {
    long page=sysconf(_SC_PAGESIZE);uint64_t first=0;
    if(pmfd<0)pmfd=open("/proc/self/pagemap",O_RDONLY|O_CLOEXEC);
    if(pmfd<0||page!=4096)return 0;
    uintptr_t start=(uintptr_t)ptr,base=start&~(uintptr_t)(page-1);
    for(size_t i=0;i<bytes+(start-base);i+=(size_t)page) {
        uint64_t entry=0;
        if(pread(pmfd,&entry,8,(off_t)(((base+i)/page)*8))!=8 || !(entry&(1ULL<<63)))return 0;
        uint64_t pfn=entry&((1ULL<<55)-1);
        if(!pfn)return 0;
        if(i==0)first=pfn;
        else if(pfn!=first+i/(size_t)page)return 0;
    }
    uint64_t physical=first*page+(start-base);
    return physical && physical+bytes<=UINT32_MAX ? (uint32_t)physical:0;
}
static int output_open(void) {
    vo_library=dlopen("/usr/lib/libVOutWrapper.so",RTLD_LAZY|RTLD_LOCAL|RTLD_DEEPBIND);
    if(!vo_library){printf("VOUT_LOAD_ERROR=%s\n",dlerror());return -1;}
    int (*init)(void)=dlsym(vo_library,"vo_wrap_init");
    int (*force)(void)=dlsym(vo_library,"vo_wrap_v1_force_on");
    vo_render=dlsym(vo_library,"vo_wrap_render");vo_stop=dlsym(vo_library,"vo_wrap_v1_stop");
    vo_deinit=dlsym(vo_library,"vo_wrap_deinit");
    vo_status=dlsym(vo_library,"rvsd_vo_PLockGetStatus");
    vo_set_status=dlsym(vo_library,"rvsd_vo_PLockSetStatus");
    vo_lock_address=dlsym(vo_library,"rvsd_vo_PLockGetPhyAddr");
    vo_received_address=dlsym(vo_library,"rvsd_vo_PLockGetReceivedPhyAddr");
    vo_version=dlsym(vo_library,"rvsd_vo_PLockGetVersion");
    vo_command=dlsym(vo_library,"process_InbandCmd");
    vo_alloc=dlsym(vo_library,"vo_wrap_alloc_nv12_buffer");
    vo_free=dlsym(vo_library,"vo_wrap_free_nv12_buffer");
    if(!init||!force||!vo_render||!vo_stop||!vo_deinit||!vo_status||!vo_set_status||
       !vo_lock_address||!vo_received_address||!vo_version||!vo_command||!vo_alloc||!vo_free)return -1;
    if(init())return -1;
    vo_active=1;
    if((unsigned)force()!=0x10000000u)return -1;
    fbfd=open("/dev/fb0",O_RDWR|O_CLOEXEC);
    struct fb_var_screeninfo v={0};struct fb_fix_screeninfo f={0};
    if(fbfd<0||ioctl(fbfd,FBIOGET_VSCREENINFO,&v)||ioctl(fbfd,FBIOGET_FSCREENINFO,&f)||
       v.xres!=1280||v.yres!=720||v.bits_per_pixel!=32||f.line_length!=5120)return -1;
    fb_map=mmap(NULL,FB_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,fbfd,0);
    if(fb_map==MAP_FAILED){fb_map=NULL;return -1;}
    fb_backup=malloc(FB_BYTES);if(!fb_backup)return -1;
    memcpy(fb_backup,fb_map,FB_BYTES);memset(fb_map,0,FB_BYTES);
    return 0;
}
static int output_queue(hw_vo_buffer *b,int slot) {
#if defined(DOVE_AIRPLAY_ASPECT_FIT)
    /* VOUT fills the panel regardless of source aspect. Feed its known-good
       1280x720 layout, with the phone's pixels centred and chroma row padding
       removed. No colour conversion, firmware write or decoder replacement. */
    if(slot<0 || slot>=8 || !b->width || !b->height ||
       b->width>1280 || b->height>720 || (b->width&1) || (b->height&1) ||
       b->stride<b->width || b->aligned_height<b->height ||
       (uint64_t)b->stride*b->aligned_height+(uint64_t)b->stride*b->height/2>b->bytes)return 0;
    hw_vo_buffer *fit=&display_buffers[slot];
    if(!fit->data){
        fit->width=1280;fit->height=720;fit->stride=1280;fit->aligned_height=720;
        if(vo_alloc(fit) || !fit->data || !fit->physical || fit->bytes<1280u*720u*3u/2u)return 0;
        memset(fit->data,16,1280u*720u);
        memset(fit->data+1280u*720u,128,1280u*720u/2u);
        printf("AIRPLAY_ASPECT_FIT slot=%d source=%ux%u stride=%u slice=%u target=1280x720\n",
               slot,b->width,b->height,b->stride,b->aligned_height);
    }
    unsigned width=(unsigned)airplay_visible_width,height=(unsigned)airplay_visible_height;
    if(!width || !height || width>b->width || height>b->height)return 0;
    unsigned x=((1280-width)/2)&~1u;
    unsigned y=((720-height)/2)&~1u;
    for(unsigned row=0;row<height;row++)
        memcpy(fit->data+(y+row)*1280+x,b->data+row*b->stride,width);
    const uint8_t *uv=b->data+b->stride*b->aligned_height;
    uint8_t *fit_uv=fit->data+1280u*720u;
    for(unsigned row=0;row<height/2;row++)
        memcpy(fit_uv+(y/2+row)*1280+x,uv+row*b->stride,width);
    if(!displayed && getenv("DOVE_AIRPLAY_FRAME_PROBE")){
        int source=open("/tmp/doveos-airplay-source-frame.yuv",O_WRONLY|O_CREAT|O_TRUNC,0600);
        if(source>=0){
            size_t left=b->bytes;const uint8_t *p=b->data;
            while(left){ssize_t n=write(source,p,left);if(n<=0)break;p+=n;left-=(size_t)n;}
            close(source);
        }
        int probe=open("/tmp/doveos-airplay-first-frame.nv12",O_WRONLY|O_CREAT|O_TRUNC,0600);
        if(probe>=0){
            size_t left=1280u*720u*3u/2u;const uint8_t *p=fit->data;
            while(left){ssize_t n=write(probe,p,left);if(n<=0)break;p+=n;left-=(size_t)n;}
            close(probe);
        }
    }
    b=fit;
#endif
    /* Diagnostic copy into the same allocator used by the verified pattern.
       This isolates decoder allocation visibility from VOUT layout. */
    if(HW_COPY_TEST){
    hw_vo_buffer *dst=&display_buffers[slot];
    if(!dst->data){
        dst->width=b->width;dst->height=b->height;dst->stride=b->stride;
        dst->aligned_height=(b->height+15)&~15;
        if(vo_alloc(dst)||!dst->data||!dst->physical)return 0;
        printf("HW_DISPLAY_BUFFER slot=%d physical=%08x bytes=%u\n",slot,dst->physical,dst->bytes);
    }
    memcpy(dst->data,b->data,b->stride*b->height);
    memcpy(dst->data+dst->stride*dst->aligned_height,
           b->data+b->stride*b->aligned_height,b->stride*b->height/2);
    b=dst;
    }
    if(HW_STATIC_TEST){int rc=vo_render(b);printf("HW_STILL_RENDER=%d\n",rc);return rc==0;}
    /* Same 112-byte command as vo_wrap_render, with a separate firmware lock
       per OMX output buffer. No fixed 50/10/10 ms sleeps. */
    uint32_t command[28]={0};
    command[0]=vo_version();command[1]=4;command[2]=b->physical;
    command[3]=b->physical+b->stride*b->aligned_height;
    command[4]=vo_lock_address(slot);command[5]=b->width;command[6]=b->height;
    command[7]=command[8]=b->stride;
    command[21]=command[22]=command[23]=UINT32_MAX;
    command[26]=vo_received_address(slot);
    vo_set_status(slot,1);__sync_synchronize();
    return vo_command(39,command,sizeof command);
}
static void output_close(void) {
    if(vo_active){
        unsigned rc=(unsigned)vo_stop();printf("HW_VOUT_STOP=%08x\n",rc);
        if(rc!=0x10000000u){puts("DOVEPLAYER_RETAINED_UNRETIRED_BUFFERS");fflush(stdout);alarm(0);signal(SIGTERM,SIG_IGN);signal(SIGINT,SIG_IGN);for(;;)sleep(60);}
        if(rc==0x10000000u)for(int i=0;i<8;i++)if(display_buffers[i].data)vo_free(&display_buffers[i]);
        vo_deinit();vo_active=0;
    }
    if(fb_map&&fb_backup)memcpy(fb_map,fb_backup,FB_BYTES);
    if(fb_map)munmap(fb_map,FB_BYTES);
    free(fb_backup);fb_map=fb_backup=NULL;
    if(fbfd>=0)close(fbfd);
    if(pmfd>=0)close(pmfd);
    fbfd=pmfd=-1;
}
