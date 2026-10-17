#define _GNU_SOURCE
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
static volatile sig_atomic_t stopping;
static const char *target="/sys/devices/virtual/gpio/gpio80/value";
static const char *shadow="/tmp/doveos-button-awake-shadow";
static void stop(int sig){(void)sig;stopping=1;}
static double mono(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static int read_value(int fd){char b[8];if(lseek(fd,0,SEEK_SET)<0)return -1;ssize_t n=read(fd,b,sizeof b);return n>0&&(b[0]=='0'||b[0]=='1')?b[0]-'0':-1;}
static int write_file(const char *path,const char *text){
 int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);if(fd<0)return -1;
 size_t size=strlen(text);ssize_t n=write(fd,text,size);int rc=close(fd);return n==(ssize_t)size&&!rc?0:-1;
}
static int same(const char *a,const char *b){struct stat x,y;return !stat(a,&x)&&!stat(b,&y)&&x.st_dev==y.st_dev&&x.st_ino==y.st_ino;}
static int hardware_ready(void){
 double sample=0;int duty=0,mode=0;FILE *f=fopen("/tmp/doveos-backlight-state","r");if(!f)return 0;
 int n=fscanf(f,"DOVE_BACKLIGHT1 %lf %d %d",&sample,&duty,&mode);fclose(f);
 double age=mono()-sample;
 return n==3&&duty>=3&&duty<=100&&mode>=0&&mode<=2&&age>=-0.01&&age<3;
}
static int lid_state(void){
 double sample=0;int closed=-1;unsigned raw=0;FILE *f=fopen("/tmp/doveos-lid-state","r");if(!f)return -1;
 int n=fscanf(f,"DOVE_LID1 %lf %d %u",&sample,&closed,&raw);fclose(f);
 double age=mono()-sample;
 return n==3&&raw<=255&&(closed==0||closed==1)&&age>=-0.01&&age<2?closed:-1;
}
static int lid_wake_request(void){
 char text[48];snprintf(text,sizeof text,"%.9f\n",mono());
 return write_file("/tmp/doveos-lid-wake-request",text);
}
/* Never unmask a low GPIO: it would replay the stock suspend trigger.
 * Wait for the user's second press to restore the original awake level. */
