#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xdamage.h>
#include <libyuv/convert_from_argb.h>
#include "gpu/gpu_backend.h"
#include "keyboard_settings.h"

#define W 1280
#define H 720
static void *gpu_library,*gpu_state;
static DoveGpuConvert gpu_convert;
static DoveGpuDestroy gpu_destroy;
static void renderer_status(const char *text){
    FILE *f=fopen("/tmp/doveos-render-status","w");if(f){fprintf(f,"%s\n",text);fclose(f);}
    printf("RENDERER %s\n",text);
}
static double render_time(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static void gpu_fallback(const char *reason){
    if(gpu_state&&gpu_destroy)gpu_destroy(gpu_state);
    gpu_state=NULL;renderer_status(reason);
}
static void renderer_init(const uint8_t *canvas,uint8_t *nv12){
    const char *mode=getenv("DOVE_GPU_MODE"),*path=getenv("DOVE_GPU_LIBRARY");
    if(!mode||(!strcmp(mode,"cpu"))){renderer_status("CPU display conversion");return;}
    if(strcmp(mode,"gpu")&&strcmp(mode,"auto")){renderer_status("CPU: invalid GPU setting");return;}
    gpu_library=path?dlopen(path,RTLD_NOW|RTLD_GLOBAL):NULL;
    DoveGpuCreate create=gpu_library?(DoveGpuCreate)dlsym(gpu_library,"dove_gpu_create"):NULL;
    gpu_convert=gpu_library?(DoveGpuConvert)dlsym(gpu_library,"dove_gpu_convert"):NULL;
    gpu_destroy=gpu_library?(DoveGpuDestroy)dlsym(gpu_library,"dove_gpu_destroy"):NULL;
    if(!create||!gpu_convert||!gpu_destroy||(gpu_state=create(W,H))==NULL){gpu_fallback("CPU: GPU runtime unavailable");return;}
    uint8_t *reference=malloc(W*H*3/2);
    if(!reference){gpu_fallback("CPU: insufficient memory for GPU check");return;}
    double t=render_time();int failed=ARGBToNV12(canvas,W*4,reference,W,reference+W*H,W,W,H);double cpu_ms=(render_time()-t)*1000;
    t=render_time();failed|=gpu_convert(gpu_state,canvas,nv12,0,0,W,H);double gpu_ms=(render_time()-t)*1000;
    unsigned bad=0;for(int i=0;!failed&&i<W*H*3/2;i++)if(abs((int)reference[i]-nv12[i])>2)bad++;
    printf("GPU_CHECK bad=%u cpu_ms=%.3f gpu_ms=%.3f\n",bad,cpu_ms,gpu_ms);
    if(failed||bad){gpu_fallback("CPU: GPU image check failed");memcpy(nv12,reference,W*H*3/2);}
    else if(!strcmp(mode,"auto")&&gpu_ms>cpu_ms){gpu_fallback("CPU: faster than GPU conversion (Auto)");memcpy(nv12,reference,W*H*3/2);}
    else renderer_status("Experimental GPU display conversion; apps use CPU drawing");
    free(reference);
}
typedef struct {uint8_t *data;uint32_t physical,bytes,width,height,stride,aligned_height;
    uint32_t magic1,ion_handle,magic2,allocated,magic3,magic4;} vo_buffer;
_Static_assert(sizeof(vo_buffer)==52,"VOUT ABI");
static volatile sig_atomic_t done;
static int button_pipe[2]={-1,-1};
static volatile int raw_buttons_ready;
static void stop(int unused){(void)unused;done=1;}
static long milliseconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000L+t.tv_nsec/1000000;}
static void cursor_pattern(uint8_t *canvas,int px,int py,const char **rows,int height){
    for(int y=0;y<height;y++)for(int x=0;rows[y][x];x++){
        int sx=px+x,sy=py+y;char c=rows[y][x];
        if(c==' '||sx<0||sx>=W||sy<0||sy>=H)continue;
        ((uint32_t*)canvas)[sy*W+sx]=c=='#'?0x101820:c=='o'?0xffc74d:0xffffff;
    }
}
static const char *fallback_arrow[]={
 "#","##","#.#","#..#","#...#","#....#","#.....#","#......#",
 "#.......#","#........#","#.........#","#..........#","#......#####",
 "#...#..#","#..##..#","#.# #...#","##  #...#","#    #...#",
 "     #...#","      #.#","       #"};
static const char *busy_hourglass[]={
 "############","#..........#","############"," #........#"," #.oooooo.#",
 "  #.oooo.#","   #.oo.#","    #..#","    #oo#","   #.oo.#","  #.oooo.#",
 " #.oooooo.#"," #........#","############","#..........#","############"};
