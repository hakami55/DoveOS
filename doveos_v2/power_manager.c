#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "power_settings.h"
#include "battery_state.h"
static volatile sig_atomic_t stopping;
static void stop(int sig){(void)sig;stopping=1;}
static double mono(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static int display(int mode){
 if(!mode){unlink("/tmp/doveos-screen-blank");unlink("/tmp/doveos-screen-dim");return 0;}
 const char *path=mode==2?"/tmp/doveos-screen-blank":"/tmp/doveos-screen-dim";
 char temp[90];snprintf(temp,sizeof temp,"%s.tmp",path);
 FILE *f=fopen(temp,"w");if(!f)return -1;
 /* A stale request expires even if the power manager crashes. */
 fprintf(f,"%.3f\n",mono()+2);if(fclose(f)||rename(temp,path))return -1;
 /* Publish the new request before removing the previous one. In particular,
    dim -> off must not briefly expose the normal brightness. */
 unlink(mode==2?"/tmp/doveos-screen-dim":"/tmp/doveos-screen-blank");return 0;
}
static void state(const char *message){
 static char previous[160];
 if(!strcmp(previous,message)&&!access("/tmp/doveos-power-status",F_OK))return;
 FILE *f=fopen("/tmp/doveos-power-status.tmp","w");if(!f)return;
 fprintf(f,"%s\n",message);if(fclose(f)||rename("/tmp/doveos-power-status.tmp","/tmp/doveos-power-status"))return;
 snprintf(previous,sizeof previous,"%s",message);
}
/* Settings are atomically renamed by the UI. Check their identity once a
   second; do not reopen the USB file on every input poll. */
static DovePower settings(void){
 static DovePower cached;static struct stat previous;static int loaded,present;
 struct stat current;int exists=!stat(DOVE_POWER_FILE,&current);
 if(!loaded||exists!=present||(exists&&(current.st_dev!=previous.st_dev||current.st_ino!=previous.st_ino||current.st_size!=previous.st_size||current.st_mtime!=previous.st_mtime||current.st_ctime!=previous.st_ctime))){
  cached=dove_power_load();loaded=1;present=exists;if(exists)previous=current;
 }
 return cached;
}
/* Use the driver's existing demand policy only while the panel is blank.
   Never alter frequency limits or CPU hotplug. Restore the policy we found
   at startup before presenting the desktop or active media again. */
static void idle_cpu(int blank){
 static int initialized,available,changed;static char original[32];
 const char *path="/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor";
 if(!initialized){
  initialized=1;FILE *f=fopen(path,"r");
  if(f){if(fscanf(f,"%31s",original)==1&&!strcmp(original,"performance"))available=1;fclose(f);}
  f=fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors","r");char line[160]={0};
  if(f){if(!fgets(line,sizeof line,f))line[0]=0;fclose(f);}
  if(!strstr(line,"ondemand"))available=0;
 }
 if(!available||blank==changed)return;
 FILE *f=fopen(path,"w");if(!f)return;
 int failed=fprintf(f,"%s\n",blank?"ondemand":original)<0;
 if(fclose(f))failed=1;
 if(!failed)changed=blank;
}
static int media(void){
 /* A validated direct-display lease covers AirPlay and other native decoders.
    Listening without a video frame does not acquire this lease. */
 const char *leases[]={"/tmp/doveos-direct-display","/tmp/doveos-usb-video-active"};
 for(unsigned i=0;i<sizeof leases/sizeof leases[0];i++){
  FILE *owner=fopen(leases[i],"r");long pid=0;
  if(owner){if(fscanf(owner,"%ld",&pid)!=1)pid=0;fclose(owner);}
  if(pid>1&&pid<4194304&&!kill((pid_t)pid,0))return 1;
 }
 DIR *d=opendir("/proc");if(!d)return 1;struct dirent *e;int active=0;
 while((e=readdir(d))){if(e->d_name[0]<'0'||e->d_name[0]>'9')continue;
  char path[128],name[64];snprintf(path,sizeof path,"/proc/%.20s/comm",e->d_name);FILE *f=fopen(path,"r");if(!f)continue;
  if(fgets(name,sizeof name,f)){name[strcspn(name,"\n")]=0;
   if(!strcmp(name,"vlc")||!strcmp(name,"dock-stream-hw")||!strcmp(name,"dock-stream-vie")||!strcmp(name,"hdmi-viewer")||!strcmp(name,"dex-input")||!strcmp(name,"doveos-retro"))active=1;
  }fclose(f);if(active)break;
 }closedir(d);return active;
}
static int hardware_ready(void){
 double sampled=0;int duty=0,mode=0;FILE *f=fopen("/tmp/doveos-backlight-state","r");if(!f)return 0;
 int n=fscanf(f,"DOVE_BACKLIGHT1 %lf %d %d",&sampled,&duty,&mode);fclose(f);
 return n==3&&duty>=3&&duty<=100&&mode>=0&&mode<=2&&mono()-sampled>=0&&mono()-sampled<3;
}
int main(void){
 /* ITE owner applies short leases to its verified PWM control. Never suspend Linux. */
 int lock=open("/tmp/doveos-power.lock",O_CREAT|O_RDWR|O_CLOEXEC,0600);if(lock<0||flock(lock,LOCK_EX|LOCK_NB))return 1;
 signal(SIGTERM,stop);signal(SIGINT,stop);
 sigset_t signals;sigemptyset(&signals);sigaddset(&signals,SIGTERM);sigaddset(&signals,SIGINT);sigprocmask(SIG_UNBLOCK,&signals,NULL);
 struct pollfd inputs[32];memset(inputs,0,sizeof inputs);int count=0,off=0,previous_ac=-1;
 double activity=mono(),scan=0,test_until=0,next_media=0,next_settings=0,next_lease=0;int inhibited=0,verified=0,manual_off=0;
 DovePower p={0}; /* Loaded before the first timeout decision. */
 unsigned held[32]={0};unsigned char pressed[32][KEY_MAX+1]={0};
 while(!stopping){
  double now=mono();
  if(now>=scan){
   for(int i=0;i<count;i++)close(inputs[i].fd);
   count=0;memset(held,0,sizeof held);memset(pressed,0,sizeof pressed);DIR *d=opendir("/dev/input");struct dirent *e;
   if(d){while((e=readdir(d))&&count<32){if(strncmp(e->d_name,"event",5))continue;
    char path[128];snprintf(path,sizeof path,"/dev/input/%.30s",e->d_name);int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    if(fd>=0){inputs[count].fd=fd;inputs[count].events=POLLIN;count++;}
   }closedir(d);}scan=now+30;
  }
  poll(inputs,(nfds_t)count,200);
  int used=0;
  for(int i=0;i<count;i++){
   if(inputs[i].revents&POLLIN){struct input_event ev;
    while(read(inputs[i].fd,&ev,sizeof ev)==sizeof ev){
     if(ev.type==EV_KEY){used=1;if(ev.code<=KEY_MAX){int down=ev.value!=0;if(down!=pressed[i][ev.code]){if(down)held[i]++;else if(held[i])held[i]--;pressed[i][ev.code]=(unsigned char)down;}}}
     if(ev.type==EV_REL||ev.type==EV_ABS)used=1;
    }
   }
   if(held[i])used=1;
   if(inputs[i].revents&(POLLHUP|POLLERR|POLLNVAL)){used=1;scan=0;}
  }
  if(used){activity=mono();test_until=0;manual_off=0;}
  if(!access("/tmp/doveos-power-wake",F_OK)){unlink("/tmp/doveos-power-wake");activity=mono();test_until=0;manual_off=0;}
  if(!access("/tmp/doveos-power-standby",F_OK)){unlink("/tmp/doveos-power-standby");manual_off=1;}
  if(!access("/tmp/doveos-power-test",F_OK)){unlink("/tmp/doveos-power-test");test_until=mono()+10;}
  if(mono()>=next_settings){DovePower updated=settings();if(memcmp(&p,&updated,sizeof p))activity=mono();p=updated;verified=access("/media/usb0/DoveOS-v2/home/power-screen-verified",F_OK)==0;next_settings=mono()+1;}
  DoveBattery b;int known=dove_battery_read(&b);
  int ac=known?b.external:-1;if(ac!=previous_ac){activity=mono();previous_ac=ac;}
  int seconds=ac==1?p.ac_seconds:p.battery_seconds;
  int dim_seconds=ac==1?p.ac_dim:p.battery_dim;
  if(mono()>=next_media){inhibited=p.inhibit_media&&media();next_media=mono()+1;}
  if(inhibited)activity=mono();
  int hardware=hardware_ready();
  if(manual_off&&(!verified||!hardware||!known||count==0))manual_off=0;
  int next=0;if(manual_off||test_until>mono())next=2;
  else if(verified&&known&&hardware&&count>0&&!inhibited){
   if(seconds>0&&mono()-activity>=seconds)next=2;
   else if(dim_seconds>0&&mono()-activity>=dim_seconds)next=1;
  }
  if(test_until&&test_until<=mono()){test_until=0;activity=mono();next=0;}
  if(next!=off){if(display(next)){activity=mono();state("Screen control failed; screen-off paused.");}else {off=next;next_lease=mono()+0.5;}}
  /* Renew well inside the two-second hardware lease, preserving the
     existing 200 ms evdev poll and crash-expiry protection. */
  if(off&&mono()>=next_lease){display(off);next_lease=mono()+0.5;}
  idle_cpu(off==2&&verified&&known&&hardware&&!inhibited);
  state(off==2?(hardware?"Backlight off; touchpad or keyboard wakes it.":"Black-screen test; hardware control unavailable."):off==1?"Display dimmed; touchpad or keyboard restores brightness.":!verified?"Automatic screen control awaits wake verification.":!hardware?"Backlight control unavailable; keeping screen on.":!known?"Battery state unavailable; keeping screen on.":inhibited?"Screen kept on while a media app is open.":seconds||dim_seconds?"Automatic dimming and backlight-off ready.":"Screen timeout disabled for this power source.");
 }
 idle_cpu(0);display(0);
 for(int i=0;i<count;i++)close(inputs[i].fd);
 state("Power manager stopped; screen restored.");close(lock);return 0;
}
