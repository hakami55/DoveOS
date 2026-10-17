/* SPDX-License-Identifier: GPL-2.0-or-later
 * Persistent verified DoveOS BLE keyboard input. HID usage/Linux key mappings
 * follow the standard boot-keyboard usages (Linux usbkbd reference).
 * Consumes only the selected keyboard's handle 0x001f eight-byte reports.
 * Does not log typed keys or open UHID. No raw key logging or UHID.
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
static volatile sig_atomic_t stopping;
static void stop(int sig){(void)sig;stopping=1;}
static long now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec;}
static const unsigned char map[256]={
 [4]=30,[5]=48,[6]=46,[7]=32,[8]=18,[9]=33,[10]=34,[11]=35,[12]=23,[13]=36,[14]=37,[15]=38,
 [16]=50,[17]=49,[18]=24,[19]=25,[20]=16,[21]=19,[22]=31,[23]=20,[24]=22,[25]=47,[26]=17,[27]=45,[28]=21,[29]=44,
 [30]=2,[31]=3,[32]=4,[33]=5,[34]=6,[35]=7,[36]=8,[37]=9,[38]=10,[39]=11,
 [40]=28,[41]=1,[42]=14,[43]=15,[44]=57,[45]=12,[46]=13,[47]=26,[48]=27,[49]=43,[50]=43,
 [51]=39,[52]=40,[53]=41,[54]=51,[55]=52,[56]=53,[57]=58,
 [58]=59,[59]=60,[60]=61,[61]=62,[62]=63,[63]=64,[64]=65,[65]=66,[66]=67,[67]=68,[68]=87,[69]=88,
 [70]=99,[71]=70,[72]=119,[73]=110,[74]=102,[75]=104,[76]=111,[77]=107,[78]=109,[79]=106,[80]=105,[81]=108,[82]=103,
 [83]=69,[84]=98,[85]=55,[86]=74,[87]=78,[88]=96,[89]=79,[90]=80,[91]=81,[92]=75,[93]=76,[94]=77,[95]=71,[96]=72,[97]=73,[98]=82,[99]=83,[100]=86,[101]=127,
 [224]=29,[225]=42,[226]=56,[227]=125,[228]=97,[229]=54,[230]=100,[231]=126
};
static void key(Display*d,unsigned code,int down){if(code)XTestFakeKeyEvent(d,code+8,down,CurrentTime);}
static int ismod(unsigned code){for(unsigned i=224;i<232;i++)if(map[i]==code)return 1;return 0;}
int main(void){
 setvbuf(stdout,NULL,_IOLBF,0);signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);
 Display*d=XOpenDisplay(NULL);if(!d){fputs("X11_OPEN_FAILED\n",stderr);return 2;}
 int e,b,major,minor;if(!XTestQueryExtension(d,&e,&b,&major,&minor)){XCloseDisplay(d);return 3;}
 unsigned char old[256]={0};char line[1024];size_t used=0;unsigned reports=0;int dropping=0;
 puts("BLE_KEYBOARD_INPUT_READY no_uhid=1 no_key_logging=1");
 while(!stopping){
  struct pollfd p={STDIN_FILENO,POLLIN,0};int rc=poll(&p,1,500);if(rc<0){if(errno==EINTR)continue;break;}if(!rc)continue;
  if(!(p.revents&POLLIN))break;
  char c;ssize_t n=read(STDIN_FILENO,&c,1);if(n!=1)break;
  if(c!='\n'){if(used+1<sizeof line)line[used++]=c;else dropping=1;continue;}
  line[used]=0;used=0;if(dropping){dropping=0;continue;}
  if(strstr(line,"Characteristic value was written successfully")){
   FILE *f=fopen("/tmp/doveos-bluetooth/keyboard-hid/ready","w");if(f){fputs("ready\n",f);fclose(f);}
   f=fopen("/tmp/doveos-bluetooth/keyboard-hid/state","w");if(f){fputs("Bluetooth keyboard connected. Desktop input ready.\n",f);fclose(f);}
   puts("Bluetooth keyboard connected. Desktop input ready.");continue;
  }
  const char*prefix="Notification handle = 0x001f value:";if(strncmp(line,prefix,strlen(prefix)))continue;
  const char*s=line+strlen(prefix);unsigned v[8];int taken=0;
  if(sscanf(s," %2x %2x %2x %2x %2x %2x %2x %2x %n",&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7],&taken)!=8||s[taken])continue;
  static long last;long at=now();if(at!=last){last=at;FILE *f=fopen("/tmp/doveos-power-wake","a");if(f)fclose(f);}
  if(access("/tmp/doveos-gpu-display-paused",F_OK)==0||access("/tmp/doveos-direct-display",F_OK)==0){
   for(unsigned i=1;i<256;i++)if(old[i])key(d,i,0);
   memset(old,0,sizeof old);XFlush(d);continue;
  }
  int rollover=0;for(int i=2;i<8;i++)if(v[i]>=1&&v[i]<=3)rollover=1;if(rollover)continue;
  unsigned char next[256]={0};for(int i=0;i<8;i++)if(v[0]&(1u<<i))next[map[224+i]]=1;
  for(int i=2;i<8;i++)if(map[v[i]])next[map[v[i]]]=1;
  for(unsigned i=1;i<256;i++)if(old[i]&&!next[i]&&!ismod(i))key(d,i,0);
  for(unsigned i=224;i<232;i++)if(old[map[i]]&&!next[map[i]])key(d,map[i],0);
  for(unsigned i=224;i<232;i++)if(!old[map[i]]&&next[map[i]])key(d,map[i],1);
  for(unsigned i=1;i<256;i++)if(!old[i]&&next[i]&&!ismod(i))key(d,i,1);
  memcpy(old,next,sizeof old);XFlush(d);reports++;if(reports==1)puts("KEYBOARD_INPUT_FORWARDED");
 }
 unlink("/tmp/doveos-bluetooth/keyboard-hid/ready");
 for(unsigned i=1;i<256;i++)if(old[i])key(d,i,0);
 XSync(d,False);XCloseDisplay(d);
 printf("BLE_KEYBOARD_INPUT_ENDED reports=%u released_all_keys=1\n",reports);return 0;
}
