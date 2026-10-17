#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/fb.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "third_party/directfb/directfb_keyboard.h"

/* ARM32 vendor ABI: every used offset and the 72-byte transfer were checked
   against libdivine's native send helpers. abi_check also checks live helpers
   against these offsets before touching a physical input device. */
typedef struct {
 uint32_t clazz,type,device_id,flags,time_sec,time_usec;
 int32_t key_code;uint32_t key_id,key_symbol,modifiers,locks,button,buttons,axis;
 int32_t axisabs,axisrel,min,max;
} native_event;
_Static_assert(sizeof(native_event)==72,"vendor event size");
_Static_assert(offsetof(native_event,flags)==12,"flags offset");
_Static_assert(offsetof(native_event,key_id)==28,"key ID offset");
_Static_assert(offsetof(native_event,key_symbol)==32,"key symbol offset");
_Static_assert(offsetof(native_event,button)==44,"button offset");
_Static_assert(offsetof(native_event,axis)==52,"axis offset");
_Static_assert(offsetof(native_event,axisabs)==56,"absolute offset");
static void *library,*divine;
static void *(*dopen)(const char *);
static void (*dclose)(void *),(*dsend)(void *,const native_event *);
static void (*didentifier)(void *,uint32_t),(*dmotion)(void *,int,int),(*dpress)(void *,uint32_t);
static volatile sig_atomic_t done;
static volatile int raw_ready;
static int button_pipe[2]={-1,-1};
static unsigned char held[KEY_MAX+1];
static unsigned mods;
static int hid_mouse;
static int carrier_width,carrier_height;
static unsigned long keys_sent,motion_sent,buttons_sent,touch_events,abs_events,sync_events;
static int pending_x,pending_y;
static void stop(int sig){(void)sig;done=1;}
static long ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000L+t.tv_nsec/1000000;}
static int abi_check(void){
 char path[80];snprintf(path,sizeof path,"/tmp/doveos-dex-abi-%ld",(long)getpid());
 int fd=open(path,O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC,0600);if(fd<0)return -1;
 void *p=dopen(path);if(!p){close(fd);unlink(path);return -1;}
 didentifier(p,DIKI_A);dmotion(p,123,456);dpress(p,2);dclose(p);
 native_event e[5];ssize_t count=read(fd,e,sizeof e);close(fd);unlink(path);
 return count==(ssize_t)sizeof e && e[0].type==1 && e[1].type==2 && e[0].flags==16 && e[0].key_id==DIKI_A &&
  e[2].type==5 && e[2].flags==2 && e[2].axis==0 && e[2].axisabs==123 &&
  e[3].axis==1 && e[3].axisabs==456 && e[4].type==3 && e[4].button==2 ? 0:-1;
}
static int load(void){
 library=dlopen("/usr/lib/libdivine-1.6.so.0.0.3",RTLD_NOW|RTLD_LOCAL);
 if(!library)return -1;
 dopen=dlsym(library,"divine_open");dclose=dlsym(library,"divine_close");dsend=dlsym(library,"divine_send_event");
 didentifier=dlsym(library,"divine_send_identifier");dmotion=dlsym(library,"divine_send_motion_absolute");dpress=dlsym(library,"divine_send_button_press");
 return dopen&&dclose&&dsend&&didentifier&&dmotion&&dpress?abi_check():-1;
}
static void axis(unsigned which,int value){
 native_event e={.clazz=1,.type=5,.flags=4,.axis=which,.axisrel=value};dsend(divine,&e);motion_sent++;
}
/* The stock WindowEvent sender carries absolute x/y. In HID mouse mode these
   are used only as bounded 8-bit relative-report bytes, followed by a zero
   report. Explicit bounds avoid DiVine's default 0..65535 normalization.
   Single-axis pulses avoid this vendor window manager's duplicate x/y reports
   and pointer acceleration. Android owns the visible full-screen pointer. */
