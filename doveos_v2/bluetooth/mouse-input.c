/* SPDX-License-Identifier: GPL-2.0-or-later
 * Persistent X11 input for the verified BT5.1 Mouse report layout.
 * Report layout comes from its captured 92-byte HID report descriptor.
 * No UHID, persistent startup hook, packet log or device-wide reset.
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
static volatile sig_atomic_t stopping;
static void stop(int sig){(void)sig;stopping=1;}
static long now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec;}
static void activity(void){static long last;long t=now();if(t==last)return;last=t;int f=open("/tmp/doveos-power-wake",O_WRONLY|O_CREAT|O_CLOEXEC,0600);if(f>=0)close(f);}
static const unsigned buttons[8]={1,3,2,8,9,10,11,12};
static void release(Display*d,unsigned *state){for(unsigned i=0;i<8;i++)if(*state&(1u<<i))XTestFakeButtonEvent(d,buttons[i],False,0);*state=0;XFlush(d);}
static void wheel(Display*d,int value,unsigned positive,unsigned negative){unsigned b=value>0?positive:negative;unsigned n=value<0?(unsigned)-value:(unsigned)value;for(unsigned i=0;i<n;i++){XTestFakeButtonEvent(d,b,True,0);XTestFakeButtonEvent(d,b,False,0);}}
int main(void){
 setvbuf(stdout,NULL,_IOLBF,0);signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);
 Display*d=XOpenDisplay(NULL);if(!d){fputs("X11_OPEN_FAILED\n",stderr);return 2;}
 int e,b,major,minor;if(!XTestQueryExtension(d,&e,&b,&major,&minor)){XCloseDisplay(d);return 3;}
 unsigned held=0,reports=0,moves=0,clicks=0,scrolls=0;char line[4096],last_link[64]={0};size_t used=0;int dropping=0;
 puts("BLE_MOUSE_DESKTOP_READY no_uhid=1 no_raw_report_logging=1");
 while(!stopping){
  struct pollfd p={STDIN_FILENO,POLLIN,0};int rc=poll(&p,1,500);if(rc<0){if(errno==EINTR)continue;break;}if(!rc)continue;if(!(p.revents&POLLIN))break;
  char c;if(read(STDIN_FILENO,&c,1)!=1)break;
  if(c!='\n'){if(!dropping){if(used+1<sizeof line)line[used++]=c;else dropping=1;}continue;}
  line[used]=0;used=0;if(dropping){dropping=0;continue;}
  if(strstr(line,"Characteristic value was written successfully")){
   FILE *f=fopen("/tmp/doveos-bluetooth/hid/ready","w");if(f){fputs("ready\n",f);fclose(f);}
   f=fopen("/tmp/doveos-bluetooth/hid/state","w");if(f){fputs("Bluetooth mouse connected. Desktop input ready.\n",f);fclose(f);}
   puts("Bluetooth mouse connected. Desktop input ready.");continue;
  }
  if(!strncmp(line,"LINK ",5)){if(strlen(line)<sizeof last_link&&strcmp(line,last_link)){strcpy(last_link,line);puts(line);}continue;}
  if(!strncmp(line,"StartNotify ",12)){puts(line);continue;}
  if(!strncmp(line,"DEVICE_CHANGE ",14)&&strstr(line,"'Connected': <false>")){release(d,&held);puts("MOUSE_DISCONNECTED_BUTTONS_RELEASED");continue;}
  unsigned serial,n,v[6];char path[256];int taken=0;
  if(!strncmp(line,"Notification handle = 0x0022 value:",35)){
   if(sscanf(line,"Notification handle = 0x0022 value: %2x %2x %2x %2x %2x %2x %n",&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&taken)!=6||line[taken])continue;
  }else{
   if(sscanf(line,"REPORT %u %255s %u %2x %2x %2x %2x %2x %2x %n",&serial,path,&n,&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&taken)!=9||n!=6||line[taken])continue;
   if(strcmp(path,"/org/bluez/hci0/dev_E4_03_48_F0_01_33/service001e/char0021"))continue;
  }
  if(access("/tmp/doveos-gpu-display-paused",F_OK)==0||access("/tmp/doveos-direct-display",F_OK)==0){release(d,&held);activity();continue;}
  int dx=(int)(v[1]|((v[2]&15)<<8)),dy=(int)((v[2]>>4)|(v[3]<<4));if(dx&0x800)dx-=4096;if(dy&0x800)dy-=4096;
  if(dx < -2047||dy < -2047)continue;
  int vertical=(v[4]&128)?(int)v[4]-256:(int)v[4];int horizontal=(v[5]&128)?(int)v[5]-256:(int)v[5];
  if(dx||dy){XTestFakeRelativeMotionEvent(d,dx,dy,0);moves++;}
  for(unsigned i=0;i<8;i++)if((held^v[0])&(1u<<i)){XTestFakeButtonEvent(d,buttons[i],!!(v[0]&(1u<<i)),0);clicks++;}
  held=v[0];wheel(d,vertical,4,5);wheel(d,horizontal,7,6);if(vertical||horizontal)scrolls++;
  XFlush(d);activity();reports++;if(reports==1)puts("MOUSE_INPUT_FORWARDED");
 }
 unlink("/tmp/doveos-bluetooth/hid/ready");release(d,&held);XSync(d,False);XCloseDisplay(d);printf("BLE_MOUSE_INPUT_ENDED reports=%u motion_frames=%u button_changes=%u scroll_frames=%u released_all_buttons=1\n",reports,moves,clicks,scrolls);return 0;
}
