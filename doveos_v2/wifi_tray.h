/* Read-only station status. Quality units come from this driver's range ioctl. */
#include <linux/wireless.h>
static int wifi_visible,wifi_mode,wifi_percent=-1,wifi_bars;
static const char *wifi_source="unavailable";

static int wifi_quality_percent(void){
 int fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0)return -1;
 struct iwreq request={0};struct iw_statistics stats={0};struct iw_range range={0};
 strcpy(request.ifr_name,"wlan0");request.u.data.pointer=&stats;request.u.data.length=sizeof stats;
 int ok=ioctl(fd,SIOCGIWSTATS,&request)==0;
 memset(&request,0,sizeof request);strcpy(request.ifr_name,"wlan0");request.u.data.pointer=&range;request.u.data.length=sizeof range;
 int ranged=ioctl(fd,SIOCGIWRANGE,&request)==0;close(fd);
 if(!ok)return -1;
 if(!(stats.qual.updated&IW_QUAL_QUAL_INVALID)&&ranged&&range.max_qual.qual&&stats.qual.qual<=range.max_qual.qual){
  wifi_source="driver link quality";
  return (stats.qual.qual*100+range.max_qual.qual/2)/range.max_qual.qual;
 }
 if(!(stats.qual.updated&IW_QUAL_LEVEL_INVALID)&&(stats.qual.updated&IW_QUAL_DBM)){
  int dbm=(int)stats.qual.level-256;
  if(dbm>=-100&&dbm<=-20){wifi_source="RSSI estimate";return dbm>=-50?100:(dbm+100)*2;}
 }
 return -1;
}
static int wifi_blocked(void){
 DIR *dir=opendir("/sys/class/rfkill");if(!dir)return 0;
 struct dirent *entry;int blocked=0;
 while((entry=readdir(dir))){if(entry->d_name[0]=='.')continue;
  char path[384],type[32],value[16];snprintf(path,sizeof path,"/sys/class/rfkill/%s/type",entry->d_name);read_line(path,type,sizeof type);
  if(strcmp(type,"wlan"))continue;
  const char *fields[]={"soft","hard"};
  for(int i=0;i<2;i++){snprintf(path,sizeof path,"/sys/class/rfkill/%s/%s",entry->d_name,fields[i]);read_line(path,value,sizeof value);if(!strcmp(value,"1"))blocked=1;}
 }
 closedir(dir);return blocked;
}
static void wifi_paint(int x,int y){
 unsigned long inactive=color("#808080"),outline=color("#404040"),active=color("#008000");
 for(int bar=0;bar<4;bar++){
  int height=4+bar*3,bx=x+1+bar*4,by=y+17-height;
  XSetForeground(display,gc,outline);XFillRectangle(display,window,gc,bx,by,3,(unsigned)height);
  XSetForeground(display,gc,wifi_mode==2&&bar<wifi_bars?active:inactive);
  XFillRectangle(display,window,gc,bx,by,2,(unsigned)(height-1));
 }
 if(wifi_mode==0){
  XSetForeground(display,gc,dark);XFillRectangle(display,window,gc,x,y,9,9);
  XSetForeground(display,gc,color("#C00000"));
  for(int n=0;n<2;n++){XDrawLine(display,window,gc,x+1,y+1+n,x+6,y+6+n);XDrawLine(display,window,gc,x+6,y+1+n,x+1,y+6+n);}
 }else if(wifi_mode==1){
  XSetForeground(display,gc,color("#805000"));XFillRectangle(display,window,gc,x+2,y+1,2,4);XFillRectangle(display,window,gc,x+2,y+6,2,1);
 }else if(wifi_percent<0){
  XSetForeground(display,gc,color("#000080"));XDrawLine(display,window,gc,x+1,y+1,x+5,y+1);XDrawLine(display,window,gc,x+5,y+1,x+5,y+3);XDrawLine(display,window,gc,x+3,y+3,x+5,y+3);XDrawPoint(display,window,gc,x+3,y+4);XDrawPoint(display,window,gc,x+3,y+6);
 }
}