static void *raw_buttons(void *unused){
    (void)unused;
    if(access("/sys/kernel/debug/hid",R_OK)&&mount("debugfs","/sys/kernel/debug","debugfs",0,NULL)){
        printf("PHYSICAL_BUTTON_INPUT_UNAVAILABLE mount_errno=%d\n",errno);return NULL;
    }
    DIR *d=opendir("/sys/bus/hid/devices");if(!d)return NULL;
    char path[256]={0};struct dirent *e;
    while((e=readdir(d))){
        if(strncmp(e->d_name,"0003:03F0:0B51.",15)||strlen(e->d_name)!=19)continue;
        char p[256];snprintf(p,sizeof p,"/sys/bus/hid/devices/%.19s/report_descriptor",e->d_name);
        FILE *f=fopen(p,"rb");if(!f)continue;unsigned char b[4]={0};size_t n=fread(b,1,4,f);fclose(f);
        if(n==4&&b[0]==5&&b[1]==1&&b[2]==9&&b[3]==2){
            snprintf(path,sizeof path,"/sys/kernel/debug/hid/%.19s/events",e->d_name);break;
        }
    }
    closedir(d);if(!path[0])return NULL;FILE *f=fopen(path,"r");if(!f)return NULL;
    raw_buttons_ready=1;puts("PHYSICAL_BUTTON_INPUT_READY");char line[256];
    while(!done&&fgets(line,sizeof line,f)){
        unsigned report,buttons;
        if(sscanf(line,"report (size %*u) (numbered) = %x %x",&report,&buttons)==2&&report==1){
            unsigned char b=buttons&3;if(write(button_pipe[1],&b,1)!=1)break;
        }
    }
    raw_buttons_ready=0;fclose(f);return NULL;
}
static void button(Display *d,unsigned n,int down){XTestFakeButtonEvent(d,n,down,CurrentTime);}
static void click(Display *d,unsigned n){button(d,n,1);button(d,n,0);}
static int reverse_scroll(void){
    char value[20]={0};
    FILE *file=fopen("/media/usb0/DoveOS-v2/home/scroll-direction.txt","r");
    if(!file)return 1;
    if(fgets(value,sizeof value,file))value[strcspn(value,"\r\n")]=0;
    fclose(file);
    return strcmp(value,"normal")!=0;
}
static void volume_key(unsigned code){
    static int remembered=70;
    int current=70;
    FILE *file=fopen("/media/usb0/DrMoSa/sound-volume.txt","r");
    if(file){if(fscanf(file,"%d",&current)!=1)current=70;fclose(file);}
    if(current<0||current>100)current=70;
    int next=current;
    if(code==KEY_VOLUMEUP)next=current+10;
    if(code==KEY_VOLUMEDOWN)next=current-10;
    if(code==KEY_MUTE){if(current){remembered=current;next=0;}else next=remembered;}
    if(next<0)next=0;
    if(next>100)next=100;
    char value[8];snprintf(value,sizeof value,"%d",next);
    pid_t child=fork();
    if(child==0){
        execl("/bin/sh","sh","/tmp/drmosa-usb-menu-runtime/settings.sh","sound-set",value,(char *)NULL);
        _exit(127);
    }
}
static void keyboard_chord(Display *d,int modifier,int key,int down){
    if(down){XTestFakeKeyEvent(d,modifier+8,True,CurrentTime);XTestFakeKeyEvent(d,key+8,True,CurrentTime);}
    else{XTestFakeKeyEvent(d,key+8,False,CurrentTime);XTestFakeKeyEvent(d,modifier+8,False,CurrentTime);}
}
static void keyboard_dispatch(Display *d,int action,int down){
    if(action==KEY_MUTE||action==KEY_VOLUMEUP||action==KEY_VOLUMEDOWN){if(down)volume_key((unsigned)action);return;}
    if(action==KEY_BRIGHTNESSUP||action==KEY_BRIGHTNESSDOWN){
        if(down){int value=40;FILE *f=fopen("/media/usb0/DoveOS-v2/home/display-brightness.txt","r");if(f){if(fscanf(f,"%d",&value)!=1)value=40;fclose(f);}
            value+=action==KEY_BRIGHTNESSUP?10:-10;if(value<3)value=3;if(value>100)value=100;
            char path[180];snprintf(path,sizeof path,"/media/usb0/DoveOS-v2/home/display-brightness.txt.%ld.new",(long)getpid());
            f=fopen(path,"w");if(f){fprintf(f,"%d\n",value);int bad=fflush(f)||fsync(fileno(f));bad|=fclose(f)!=0;if(!bad)rename(path,"/media/usb0/DoveOS-v2/home/display-brightness.txt");else unlink(path);}
        }return;
    }
    if(action==DOVE_KEY_SETTINGS||action==DOVE_KEY_FILES||action==DOVE_KEY_TERMINAL){
        if(down&&fork()==0){unsetenv("LD_PRELOAD");unsetenv("LD_LIBRARY_PATH");execl("/bin/sh","sh","/tmp/doveos-theme/keyboard-action.sh",action==DOVE_KEY_SETTINGS?"settings":action==DOVE_KEY_FILES?"files":"terminal",(char*)NULL);_exit(127);}return;
    }
    switch(action){
    case DOVE_KEY_ALT_TAB:keyboard_chord(d,KEY_LEFTALT,KEY_TAB,down);return;
    case DOVE_KEY_ALT_F4:keyboard_chord(d,KEY_LEFTALT,KEY_F4,down);return;
    case DOVE_KEY_COPY:keyboard_chord(d,KEY_LEFTCTRL,KEY_C,down);return;
    case DOVE_KEY_PASTE:keyboard_chord(d,KEY_LEFTCTRL,KEY_V,down);return;
    case DOVE_KEY_SELECT_ALL:keyboard_chord(d,KEY_LEFTCTRL,KEY_A,down);return;
    }
    if(action>0&&action<248)XTestFakeKeyEvent(d,(unsigned)action+8,down,CurrentTime);
}
int main(int argc,char **argv){
    if(argc>1&&!strcmp(argv[1],"--check")){puts("DOVEOS_X11_BRIDGE_CHECK_OK");return 0;}
    sigset_t allowed_signals;sigemptyset(&allowed_signals);sigprocmask(SIG_SETMASK,&allowed_signals,NULL);
    setvbuf(stdout,NULL,_IONBF,0);signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGCHLD,SIG_IGN);
    Display *display=NULL;
    for(int attempt=0;attempt<50&&!display;attempt++){
        display=XOpenDisplay(":7");
        if(!display)usleep(100000);
    }
    if(!display){puts("X11_UNAVAILABLE");return 1;}
    if(argc>1&&!strcmp(argv[1],"--cursor-check")){
        XFixesCursorImage *c=XFixesGetCursorImage(display);unsigned visible=0;
        if(c){for(unsigned i=0;i<c->width*c->height;i++)if((uint32_t)c->pixels[i]>>24)visible++;
            printf("CURSOR x=%d y=%d width=%u height=%u visible_pixels=%u\n",c->x,c->y,c->width,c->height,visible);XFree(c);}
        else puts("CURSOR_IMAGE_UNAVAILABLE");
        XCloseDisplay(display);return 0;
    }
    XFixesHideCursor(display,DefaultRootWindow(display));
    XSync(display,False);
    int event,error,major,minor;if(!XTestQueryExtension(display,&event,&error,&major,&minor)){puts("XTEST_UNAVAILABLE");return 2;}
    /* JWM grabs the server while drawing resize outlines. Input and cursor
       queries must still run, otherwise the button release cannot reach JWM. */
    XTestGrabControl(display,True);
    XSync(display,False);
    puts("XTEST_SERVER_GRAB_BYPASS_READY");
    int damage_event,damage_error;
    Damage damage=XDamageQueryExtension(display,&damage_event,&damage_error)?XDamageCreate(display,DefaultRootWindow(display),XDamageReportNonEmpty):0;
    int xfd=open("/tmp/doveos-v2-x/Xvfb_screen0",O_RDONLY|O_CLOEXEC);struct stat st;
    if(xfd<0||fstat(xfd,&st)||st.st_size<100){puts("XWD_UNAVAILABLE");return 3;}
    size_t mapsize=(size_t)st.st_size;const uint8_t *xmap=mmap(NULL,mapsize,PROT_READ,MAP_SHARED,xfd,0);close(xfd);
    if(xmap==MAP_FAILED)return 4;
    uint32_t hdr[25];memcpy(hdr,xmap,100);for(int i=0;i<25;i++)hdr[i]=ntohl(hdr[i]);
    size_t offset=(size_t)hdr[0]+(size_t)hdr[19]*12;
    if(hdr[1]!=7||hdr[4]!=W||hdr[5]!=H||hdr[11]!=32||hdr[12]!=W*4||offset+W*H*4>mapsize){
        printf("XWD_FORMAT width=%u height=%u bpp=%u stride=%u offset=%zu\n",hdr[4],hdr[5],hdr[11],hdr[12],offset);return 5;
    }
    int fbfd=open("/dev/fb0",O_RDWR|O_CLOEXEC);struct fb_var_screeninfo var={0};struct fb_fix_screeninfo fix={0};
    if(fbfd<0||ioctl(fbfd,FBIOGET_VSCREENINFO,&var)||ioctl(fbfd,FBIOGET_FSCREENINFO,&fix)||
       var.xres!=W||var.yres!=H||var.bits_per_pixel!=32||fix.line_length!=W*4){puts("FB_FORMAT_UNAVAILABLE");return 6;}
    uint8_t *fb=mmap(NULL,W*H*4,PROT_READ|PROT_WRITE,MAP_SHARED,fbfd,0);close(fbfd);if(fb==MAP_FAILED)return 7;
    /* Convert from cached RAM, never read the uncached device framebuffer. */
    uint8_t *canvas=malloc(W*H*4);if(!canvas)return 7;
    uint8_t *nv12=malloc(W*H*3/2);if(!nv12)return 7;
    uint8_t *background=malloc(W*H*4);if(!background)return 7;
    int old_left=0,old_top=0,old_right=0,old_bottom=0;
    void *lib=dlopen("/usr/lib/libVOutWrapper.so",RTLD_LAZY|RTLD_LOCAL|RTLD_DEEPBIND);if(!lib){printf("VOUT_LOAD %s\n",dlerror());return 8;}
    int (*init)(void)=dlsym(lib,"vo_wrap_init"),(*force)(void)=dlsym(lib,"vo_wrap_v1_force_on");
    int (*alloc)(vo_buffer*)=dlsym(lib,"vo_wrap_alloc_nv12_buffer"),(*release)(vo_buffer*)=dlsym(lib,"vo_wrap_free_nv12_buffer");
    int (*vstop)(void)=dlsym(lib,"vo_wrap_v1_stop"),(*deinit)(void)=dlsym(lib,"vo_wrap_deinit");
    int (*status)(int)=dlsym(lib,"rvsd_vo_PLockGetStatus"),(*command)(int,void*,int)=dlsym(lib,"process_InbandCmd");
    void (*set_status)(int,int)=dlsym(lib,"rvsd_vo_PLockSetStatus");
    uint32_t (*lock_address)(int)=dlsym(lib,"rvsd_vo_PLockGetPhyAddr"),(*received_address)(int)=dlsym(lib,"rvsd_vo_PLockGetReceivedPhyAddr");
    uint32_t (*version)(void)=dlsym(lib,"rvsd_vo_PLockGetVersion");
    if(!init||!force||!alloc||!release||!vstop||!deinit||!status||!command||!set_status||!lock_address||!received_address||!version||init())return 9;
    vo_buffer buffers[4]={0};int count=0;
    for(int i=0;i<4;i++){buffers[i].width=W;buffers[i].height=H;buffers[i].stride=W;buffers[i].aligned_height=H;
        if(alloc(&buffers[i])||!buffers[i].data||buffers[i].bytes<W*H*3/2)break;
        count++;}
    if(count!=4||(unsigned)force()!=0x10000000u){puts("VOUT_START_FAILED");return 10;}
    struct pollfd fds[4]={{open("/dev/input/event1",O_RDONLY|O_NONBLOCK|O_CLOEXEC),POLLIN,0},
                        {open("/dev/input/event2",O_RDONLY|O_NONBLOCK|O_CLOEXEC),POLLIN,0},{-1,POLLIN,0},{-1,POLLIN,0}};
