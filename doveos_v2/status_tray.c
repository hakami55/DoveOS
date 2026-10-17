#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <dirent.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "tray_icons.h"
#include "clock_settings.h"
#include "battery_state.h"

#define W 174
#define H 24
static Display *display;
static Window window,tooltip;
static GC gc;
static XftFont *text_font;
static XftDraw *text_draw,*hint_draw;
static XftColor text_color;
static unsigned long dark,white;
static Pixmap images[18];
static const unsigned char *const pixels[]={tray_wifi_zero,tray_wifi_low,tray_wifi_high,tray_wifi,tray_wifi_off,tray_cable,tray_volume_x,tray_volume_1,tray_volume_2,tray_bluetooth,tray_bluetooth_off,tray_battery_low,tray_battery_medium,tray_battery_full,tray_battery_warning,tray_plug,tray_volume_3,tray_battery_three_quarters};
static char hints[6][160];
static char clock_text[32];
static int separator;
static int dock_icons;
static int icon_left=25,clock_left=117,language_left=3;
static int status_slot(int x){if(x>=clock_left-4)return 4;if(x>=language_left&&x<icon_left)return 5;if(x<icon_left)return -1;int slot=(x-icon_left)/22;return slot<4?slot:-1;}
static int dock_icon_count(void){Atom type;int format;unsigned long n,left;unsigned char *value=NULL;int count=0;if(XGetWindowProperty(display,DefaultRootWindow(display),XInternAtom(display,"_DOVEOS_TRAY_ICON_COUNT",False),0,1,False,XA_CARDINAL,&type,&format,&n,&left,&value)==Success&&format==32&&n==1&&value)count=(int)*(unsigned long*)value;if(value)XFree(value);return count;}
static int states[4]={4,6,10,14},hover=-1;
static void read_line(const char *path,char *out,size_t cap){out[0]=0;FILE *f=fopen(path,"r");if(!f)return;if(fgets(out,(int)cap,f))out[strcspn(out,"\r\n")]=0;fclose(f);}
#include "cpu_mode_watermark.h"
static unsigned long color(const char *name){XColor got,exact;if(!XAllocNamedColor(display,DefaultColormap(display,DefaultScreen(display)),name,&got,&exact))return 0;return got.pixel;}
#include "language_tray.h"
#include "wifi_tray.h"
static int link_up(const char *name){char path[180],value[32];snprintf(path,sizeof path,"/sys/class/net/%s/flags",name);read_line(path,value,sizeof value);if(!(strtoul(value,NULL,16)&1))return 0;snprintf(path,sizeof path,"/sys/class/net/%s/carrier",name);read_line(path,value,sizeof value);return !strcmp(value,"1");}
static int ethernet_visible;
static void network_wifi(void){
 wifi_visible=access("/sys/class/net/wlan0",F_OK)==0;wifi_percent=-1;wifi_bars=0;wifi_source="unavailable";
 if(!wifi_visible){states[0]=4;snprintf(hints[0],sizeof hints[0],"Wi-Fi unavailable");return;}
 char flags[32];read_line("/sys/class/net/wlan0/flags",flags,sizeof flags);
 wifi_mode=access("/tmp/doveos-wifi/manual-off",F_OK)==0||!(strtoul(flags,NULL,16)&1)||wifi_blocked()?0:link_up("wlan0")?2:1;
 if(wifi_mode!=2){states[0]=wifi_mode?0:4;snprintf(hints[0],sizeof hints[0],"Wi-Fi %s",wifi_mode?"on - not connected":"off");return;}
 wifi_percent=wifi_quality_percent();
 wifi_bars=wifi_percent<0||wifi_percent==0?0:wifi_percent<25?1:wifi_percent<50?2:wifi_percent<75?3:4;
 char info[100];read_line("/tmp/doveos-wifi/link-status",info,sizeof info);char *at=strstr(info," at ");int freq=at?atoi(at+4):0;
 states[0]=wifi_bars>0?wifi_bars-1:0;
 if(wifi_percent>=0)snprintf(hints[0],sizeof hints[0],"Wi-Fi %s - %d%% %s",freq>=4900?"5 GHz":freq>0?"2.4 GHz":"connected",wifi_percent,wifi_source);
 else snprintf(hints[0],sizeof hints[0],"Wi-Fi connected - signal unavailable");
}
static void network(void){
 char rt_network[128];read_line("/tmp/doveos-rtl8153/network",rt_network,sizeof rt_network);
 char wired[64]="";if(!strncmp(rt_network,"rtdebug0 connected ",19))snprintf(wired,sizeof wired,"rtdebug0");DIR *dir=opendir("/sys/class/net");struct dirent *entry;
 if(dir){while(!wired[0]&&(entry=readdir(dir))){const char *n=entry->d_name;if(!strncmp(n,"eth",3)||!strncmp(n,"en",2)||!strncmp(n,"axdebug",7)){if(link_up(n)){snprintf(wired,sizeof wired,"%.63s",n);break;}}}closedir(dir);}
 network_wifi();ethernet_visible=wired[0]!=0;
 if(ethernet_visible){
  char wifi_hint[160];snprintf(wifi_hint,sizeof wifi_hint,"%s",hints[0]);
  states[0]=5;
  snprintf(hints[0],sizeof hints[0],"Ethernet connected (%.63s); %.65s",wired,wifi_hint);
 }
}
static void sound(void){char value[24];read_line("/media/usb0/DrMoSa/sound-volume.txt",value,sizeof value);if(!value[0]){states[1]=6;snprintf(hints[1],sizeof hints[1],"Speaker volume unavailable");return;}int v=atoi(value);if(v<0)v=0;if(v>100)v=100;states[1]=v==0?6:v<=33?7:v<=66?8:16;if(v)snprintf(hints[1],sizeof hints[1],"Speaker volume: %d%%",v);else snprintf(hints[1],sizeof hints[1],"Speakers muted");}
static void bluetooth(void){
 /* Read-only Linux HCIGETDEVINFO ABI: dev_id (2), name (8), bdaddr (6),
  * then flags. The buffer holds the entire kernel structure. No scan/power. */
 int up=-1,fd=socket(31,SOCK_RAW,1);unsigned char info[256]={0};uint32_t flags=0;
 if(fd>=0){if(ioctl(fd,_IOR('H',211,int),info)==0){memcpy(&flags,info+16,sizeof flags);up=(flags&1)!=0;}close(fd);}
 states[2]=up==1?9:10;snprintf(hints[2],sizeof hints[2],"%s",up==1?"Bluetooth enabled":up==0?"Bluetooth disabled":"Bluetooth status unavailable");
}
static void power(void){
 DIR *dir=opendir("/sys/class/power_supply");struct dirent *entry;int pct=-1,external=-1;char status[40]="";
 if(dir){while((entry=readdir(dir))){if(entry->d_name[0]=='.')continue;char path[384],type[32],value[32];snprintf(path,sizeof path,"/sys/class/power_supply/%s/type",entry->d_name);read_line(path,type,sizeof type);
 if(!strcmp(type,"Battery")){snprintf(path,sizeof path,"/sys/class/power_supply/%s/capacity",entry->d_name);read_line(path,value,sizeof value);if(value[0])pct=atoi(value);snprintf(path,sizeof path,"/sys/class/power_supply/%s/status",entry->d_name);read_line(path,status,sizeof status);}
 else {snprintf(path,sizeof path,"/sys/class/power_supply/%s/online",entry->d_name);read_line(path,value,sizeof value);if(value[0]){if(atoi(value))external=1;else if(external<0)external=0;}}
 }closedir(dir);}
 if(pct<0){DoveBattery b;if(dove_battery_read(&b)){pct=(int)b.percent;external=b.external;snprintf(status,sizeof status,"%s",b.status);}}
 if(pct<0||pct>100)pct=-1;
 if(!strcmp(status,"Charging")||!strcmp(status,"Not charging"))external=1;else if(!strcmp(status,"Discharging"))external=0;
 if(external==1){states[3]=15;if(pct>=0)snprintf(hints[3],sizeof hints[3],"External power - %d%% (%s)",pct,status[0]?status:"charge status unknown");else snprintf(hints[3],sizeof hints[3],"External power connected");}
 else if(external==0){states[3]=pct<0?14:pct<=10?14:pct<=30?11:pct<=60?12:pct<=85?17:13;if(pct>=0)snprintf(hints[3],sizeof hints[3],"On battery: %d%%",pct);else snprintf(hints[3],sizeof hints[3],"On battery - percentage unavailable");}
 else {states[3]=14;snprintf(hints[3],sizeof hints[3],"Battery / charger status unavailable");}
}
static void render(void){if(separator){XSetForeground(display,gc,color("#808080"));XDrawLine(display,window,gc,6,3,6,H-4);XSetForeground(display,gc,color("#FFFFFF"));XDrawLine(display,window,gc,7,3,7,H-4);XFlush(display);return;}XWindowAttributes geometry;XGetWindowAttributes(display,window,&geometry);int offset=(geometry.height-H)/2;if(offset<0)offset=0;XSetForeground(display,gc,dark);XFillRectangle(display,window,gc,0,0,W,(unsigned)geometry.height);XGlyphInfo clock_extent;XftTextExtentsUtf8(display,text_font,(const FcChar8*)clock_text,(int)strlen(clock_text),&clock_extent);int clock_x=W-5-clock_extent.xOff;if(clock_x<115)clock_x=115;clock_left=clock_x;/* Keep the language badge 3 px from the frame, with even icon spacing. */language_render();for(int i=0;i<4;i++){if(i==0&&wifi_visible&&!ethernet_visible)wifi_paint(icon_left,3+offset);else XCopyArea(display,images[states[i]],window,gc,0,0,18,18,icon_left+i*22,3+offset);}XSetForeground(display,gc,white);XftDrawStringUtf8(text_draw,&text_color,text_font,clock_x,17+offset,(const FcChar8*)clock_text,(int)strlen(clock_text));XSetForeground(display,gc,color("#808080"));XDrawLine(display,window,gc,0,offset,W-1,offset);XSetForeground(display,gc,color("#FFFFFF"));XDrawLine(display,window,gc,0,H-1+offset,W-1,H-1+offset);XDrawLine(display,window,gc,W-1,offset,W-1,H-1+offset);XFlush(display);}
static void show_hint(int slot){
 if(slot<0||slot>5){XUnmapWindow(display,tooltip);return;}
 Window child;int rx,ry;XTranslateCoordinates(display,slot==5?language_window:window,DefaultRootWindow(display),0,0,&rx,&ry,&child);int len=(int)strlen(hints[slot]);XGlyphInfo extent;XftTextExtentsUtf8(display,text_font,(const FcChar8*)hints[slot],len,&extent);int tw=extent.xOff+12,th=text_font->ascent+text_font->descent+8;
 if(rx+tw>DisplayWidth(display,DefaultScreen(display)))rx=DisplayWidth(display,DefaultScreen(display))-tw-4;
 if(rx<0)rx=0;
 XMoveResizeWindow(display,tooltip,rx,ry-th-5,(unsigned)tw,(unsigned)th);XMapRaised(display,tooltip);XSetForeground(display,gc,dark);XFillRectangle(display,tooltip,gc,0,0,(unsigned)tw,(unsigned)th);XSetForeground(display,gc,white);XftDrawStringUtf8(hint_draw,&text_color,text_font,6,4+text_font->ascent,(const FcChar8*)hints[slot],len);
}
static void open_settings(int slot){const char *page=slot==0?"network":slot==1?"sound":slot==2?"bluetooth":slot==3?"battery":"datetime";const char *number=slot==0?"0":slot==1?"3":slot==2?"1":slot==3?"7":"5";pid_t child=fork();if(child==0){execl("/tmp/doveos-theme/doveos-open","doveos-open","DoveSettings","--page",number,"/usr/bin/doveos-settings-ui",page,(char*)NULL);_exit(127);}}
int main(int argc,char **argv){
 if(argc==3&&!strcmp(argv[1],"--select-layout")){if(strcmp(argv[2],"0")&&strcmp(argv[2],"1"))return 2;Display *d=XOpenDisplay(NULL);if(!d)return 1;Bool ok=XkbLockGroup(d,XkbUseCoreKbd,(unsigned)atoi(argv[2]));XSync(d,False);XCloseDisplay(d);return ok?0:1;}

 if(argc==2 && !strcmp(argv[1],"--power-status")){power();printf("POWER_ICON=%s\n%s\n",states[3]==15?"plug":states[3]==14?"unknown":"battery",hints[3]);return 0;}
 if(argc==2 && !strcmp(argv[1],"--network-status")){network();printf("NETWORK_ICON=%s\nWIFI_STATE=%s\nWIFI_PERCENT=%d\nWIFI_BARS=%d\n%s\n",ethernet_visible?"ethernet":wifi_visible?"wifi":"unavailable",!wifi_visible?"unavailable":wifi_mode==0?"off":wifi_mode==1?"not-connected":"connected",wifi_percent,wifi_bars,hints[0]);return 0;}
 separator=argc>1;
 signal(SIGCHLD,SIG_IGN);display=XOpenDisplay(NULL);if(!display)return 1;int screen=DefaultScreen(display);dark=color("#C0C0C0");white=color("#000000");
 window=XCreateSimpleWindow(display,RootWindow(display,screen),0,0,separator?14:W,H,0,dark,dark);XStoreName(display,window,"DoveOS status");XClassHint cls={separator?argv[1]:"doveos-status","DoveOS status"};XSetClassHint(display,window,&cls);XSelectInput(display,window,ExposureMask|ButtonPressMask|StructureNotifyMask|PointerMotionMask|LeaveWindowMask);gc=XCreateGC(display,window,0,NULL);
 XSetWindowAttributes attrs={0};attrs.override_redirect=True;attrs.background_pixel=dark;tooltip=XCreateWindow(display,RootWindow(display,screen),0,0,200,25,0,CopyFromParent,InputOutput,CopyFromParent,CWOverrideRedirect|CWBackPixel,&attrs);
 text_font=XftFontOpenName(display,screen,"Microsoft Sans Serif-8:antialias=false");if(!text_font)return 2;
 XftColorAllocName(display,DefaultVisual(display,screen),DefaultColormap(display,screen),"#000000",&text_color);
 text_draw=XftDrawCreate(display,window,DefaultVisual(display,screen),DefaultColormap(display,screen));hint_draw=XftDrawCreate(display,tooltip,DefaultVisual(display,screen),DefaultColormap(display,screen));
 if(separator){XMapWindow(display,window);for(;;){XEvent e;XNextEvent(display,&e);if(e.type==Expose)render();if(e.type==DestroyNotify)return 0;}}
 for(unsigned i=0;i<sizeof images/sizeof images[0];i++){images[i]=XCreatePixmap(display,window,18,18,(unsigned)DefaultDepth(display,screen));XImage *im=XCreateImage(display,DefaultVisual(display,screen),(unsigned)DefaultDepth(display,screen),ZPixmap,0,calloc(18*18,4),18,18,32,0);if(!im)return 2;for(int y=0;y<18;y++)for(int x=0;x<18;x++){const unsigned char *p=pixels[i]+(y*18+x)*3;XPutPixel(im,x,y,((unsigned long)p[0]<<16)|((unsigned long)p[1]<<8)|p[2]);}XPutImage(display,images[i],gc,im,0,0,0,0,18,18);XDestroyImage(im);}
 language_init();cpu_watermark_init();XMapWindow(display,window);time_t last=0;int done=0;
 while(!done){int redraw=0;while(XPending(display)){XEvent e;XNextEvent(display,&e);if(e.xany.window==cpu_watermark){if(e.type==Expose)cpu_watermark_paint();continue;}if(language_handle(&e))continue;if(e.type==DestroyNotify){done=1;break;}if(e.type==Expose)redraw=1;if(e.type==ButtonPress){XUnmapWindow(display,tooltip);int slot=status_slot(e.xbutton.x);if(slot==5){if(language_menu_open)language_close_menu();else language_open_menu();}else if(slot>=0)open_settings(slot);}if(e.type==MotionNotify){int next=status_slot(e.xmotion.x);if(next!=hover){hover=next;show_hint(hover);}}if(e.type==LeaveNotify){hover=-1;XUnmapWindow(display,tooltip);}}
 time_t now=time(NULL);if(now!=last){cpu_watermark_update();int count=dock_icon_count();if(count!=dock_icons){dock_icons=count;redraw=1;}int old_wifi=wifi_visible*100+wifi_mode*10+wifi_bars;int old[4];memcpy(old,states,sizeof old);network();sound();bluetooth();power();clock_zone();struct tm local;localtime_r(&now,&local);char previous[32];strcpy(previous,clock_text);if(now<1577836800)strcpy(clock_text,"--:--");else strftime(clock_text,sizeof clock_text,clock_24h()?"%H:%M":"%I:%M %p",&local);char date_text[80];strftime(date_text,sizeof date_text,"%A, %d %B %Y",&local);snprintf(hints[4],sizeof hints[4],"%s - %s - click to set",date_text,clock_zone_label());if(strcmp(previous,clock_text))redraw=1;if(memcmp(old,states,sizeof old)||old_wifi!=wifi_visible*100+wifi_mode*10+wifi_bars||last==0)redraw=1;last=now;}
 if(redraw)render();
 if(language_refresh(0))language_render();
 if(hover>=0&&!language_menu_open)show_hint(hover);
 struct timespec pause={0,100000000};nanosleep(&pause,NULL);}
 cpu_watermark_destroy();language_close_menu();XDestroyWindow(display,language_menu);XDestroyWindow(display,tooltip);for(unsigned i=0;i<sizeof images/sizeof images[0];i++)XFreePixmap(display,images[i]);XFreeGC(display,gc);XCloseDisplay(display);return 0;
}