static int restore(int original){
 if(!same(target,shadow))return 0;
 if(read_value(original)!=1)return 1;
 for(int attempt=0;attempt<20;attempt++){
  if(read_value(original)!=1)return 1;
  if(!umount(target)){unlink(shadow);return 0;}
  if(errno!=EBUSY){printf("RESTORE_ERROR errno=%d\n",errno);return -1;}
  struct timespec pause={0,100000000};nanosleep(&pause,NULL);
 }
 printf("RESTORE_BUSY errno=%d\n",errno);return -1;
}
static int selftest(void){
 const char *a="/tmp/dove-button-test-original",*b="/tmp/dove-button-test-shadow";
 if(write_file(a,"0\n")||write_file(b,"1\n"))return 2;
 int old=open(a,O_RDONLY|O_CLOEXEC);if(old<0)return 3;
 if(mount(b,a,NULL,MS_BIND,NULL)){perror("RAM_BIND_TEST");close(old);return 4;}
 int current=open(a,O_RDONLY|O_CLOEXEC);int pass=current>=0&&read_value(old)==0&&read_value(current)==1;
 if(current>=0)close(current);
 int restored=umount(a)==0;close(old);
 if(restored){unlink(a);unlink(b);}
 printf("RAM_ONLY_SELFTEST retained_fd=%d restored=%d\n",pass,restored);return pass&&restored?0:5;
}
static void watchdog(pid_t owner,int original,int lockfd,int service){
 close(lockfd);double deadline=mono()+150;
 while(same(target,shadow)&&kill(owner,0)==0&&(service||mono()<deadline)){struct timespec t={0,100000000};nanosleep(&t,NULL);}
 if(!same(target,shadow))_exit(0);
 kill(owner,SIGTERM);unlink("/tmp/doveos-power-test");write_file("/tmp/doveos-power-wake","1\n");
 deadline=mono()+300;
 while(same(target,shadow)&&mono()<deadline){
  if(!restore(original)){printf("%.3f WATCHDOG_RESTORED\n",mono());fflush(stdout);_exit(0);}
  struct timespec t={0,100000000};nanosleep(&t,NULL);
 }
 printf("%.3f RESTORE_PENDING_GPIO_LOW_PRESS_POWER_ONCE\n",mono());fflush(stdout);_exit(6);
}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IOLBF,0);
 if(argc==2&&!strcmp(argv[1],"--selftest"))return selftest();
 if(argc!=2||(strcmp(argv[1],"--trial")&&strcmp(argv[1],"--service")))return 2;
 int service=!strcmp(argv[1],"--service");
 sigset_t mask;sigemptyset(&mask);sigprocmask(SIG_SETMASK,&mask,NULL);
 signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);
 int lockfd=open("/tmp/doveos-button-screen.lock",O_CREAT|O_RDWR|O_CLOEXEC,0600);
 if(lockfd<0||flock(lockfd,LOCK_EX|LOCK_NB))return 3;
 int original=open(target,O_RDONLY|O_CLOEXEC);
 if(original<0||read_value(original)!=1||!hardware_ready()||!access("/tmp/doveos-direct-display",F_OK))return 4;
 if(write_file(shadow,"1\n"))return 5;
 if(mount(shadow,target,NULL,MS_BIND,NULL)){perror("GPIO_READ_SHADOW_BIND");return 6;}
 int masked=open(target,O_RDONLY|O_CLOEXEC);
 int masked_value=masked<0?-1:read_value(masked);
 if(masked>=0)close(masked);
 if(masked_value!=1||read_value(original)!=1){restore(original);return 7;}
 pid_t owner=getpid(),child=fork();
 if(child<0){restore(original);return 8;}
 if(!child)watchdog(owner,original,lockfd,service);
 printf("%.3f GUARD_READY pid=%d physical=1 stock_read=1 mode=%s\n",mono(),owner,service?"service":"trial120");fsync(STDOUT_FILENO);
 double deadline=mono()+120,beat=mono()+5,last_hardware=mono(),next_blank=0,last_lid_edge=-10;
 int old=1,failed=0,lid=lid_state(),blank=0;
 while(!stopping&&(service||mono()<deadline)){
  int value=read_value(original);
  masked=open(target,O_RDONLY|O_CLOEXEC);masked_value=masked<0?-1:read_value(masked);
  if(masked>=0)close(masked);
  if(hardware_ready())last_hardware=mono();
  if(value<0||masked_value!=1||!same(target,shadow)||mono()-last_hardware>2){
   printf("GUARD_ERROR physical=%d masked=%d same=%d backlight_age=%.3f errno=%d\n",value,masked_value,same(target,shadow),mono()-last_hardware,errno);
   failed=1;break;
  }
  int sensed=lid_state(),lid_changed=sensed>=0&&sensed!=lid;
  if(lid_changed){
   int previous=lid;lid=sensed;last_lid_edge=mono();
   printf("%.3f LID_STATE closed=%d previous=%d\n",mono(),lid,previous);
   if(lid||previous==1){
    blank=lid;
    if(!blank)unlink("/tmp/doveos-power-test");
    if(!blank&&lid_wake_request()){failed=1;break;}
    if(write_file(blank?"/tmp/doveos-power-test":"/tmp/doveos-power-wake","1\n")){failed=1;break;}
    printf("%.3f %s\n",mono(),blank?"LID_SCREEN_OFF":"LID_SCREEN_WAKE");fsync(STDOUT_FILENO);
    next_blank=mono()+0.5;
   }
  }
  if(value!=old){
   old=value;printf("%.3f PHYSICAL_GPIO80=%d STOCK_READ=%d\n",mono(),value,masked_value);
   if(lid!=1&&!lid_changed&&mono()-last_lid_edge>0.8){
    blank=!blank;
    if(!blank)unlink("/tmp/doveos-power-test");
    int rc=write_file(blank?"/tmp/doveos-power-test":"/tmp/doveos-power-wake","1\n");
    printf("%.3f %s rc=%d\n",mono(),blank?service?"SCREEN_OFF_REQUEST_UNTIL_WAKE":"SCREEN_OFF_REQUEST_AUTO_WAKE_10_SECONDS":"SCREEN_WAKE_REQUEST",rc);fsync(STDOUT_FILENO);
    if(rc){failed=1;break;}
    next_blank=mono()+0.5;
   }
  }
  if(lid==1)blank=1;
  if(service&&blank&&mono()>=next_blank){
   if(write_file("/tmp/doveos-power-test","1\n")){failed=1;break;}
   next_blank=mono()+0.5;
  }
  if(mono()>=beat){printf("%.3f HEARTBEAT physical=%d stock=1\n",mono(),old);beat=mono()+(service?60:5);}
  struct timespec pause={0,20000000};nanosleep(&pause,NULL);
 }
 unlink("/tmp/doveos-power-test");write_file("/tmp/doveos-power-wake","1\n");
 int result=restore(original);
 if(result==1){
  puts("RESTORE_WAITING_FOR_SECOND_POWER_PRESS");fsync(STDOUT_FILENO);
  deadline=mono()+20;
  while(result==1&&mono()<deadline){struct timespec pause={0,100000000};nanosleep(&pause,NULL);result=restore(original);}
 }
 if(!result){kill(child,SIGTERM);waitpid(child,NULL,0);}
 printf("%.3f GUARD_FINISHED restored=%d error=%d\n",mono(),!result,failed);fsync(STDOUT_FILENO);
 close(original);close(lockfd);return !result&&!failed?0:9;
}