#ifdef CLICKPAD_RAM_TRIAL
    int trial_sock=socket(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    struct sockaddr_in trial_addr={.sin_family=AF_INET,.sin_port=htons(18829),
                                  .sin_addr={.s_addr=htonl(INADDR_LOOPBACK)}};
    if(trial_sock<0 || bind(trial_sock,(struct sockaddr*)&trial_addr,sizeof trial_addr)){puts("CLICKPAD_TRIAL_SOCKET_FAILED");return 11;}
    fds[3].fd=trial_sock;
    long trial_seen=0,trial_press=0;int trial_live=0,trial_button=0;unsigned trial_seq=0;
    puts("CLICKPAD_TRIAL_LISTENING right_zone=x>=2302,y>=1265");
#else
    const int trial_live=0;
#endif
    pthread_t hid;if(!pipe(button_pipe)){fcntl(button_pipe[0],F_SETFL,O_NONBLOCK);fcntl(button_pipe[1],F_SETFL,O_NONBLOCK);
        fds[2].fd=button_pipe[0];if(!pthread_create(&hid,NULL,raw_buttons,NULL))pthread_detach(hid);}
    int touching=0,hx=0,hy=0,tx=0,ty=0,dx=0,dy=0,pdx=0,pdy=0,moving=0,physical=0,two=0,sawtwo=0;
    int rx=0,ry=0,ctrl=0,alt=0,scroll=0,travel=0;unsigned char buttons=0;
    unsigned char modifiers[KEY_MAX+1]={0};int key_routes[KEY_MAX+1]={0};
    DoveKeyboard keyboard=dove_keyboard_load();long next_keyboard_check=0;
    long touched=0,nextframe=0,deadline=argc>1?milliseconds()+atoi(argv[1])*1000L:0;
    unsigned frames=0,slot=1;unsigned char used[4]={0};
    int renderer_checked=0,external_active=0;
    long measured=milliseconds(),copy_ms=0,convert_ms=0;unsigned measured_frames=0;
    int scroll_reversed=reverse_scroll(),busy=0,power_blank=0,previous_blank=0;long next_scroll_check=milliseconds()+5000,next_busy_check=0,next_power_check=0;
    XTestFakeMotionEvent(display,-1,W/2,H/2,CurrentTime);XFlush(display);
    puts("DOVEOS_X11_BRIDGE_READY keyboard=1 touchpad=1 resolution=1280x720");
    while(!done&&access("/tmp/doveos-v2-stop",F_OK)&&(!deadline||milliseconds()<deadline)){
        if(milliseconds()>=next_keyboard_check){keyboard=dove_keyboard_load();next_keyboard_check=milliseconds()+1000;}
        poll(fds,4,2);
#ifdef CLICKPAD_RAM_TRIAL
        if(fds[3].revents&POLLIN){unsigned char p[12];ssize_t n;
            while((n=recv(trial_sock,p,sizeof p,MSG_TRUNC))>0){
                if(n!=12||p[0]!=0x43||p[1]!=0x50||p[2]!=1||p[3]>1)continue;
                unsigned seq=p[4]|((unsigned)p[5]<<8)|((unsigned)p[6]<<16)|((unsigned)p[7]<<24);
                unsigned x=p[8]|((unsigned)p[9]<<8),y=p[10]|((unsigned)p[11]<<8);
                if(x>2877||y>1620||(trial_live&&seq<=trial_seq))continue;
                trial_seq=seq;trial_seen=milliseconds();
                if(!trial_live){if(buttons&1)button(display,1,0);if(buttons&2)button(display,3,0);puts("CLICKPAD_TRIAL_ACTIVE");trial_live=1;}
                int next=p[3]?(trial_button?trial_button:(x>=2302&&y>=1265?3:1)):0;
                if(next!=trial_button){
                    if(trial_button)button(display,(unsigned)trial_button,0);
                    if(next)button(display,(unsigned)next,1);
                    printf("CLICKPAD_TRIAL_BUTTON button=%d x=%u y=%u\n",next,x,y);
                    trial_button=next;
                }
                if(p[3]){physical=1;trial_press=trial_seen;}
            }
        }
        if(trial_live&&milliseconds()-trial_seen>250){
            if(trial_button)button(display,(unsigned)trial_button,0);
            trial_button=0;trial_live=0;physical=1;puts("CLICKPAD_TRIAL_EXPIRED_ORIGINAL_BUTTONS_RESTORED");
        }
#endif
        for(int device=0;device<2;device++)if(fds[device].revents&POLLIN){
            struct input_event ev;
            while(read(fds[device].fd,&ev,sizeof ev)==sizeof ev){
                if(!device&&ev.type==EV_KEY){
                    if(ev.code==KEY_LEFTCTRL||ev.code==KEY_RIGHTCTRL)ctrl=ev.value!=0;
                    if(ev.code==KEY_LEFTALT||ev.code==KEY_RIGHTALT)alt=ev.value!=0;
                    if(ev.code==KEY_BACKSPACE&&ev.value&&ctrl&&alt){done=1;break;}
                    if(ev.code>KEY_MAX||ev.value==2)continue;
                    modifiers[ev.code]=ev.value!=0;
                    int modified=modifiers[KEY_LEFTCTRL]||modifiers[KEY_RIGHTCTRL]||modifiers[KEY_LEFTALT]||modifiers[KEY_RIGHTALT]||modifiers[KEY_LEFTSHIFT]||modifiers[KEY_RIGHTSHIFT]||modifiers[KEY_LEFTMETA]||modifiers[KEY_RIGHTMETA];
                    if(ev.value==1)key_routes[ev.code]=dove_keyboard_action(&keyboard,ev.code,modified);
                    int action=key_routes[ev.code]?key_routes[ev.code]:ev.code;
                    keyboard_dispatch(display,action,ev.value!=0);
                    if(!ev.value)key_routes[ev.code]=0;
                }else if(device){
                    if(ev.type==EV_KEY&&ev.code==BTN_TOUCH){
                        if(ev.value){touching=1;hx=hy=dx=dy=pdx=pdy=moving=physical=sawtwo=scroll=travel=rx=ry=0;touched=milliseconds();}
                        else if(touching){
#ifdef CLICKPAD_RAM_TRIAL
                            if(trial_live&&(trial_button||milliseconds()-trial_press<150))physical=1;
#endif
                            if(!moving&&!physical&&!scroll&&travel<80&&milliseconds()-touched<700){click(display,sawtwo?3:1);}
                            touching=0;hx=hy=dx=dy=0;}
                    }else if(ev.type==EV_KEY&&ev.code==BTN_TOOL_DOUBLETAP){two=ev.value!=0;if(two){sawtwo=1;dx=dy=0;}}
                    else if(ev.type==EV_ABS&&touching&&ev.code==ABS_X){if(hx){dx+=ev.value-tx;travel+=abs(ev.value-tx);}tx=ev.value;hx=1;}
                    else if(ev.type==EV_ABS&&touching&&ev.code==ABS_Y){if(hy){dy+=ev.value-ty;travel+=abs(ev.value-ty);}ty=ev.value;hy=1;}
                    else if(!trial_live&&!raw_buttons_ready&&ev.type==EV_KEY&&(ev.code==BTN_LEFT||ev.code==BTN_RIGHT)){button(display,ev.code==BTN_LEFT?1:3,ev.value!=0);if(ev.value)physical=1;}
                    else if(ev.type==EV_SYN&&ev.code==SYN_REPORT){
                        if(two){pdy+=dy;while(abs(pdy)>=30){click(display,pdy>0?(scroll_reversed?4:5):(scroll_reversed?5:4));pdy+=pdy>0?-30:30;scroll=1;}}
                        else if(!sawtwo){if(!moving){pdx+=dx;pdy+=dy;int dist=abs(pdx)+abs(pdy);
                            if(dist>=28){moving=1;dx=pdx*(dist-28)/dist;dy=pdy*(dist-28)/dist;}else dx=dy=0;}
                            if(moving){rx+=dx*60;ry+=dy*60;int mx=rx/100,my=ry/100;rx-=mx*100;ry-=my*100;
                                if(mx||my)XTestFakeRelativeMotionEvent(display,mx,my,CurrentTime);}}
                        dx=dy=0;
                    }
                }
            }
        }
        if(fds[2].revents&POLLIN){unsigned char state;while(read(fds[2].fd,&state,1)==1){
            unsigned changed=buttons^state;if(!trial_live){if(changed&1)button(display,1,(state&1)!=0);if(changed&2)button(display,3,(state&2)!=0);}
            if(state)physical=1;
            buttons=state;}}
        XFlush(display);long now=milliseconds();
        /* The phone decoder can own VOUT while this process continues input.
           Validate a live owner; an abandoned request never hides the desktop. */
        long external_owner=0;FILE *external_file=fopen("/tmp/doveos-direct-display","r");
        if(external_file){if(fscanf(external_file,"%ld",&external_owner)!=1)external_owner=0;fclose(external_file);}
        int external=external_owner>1&&external_owner<4194304&&!kill((pid_t)external_owner,0);
        if(external&&!external_active){
            if((unsigned)vstop()!=0x10000000u){puts("DIRECT_DISPLAY_RELEASE_FAILED");continue;}
            for(int i=0;i<count;i++)release(&buffers[i]);
            deinit();dlclose(lib);lib=NULL;memset(buffers,0,sizeof buffers);count=0;external_active=1;
            FILE *ack=fopen("/tmp/doveos-direct-display.ready","w");if(ack){fprintf(ack,"%ld\n",external_owner);fclose(ack);}
            puts("DIRECT_DISPLAY_RELEASED_INPUT_ACTIVE");
        }
        if(external_active&&external)continue;
        if(external_active){
            /* vo_wrap_init keeps process-local state after deinit. Reload its
               DSO so the desktop gets a fresh VOUT context after the decoder. */
            lib=dlopen("/usr/lib/libVOutWrapper.so",RTLD_LAZY|RTLD_LOCAL|RTLD_DEEPBIND);
            if(!lib){puts("DIRECT_DISPLAY_RELOAD_FAILED");break;}
            init=dlsym(lib,"vo_wrap_init");force=dlsym(lib,"vo_wrap_v1_force_on");
            alloc=dlsym(lib,"vo_wrap_alloc_nv12_buffer");release=dlsym(lib,"vo_wrap_free_nv12_buffer");
            vstop=dlsym(lib,"vo_wrap_v1_stop");deinit=dlsym(lib,"vo_wrap_deinit");
            status=dlsym(lib,"rvsd_vo_PLockGetStatus");command=dlsym(lib,"process_InbandCmd");
            set_status=dlsym(lib,"rvsd_vo_PLockSetStatus");lock_address=dlsym(lib,"rvsd_vo_PLockGetPhyAddr");
            received_address=dlsym(lib,"rvsd_vo_PLockGetReceivedPhyAddr");version=dlsym(lib,"rvsd_vo_PLockGetVersion");
            if(!init||!force||!alloc||!release||!vstop||!deinit||!status||!command||!set_status||!lock_address||!received_address||!version){puts("DIRECT_DISPLAY_SYMBOLS_FAILED");break;}
            if(init()){puts("DIRECT_DISPLAY_REINIT_FAILED");break;}
            for(int i=0;i<4;i++){buffers[i].width=W;buffers[i].height=H;buffers[i].stride=W;buffers[i].aligned_height=H;
                if(alloc(&buffers[i])||!buffers[i].data||buffers[i].bytes<W*H*3/2)break;
                count++;}
            if(count!=4||(unsigned)force()!=0x10000000u){puts("DIRECT_DISPLAY_RESTORE_FAILED");break;}
            external_active=0;memset(used,0,sizeof used);slot=1;frames=0;measured_frames=0;measured=now;nextframe=0;
            unlink("/tmp/doveos-direct-display.ready");puts("DIRECT_DISPLAY_DESKTOP_RESTORED");
        }
        if(now>=next_power_check){
            double until=0;FILE *f=fopen("/tmp/doveos-screen-blank","r");
            if(f){if(fscanf(f,"%lf",&until)!=1)until=0;fclose(f);}
            power_blank=until>now/1000.0&&until<now/1000.0+3;
            next_power_check=now+100;
        }
        if(now>=next_busy_check){busy=!access("/tmp/doveos-vlc-opening",F_OK);next_busy_check=now+100;}
        if(now>=next_scroll_check){scroll_reversed=reverse_scroll();next_scroll_check=now+5000;}
        if(now<nextframe)continue;
        nextframe=now+16;
        int chosen=0;for(int i=0;i<3;i++){int candidate=(int)slot;slot=slot%3+1;if(!used[candidate]||status(candidate)==3){chosen=candidate;break;}}
        if(!chosen)continue;
        long begin=milliseconds(),converted=begin;int changed=!frames||(previous_blank&&!power_blank);
        if(power_blank){
            memset(nv12,16,W*H);memset(nv12+W*H,128,W*H/2);
            previous_blank=1;nextframe=now+250;
            goto present_power_frame;
        }
        previous_blank=0;
        if(damage){while(XPending(display)){XEvent e;XNextEvent(display,&e);if(e.type==damage_event+XDamageNotify)changed=1;}
            if(changed)XDamageSubtract(display,damage,None,None);
        }else changed=changed||memcmp(background,xmap+offset,W*H*4);
        int left=old_left,top=old_top,right=old_right,bottom=old_bottom;
        if(changed){memcpy(background,xmap+offset,W*H*4);memcpy(canvas,background,W*H*4);left=top=0;right=W;bottom=H;}
        else for(int y=old_top;y<old_bottom;y++)memcpy(canvas+(y*W+old_left)*4,background+(y*W+old_left)*4,(old_right-old_left)*4);
        XFixesCursorImage *cursor=XFixesGetCursorImage(display);
        {
            int px=W/2,py=H/2;unsigned visible=0;
            if(cursor){px=cursor->x;py=cursor->y;
                for(unsigned i=0;i<cursor->width*cursor->height;i++)if((uint32_t)cursor->pixels[i]>>24)visible++;}
            else {Window root,child;int wx,wy;unsigned mask;XQueryPointer(display,DefaultRootWindow(display),&root,&child,&px,&py,&wx,&wy,&mask);}
            int cl=visible?px-cursor->xhot:px,ct=visible?py-cursor->yhot:py;
            int cr=cl+(visible?(int)cursor->width:16),cb=ct+(visible?(int)cursor->height:22);
            if(busy){if(cr<px+30)cr=px+30;if(cb<py+22)cb=py+22;}
            if(cl<0)cl=0;
            if(ct<0)ct=0;
            if(cr>W)cr=W;
            if(cb>H)cb=H;
            if(cl<left)left=cl;
            if(ct<top)top=ct;
            if(cr>right)right=cr;
            if(cb>bottom)bottom=cb;
            old_left=cl;old_top=ct;old_right=cr;old_bottom=cb;
            if(!visible)cursor_pattern(canvas,px,py,fallback_arrow,21);
            else for(unsigned y=0;y<cursor->height;y++)for(unsigned x=0;x<cursor->width;x++){
            int sx=cursor->x-cursor->xhot+(int)x,sy=cursor->y-cursor->yhot+(int)y;
            if(sx<0||sx>=W||sy<0||sy>=H)continue;
            uint32_t color=(uint32_t)cursor->pixels[y*cursor->width+x];
            unsigned alpha=color>>24;if(!alpha)continue;uint32_t *p=(uint32_t*)canvas+sy*W+sx;
            unsigned r=((color>>16)&255)+(((*p>>16)&255)*(255-alpha)+127)/255;
            unsigned g=((color>>8)&255)+(((*p>>8)&255)*(255-alpha)+127)/255;
            unsigned b=(color&255)+((*p&255)*(255-alpha)+127)/255;*p=(r<<16)|(g<<8)|b;
            }
            if(busy)cursor_pattern(canvas,px+18,py+6,busy_hourglass,16);
            if(cursor)XFree(cursor);
        }
        converted=milliseconds();copy_ms+=converted-begin;
        left&=~1;top&=~1;right=(right+1)&~1;bottom=(bottom+1)&~1;
        vo_buffer *v=&buffers[chosen];
        if(!renderer_checked){renderer_init(canvas,nv12);renderer_checked=1;left=top=0;right=W;bottom=H;}
        if(right>left&&bottom>top){
            if(gpu_state&&gpu_convert(gpu_state,canvas,nv12,left,top,right,bottom)){
                gpu_fallback("CPU: GPU conversion failed");left=top=0;right=W;bottom=H;
            }
            if(!gpu_state&&ARGBToNV12(canvas+(top*W+left)*4,W*4,nv12+top*W+left,W,nv12+W*H+(top/2)*W+left,W,right-left,bottom-top))break;
        }
present_power_frame:
        v=&buffers[chosen];
        memcpy(v->data,nv12,W*H*3/2);
        convert_ms+=milliseconds()-converted;
        __sync_synchronize();uint32_t words[28]={0};words[0]=version();words[1]=4;words[2]=v->physical;words[3]=v->physical+W*H;
        words[4]=lock_address(chosen);words[5]=W;words[6]=H;words[7]=words[8]=W;
        words[21]=words[22]=words[23]=UINT32_MAX;words[26]=received_address(chosen);set_status(chosen,1);__sync_synchronize();
        if(command(39,words,sizeof words)){
            /* VOUT carries the complete, continuously updated desktop. A
               snapshot in fb0 would remain over it after windows close. */
            if(!frames){memset(fb,0,W*H*4);__sync_synchronize();}
            used[chosen]=1;frames++;
        }
        if(milliseconds()-measured>=5000){long elapsed=milliseconds()-measured;unsigned n=frames-measured_frames;
            printf("TIMING fps=%.1f copy_cursor_ms=%.2f convert_ms=%.2f frames=%u\n",n*1000.0/elapsed,n?copy_ms/(double)n:0,n?convert_ms/(double)n:0,frames);
            measured=milliseconds();measured_frames=frames;copy_ms=convert_ms=0;}
    }
#ifdef CLICKPAD_RAM_TRIAL
    if(trial_button){button(display,(unsigned)trial_button,0);XFlush(display);}
    close(trial_sock);
#endif
    if(!external_active&&(unsigned)vstop()==0x10000000u)for(int i=0;i<count;i++)release(&buffers[i]);
    if(gpu_state&&gpu_destroy)gpu_destroy(gpu_state);
    if(!external_active){deinit();}
    if(lib){dlclose(lib);}
    free(background);free(nv12);free(canvas);munmap(fb,W*H*4);munmap((void*)xmap,mapsize);
    XFixesShowCursor(display,DefaultRootWindow(display));XCloseDisplay(display);
    printf("DOVEOS_X11_BRIDGE_STOP frames=%u\n",frames);return 0;
}