static int hid_init(void){
 struct fb_var_screeninfo v={0};int fd=open("/dev/fb0",O_RDONLY|O_CLOEXEC);
 if(fd<0)return -1;
 int r=ioctl(fd,FBIOGET_VSCREENINFO,&v);close(fd);
 if(r||v.xres<256||v.yres<256)return -1;
 carrier_width=(int)v.xres;carrier_height=(int)v.yres;
 dmotion(divine,0,0);printf("DEX_HID_CARRIER %dx%d\n",carrier_width,carrier_height);return 0;
}
static void hid_motion(int x,int y){
 for(unsigned axis_id=0;axis_id<2;axis_id++){
  int value=axis_id?y:x;if(!value)continue;
  native_event e={.clazz=1,.type=5,.flags=0x3002,.axis=axis_id,
    .axisabs=(unsigned char)value,.min=0,.max=(axis_id?carrier_height:carrier_width)-1};
  dsend(divine,&e);e.axisabs=0;dsend(divine,&e);motion_sent++;
 }
}
static void motion(int x,int y){
 if(!hid_mouse){if(x)axis(0,x);if(y)axis(1,y);return;}
 pending_x+=x;pending_y+=y;
}
static void flush_motion(void){
 int dx=pending_x>127?127:pending_x<-127?-127:pending_x;
 int dy=pending_y>127?127:pending_y<-127?-127:pending_y;
 hid_motion(dx,dy);pending_x-=dx;pending_y-=dy;
}
static void button(unsigned which,int down){
 if(hid_mouse){if(which==2)which=1;}
 native_event e={.clazz=1,.type=down?3u:4u,.button=which};dsend(divine,&e);buttons_sent++;
}
static void click(unsigned which){button(which,1);button(which,0);}
static int map_key(unsigned code,unsigned *id,unsigned *symbol){
 static const unsigned letters[26]={KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z};
 for(unsigned i=0;i<26;i++)if(code==letters[i]){*id=DIKI_A+i;*symbol=(mods&1?'A':'a')+i;return 1;}
 if(code>=KEY_1&&code<=KEY_9){unsigned n=code-KEY_1+1;*id=DIKI_0+n;*symbol=mods&1?(unsigned)"!@#$%^&*("[n-1]:'0'+n;return 1;}
 if(code>=KEY_F1&&code<=KEY_F10){*id=DIKI_F1+code-KEY_F1;*symbol=DIKS_F1+code-KEY_F1;return 1;}
 struct mapping {unsigned code,id,symbol,shifted;};
 static const struct mapping map[]={
  {KEY_0,DIKI_0,'0',')'}, {KEY_SPACE,DIKI_SPACE,' ',' '}, {KEY_ENTER,DIKI_ENTER,DIKS_RETURN,DIKS_RETURN},
  {KEY_TAB,DIKI_TAB,DIKS_TAB,DIKS_TAB}, {KEY_ESC,DIKI_ESCAPE,DIKS_ESCAPE,DIKS_ESCAPE},
  {KEY_BACKSPACE,DIKI_BACKSPACE,DIKS_BACKSPACE,DIKS_BACKSPACE},
  {KEY_LEFTSHIFT,DIKI_SHIFT_L,DIKS_SHIFT,DIKS_SHIFT},{KEY_RIGHTSHIFT,DIKI_SHIFT_R,DIKS_SHIFT,DIKS_SHIFT},
  {KEY_LEFTCTRL,DIKI_CONTROL_L,DIKS_CONTROL,DIKS_CONTROL},{KEY_RIGHTCTRL,DIKI_CONTROL_R,DIKS_CONTROL,DIKS_CONTROL},
  {KEY_LEFTALT,DIKI_ALT_L,DIKS_ALT,DIKS_ALT},{KEY_RIGHTALT,DIKI_ALT_R,DIKS_ALTGR,DIKS_ALTGR},
  {KEY_LEFTMETA,DIKI_SUPER_L,DIKS_SUPER,DIKS_SUPER},{KEY_RIGHTMETA,DIKI_SUPER_R,DIKS_SUPER,DIKS_SUPER},
  {KEY_LEFT,DIKI_LEFT,DIKS_CURSOR_LEFT,DIKS_CURSOR_LEFT},{KEY_RIGHT,DIKI_RIGHT,DIKS_CURSOR_RIGHT,DIKS_CURSOR_RIGHT},
  {KEY_UP,DIKI_UP,DIKS_CURSOR_UP,DIKS_CURSOR_UP},{KEY_DOWN,DIKI_DOWN,DIKS_CURSOR_DOWN,DIKS_CURSOR_DOWN},
  {KEY_HOME,DIKI_HOME,DIKS_HOME,DIKS_HOME},{KEY_END,DIKI_END,DIKS_END,DIKS_END},
  {KEY_PAGEUP,DIKI_PAGE_UP,DIKS_PAGE_UP,DIKS_PAGE_UP},{KEY_PAGEDOWN,DIKI_PAGE_DOWN,DIKS_PAGE_DOWN,DIKS_PAGE_DOWN},
  {KEY_DELETE,DIKI_DELETE,DIKS_DELETE,DIKS_DELETE},{KEY_INSERT,DIKI_INSERT,DIKS_INSERT,DIKS_INSERT},
  {KEY_CAPSLOCK,DIKI_CAPS_LOCK,DIKS_CAPS_LOCK,DIKS_CAPS_LOCK},{KEY_NUMLOCK,DIKI_NUM_LOCK,DIKS_NUM_LOCK,DIKS_NUM_LOCK},
  {KEY_F11,DIKI_F11,DIKS_F11,DIKS_F11},{KEY_F12,DIKI_F12,DIKS_F12,DIKS_F12},
  {KEY_MINUS,DIKI_MINUS_SIGN,'-','_'},{KEY_EQUAL,DIKI_EQUALS_SIGN,'=','+'},
  {KEY_LEFTBRACE,DIKI_BRACKET_LEFT,'[','{'},{KEY_RIGHTBRACE,DIKI_BRACKET_RIGHT,']','}'},
  {KEY_SEMICOLON,DIKI_SEMICOLON,';',':'},{KEY_APOSTROPHE,DIKI_QUOTE_RIGHT,'\'', '"'},
  {KEY_GRAVE,DIKI_QUOTE_LEFT,'`','~'},{KEY_BACKSLASH,DIKI_BACKSLASH,'\\','|'},
  {KEY_COMMA,DIKI_COMMA,',','<'},{KEY_DOT,DIKI_PERIOD,'.','>'},{KEY_SLASH,DIKI_SLASH,'/','?'}
 };
 for(size_t i=0;i<sizeof map/sizeof map[0];i++)if(code==map[i].code){*id=map[i].id;*symbol=mods&1?map[i].shifted:map[i].symbol;return 1;}
 return 0;
}
static void update_mods(void){
 mods=(held[KEY_LEFTSHIFT]||held[KEY_RIGHTSHIFT]?1u:0u)|(held[KEY_LEFTCTRL]||held[KEY_RIGHTCTRL]?2u:0u)|
  (held[KEY_LEFTALT]?4u:0u)|(held[KEY_RIGHTALT]?8u:0u)|(held[KEY_LEFTMETA]||held[KEY_RIGHTMETA]?32u:0u);
}
static void key(unsigned code,int down){
 if(code>KEY_MAX)return;
 held[code]=down!=0;update_mods();unsigned id,symbol;
 if(!map_key(code,&id,&symbol))return;
 native_event e={.clazz=1,.type=down?1u:2u,.flags=0x78,.key_code=(int32_t)code,.key_id=id,.key_symbol=symbol,.modifiers=mods};
 dsend(divine,&e);keys_sent++;
}
static void *raw_buttons(void *unused){
 (void)unused;DIR *d=opendir("/sys/bus/hid/devices");if(!d)return NULL;
 char path[256]={0};struct dirent *e;
 while((e=readdir(d))){
  if(strncmp(e->d_name,"0003:03F0:0B51.",15)||strlen(e->d_name)!=19)continue;
  char p[256];snprintf(p,sizeof p,"/sys/bus/hid/devices/%.19s/report_descriptor",e->d_name);
  FILE *f=fopen(p,"rb");if(!f)continue;unsigned char b[4]={0};size_t n=fread(b,1,4,f);fclose(f);
  if(n==4&&b[0]==5&&b[1]==1&&b[2]==9&&b[3]==2){snprintf(path,sizeof path,"/sys/kernel/debug/hid/%.19s/events",e->d_name);break;}
 }
 closedir(d);if(!path[0])return NULL;FILE *f=fopen(path,"r");if(!f)return NULL;
 raw_ready=1;puts("DEX_PHYSICAL_BUTTONS_READY");char line[256];
 while(!done&&fgets(line,sizeof line,f)){unsigned report,buttons;
  if(sscanf(line,"report (size %*u) (numbered) = %x %x",&report,&buttons)==2&&report==1){unsigned char b=buttons&3;if(write(button_pipe[1],&b,1)<0&&errno!=EAGAIN)break;}
 }
 raw_ready=0;fclose(f);return NULL;
}
static int reverse_scroll(void){char s[20]={0};FILE *f=fopen("/media/usb0/DoveOS-v2/home/scroll-direction.txt","r");if(f){if(!fgets(s,sizeof s,f))s[0]=0;fclose(f);}return strncmp(s,"normal",6)!=0;}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IONBF,0);signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGPIPE,stop);
 alarm(5);if(load()){puts("DEX_INPUT_ABI_CHECK_FAILED");return 1;}alarm(0);
 if(argc==2&&!strcmp(argv[1],"--check")){puts("DEX_INPUT_VENDOR_ABI_VERIFIED");return 0;}
 alarm(5);divine=dopen("/tmp/divine");alarm(0);if(!divine)return 2;
 if(argc==2&&!strcmp(argv[1],"--test-relative")){axis(0,20);axis(1,10);dclose(divine);puts("DEX_RELATIVE_POINTER_SENT");return 0;}
 if(argc==2&&!strcmp(argv[1],"--test-hid-mouse")){hid_mouse=1;if(hid_init())return 5;motion(1,1);flush_motion();usleep(300000);motion(20,10);flush_motion();dclose(divine);puts("DEX_HID_MOUSE_SENT");return 0;}
 if(argc==2&&!strcmp(argv[1],"--test-hid-negative")){hid_mouse=1;if(hid_init())return 5;motion(-20,-10);flush_motion();dclose(divine);puts("DEX_HID_NEGATIVE_SENT");return 0;}
 if(argc==2&&!strcmp(argv[1],"--test-hid-pacing")){
  hid_mouse=1;if(hid_init())return 5;
  struct timespec deadline;clock_gettime(CLOCK_MONOTONIC,&deadline);
  for(int i=0;i<120;i++){
   struct timespec sent,ended;clock_gettime(CLOCK_MONOTONIC,&sent);
   motion(i<60?2:-2,i<60?2:-2);flush_motion();
   clock_gettime(CLOCK_MONOTONIC,&ended);
   printf("PROBE frame=%d mono=%ld.%09ld send_us=%ld\n",i,(long)sent.tv_sec,sent.tv_nsec,
     (long)(ended.tv_sec-sent.tv_sec)*1000000L+(ended.tv_nsec-sent.tv_nsec)/1000L);
   deadline.tv_nsec+=16666667;if(deadline.tv_nsec>=1000000000){deadline.tv_nsec-=1000000000;deadline.tv_sec++;}
   clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&deadline,NULL);
  }
  dclose(divine);puts("DEX_HID_PACING_120_FRAMES_SENT");return 0;
 }
 if(argc==2&&!strcmp(argv[1],"--test-modifier")){key(KEY_LEFTSHIFT,1);usleep(50000);key(KEY_LEFTSHIFT,0);dclose(divine);puts("DEX_SHIFT_PRESS_RELEASE_SENT");return 0;}
 if(argc==2&&!strcmp(argv[1],"--test-hid-keys")){
  const unsigned codes[]={KEY_LEFTCTRL,KEY_RIGHTCTRL,KEY_LEFTALT,KEY_RIGHTALT,
    KEY_LEFTSHIFT,KEY_RIGHTSHIFT,KEY_UP,KEY_DOWN,KEY_LEFT,KEY_RIGHT,KEY_PAGEUP,KEY_PAGEDOWN};
  for(size_t i=0;i<sizeof codes/sizeof codes[0];i++){
   key(codes[i],1);usleep(60000);key(codes[i],0);usleep(120000);
  }
  dclose(divine);puts("DEX_HID_MODIFIERS_ARROWS_PAGE_KEYS_SENT");return 0;
 }
 if(argc==2&&!strcmp(argv[1],"--hid-mouse"))hid_mouse=1;
 else if(argc!=1){dclose(divine);return 3;}
 if(hid_mouse&&hid_init()){dclose(divine);return 5;}
 int k=open("/dev/input/event1",O_RDONLY|O_NONBLOCK|O_CLOEXEC),t=open("/dev/input/event2",O_RDONLY|O_NONBLOCK|O_CLOEXEC);
 if(k<0||t<0||ioctl(k,EVIOCGRAB,1)||ioctl(t,EVIOCGRAB,1)){puts("DEX_INPUT_EXCLUSIVE_ACCESS_FAILED");if(k>=0)close(k);if(t>=0)close(t);dclose(divine);return 4;}
 if(pipe2(button_pipe,O_NONBLOCK|O_CLOEXEC)){button_pipe[0]=button_pipe[1]=-1;}
 pthread_t raw;int raw_started=button_pipe[0]>=0&&!pthread_create(&raw,NULL,raw_buttons,NULL);
 struct pollfd fds[3]={{k,POLLIN,0},{t,POLLIN,0},{button_pipe[0],POLLIN,0}};
 int touching=0,two=0,sawtwo=0,moving=0,physical=0,scroll=0,hx=0,hy=0,tx=0,ty=0,dx=0,dy=0,pdx=0,pdy=0,rx=0,ry=0,travel=0;
 unsigned buttons=0;long touched=0,next_log=ms()+10000,next_motion=ms();int reversed=reverse_scroll();
 /* A neutral HID report creates Android's keyboard before the first real key.
    No key-down event is injected. */
 key(KEY_LEFTSHIFT,0);usleep(100000);
 int ready=open("/tmp/doveos-dex-input.ready",O_WRONLY|O_CREAT|O_CLOEXEC,0600);if(ready>=0)close(ready);
 puts("DEX_INPUT_READY exclusive_keyboard=1 exclusive_touchpad=1");
 while(!done&&access("/tmp/doveos-dex.stop",F_OK)){
  long until=next_motion-ms();int wait=hid_mouse?(until>10?10:until>0?(int)until:0):10;
  int count=poll(fds,3,wait);if(count<0){if(errno==EINTR)continue;break;}
  for(int device=0;device<2;device++)if(fds[device].revents&POLLIN){struct input_event e;
   while(read(fds[device].fd,&e,sizeof e)==sizeof e){
    if(!device&&e.type==EV_KEY&&e.code<=KEY_MAX){
     if(e.value==2)continue;
     if(e.code==KEY_Q&&e.value==1&&(mods&7)==7){int s=open("/tmp/doveos-dex.stop",O_WRONLY|O_CREAT|O_CLOEXEC,0600);if(s>=0)close(s);done=1;break;}
     key(e.code,e.value!=0);
    }else if(device){
     if(e.type==EV_ABS)abs_events++;
     if(e.type==EV_SYN&&e.code==SYN_REPORT)sync_events++;
     if(e.type==EV_KEY&&e.code==BTN_TOUCH){
      touch_events++;
      if(e.value){touching=1;hx=hy=dx=dy=pdx=pdy=moving=sawtwo=scroll=travel=rx=ry=0;physical=buttons!=0;touched=ms();}
      else if(touching){if(!moving&&!physical&&!scroll&&travel<80&&ms()-touched<700)click(sawtwo?2:0);touching=0;hx=hy=dx=dy=0;}
     }else if(e.type==EV_KEY&&e.code==BTN_TOOL_DOUBLETAP){two=e.value!=0;if(two){sawtwo=1;dx=dy=0;}}
     else if(e.type==EV_ABS&&touching&&e.code==ABS_X){if(hx){dx+=e.value-tx;travel+=abs(e.value-tx);}tx=e.value;hx=1;}
     else if(e.type==EV_ABS&&touching&&e.code==ABS_Y){if(hy){dy+=e.value-ty;travel+=abs(e.value-ty);}ty=e.value;hy=1;}
     else if(!raw_ready&&e.type==EV_KEY&&(e.code==BTN_LEFT||e.code==BTN_RIGHT)){button(e.code==BTN_LEFT?0:2,e.value!=0);if(e.value)physical=1;}
     else if(e.type==EV_SYN&&e.code==SYN_REPORT){
      if(two){pdy+=dy;while(abs(pdy)>=30){int up=(pdy>0?1:-1)*(reversed?1:-1);
       if(hid_mouse){unsigned c=up>0?KEY_PAGEUP:KEY_PAGEDOWN;key(c,1);key(c,0);}else axis(2,up);
       pdy+=pdy>0?-30:30;scroll=1;}}
      else if(!sawtwo){if(!moving){pdx+=dx;pdy+=dy;int dist=abs(pdx)+abs(pdy);if(dist>=28){moving=1;dx=pdx*(dist-28)/dist;dy=pdy*(dist-28)/dist;}else dx=dy=0;}
       if(moving){rx+=dx*60;ry+=dy*60;int mx=rx/100,my=ry/100;rx-=mx*100;ry-=my*100;motion(mx,my);}}
      dx=dy=0;
     }
    }
   }
  }
  if(fds[2].revents&POLLIN){unsigned char state;while(read(fds[2].fd,&state,1)==1){unsigned changed=buttons^state;if(changed&1)button(0,(state&1)!=0);if(changed&2)button(2,(state&2)!=0);if(state)physical=1;buttons=state;}}
  /* Coalesce the pad's high-rate samples into a bounded 60 Hz HID stream.
     The FIFO receives at most four pulse/reset reports per frame. */
  if(hid_mouse&&ms()>=next_motion){flush_motion();next_motion+=16;if(next_motion<=ms())next_motion=ms()+16;}
  if(ms()>=next_log){printf("DEX_INPUT_COUNTS keys=%lu motion=%lu buttons=%lu touch=%lu abs=%lu syn=%lu state=%d two=%d sawtwo=%d travel=%d\n",keys_sent,motion_sent,buttons_sent,touch_events,abs_events,sync_events,touching,two,sawtwo,travel);next_log=ms()+10000;}
 }
 for(unsigned i=0;i<=KEY_MAX;i++)if(held[i])key(i,0);
 if(buttons&1)button(0,0);
 if(buttons&2)button(2,0);
 ioctl(k,EVIOCGRAB,0);ioctl(t,EVIOCGRAB,0);close(k);close(t);
 if(raw_started){pthread_cancel(raw);pthread_join(raw,NULL);}
 if(button_pipe[0]>=0)close(button_pipe[0]);
 if(button_pipe[1]>=0)close(button_pipe[1]);
 dclose(divine);dlclose(library);unlink("/tmp/doveos-dex-input.ready");puts("DEX_INPUT_RELEASED");return 0;
}
