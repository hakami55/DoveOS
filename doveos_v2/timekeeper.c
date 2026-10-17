#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "clock_settings.h"
#define MIN_TIME 1577836800L
#define MAX_TIME 2145916799L
static void status(const char *s){FILE *f=fopen("/tmp/doveos-time-status.next","w");if(f){fprintf(f,"%s\n",s);fclose(f);rename("/tmp/doveos-time-status.next","/tmp/doveos-time-status");}}
static void save_now(void){
 time_t now=time(NULL);if(now<MIN_TIME||now>MAX_TIME)return;
 char value[32];snprintf(value,sizeof value,"%ld",(long)now);clock_save("time-last-set.txt",value);
 struct tm utc;gmtime_r(&now,&utc);struct rtc_time r={0};
 r.tm_sec=utc.tm_sec;r.tm_min=utc.tm_min;r.tm_hour=utc.tm_hour;r.tm_mday=utc.tm_mday;r.tm_mon=utc.tm_mon;r.tm_year=utc.tm_year;r.tm_wday=utc.tm_wday;r.tm_yday=utc.tm_yday;r.tm_isdst=0;
 int fd=open("/dev/rtc0",O_RDWR|O_CLOEXEC);if(fd>=0){if(ioctl(fd,RTC_SET_TIME,&r))perror("RTC save");close(fd);}
}
static void restore(void){
 FILE *f=fopen(CLOCK_HOME "time-last-set.txt","r");long last=0;if(f){if(fscanf(f,"%ld",&last)!=1)last=0;fclose(f);}
 time_t now=time(NULL);
 if(last>=MIN_TIME&&last<=MAX_TIME&&(now<MIN_TIME||now<last-300)){
  struct timespec t={(time_t)last,0};if(!clock_settime(CLOCK_REALTIME,&t))status("Restored saved time; waiting for Internet sync.");
 }
}
static int online(void){FILE *f=fopen(CLOCK_HOME "time-auto.txt","r");int c=f?fgetc(f):'1';if(f)fclose(f);return c!='0';}
static int sync_time(void){
 const char *servers[]={"162.159.200.1","162.159.200.123"};
 for(unsigned i=0;i<2;i++){
  int fd=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);if(fd<0)continue;
  struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons(123)};inet_pton(AF_INET,servers[i],&a.sin_addr);
  struct timeval timeout={3,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
  unsigned char q[48]={0},r[512];q[0]=0x23;
  int random=open("/dev/urandom",O_RDONLY);ssize_t got=random<0?-1:read(random,q+40,8);if(random>=0)close(random);if(got!=8){close(fd);continue;}
  struct timespec start,end;clock_gettime(CLOCK_MONOTONIC,&start);
  if(connect(fd,(struct sockaddr*)&a,sizeof a)||send(fd,q,sizeof q,0)!=(ssize_t)sizeof q){close(fd);continue;}
  ssize_t n=recv(fd,r,sizeof r,0);clock_gettime(CLOCK_MONOTONIC,&end);close(fd);
  double elapsed=(end.tv_sec-start.tv_sec)+(end.tv_nsec-start.tv_nsec)/1e9;
  if(n<48||(r[0]&7)!=4||(r[0]>>6)==3||!r[1]||r[1]>15||memcmp(q+40,r+24,8)||elapsed>3.1)continue;
  uint32_t seconds,fraction;memcpy(&seconds,r+40,4);memcpy(&fraction,r+44,4);seconds=ntohl(seconds);fraction=ntohl(fraction);
  int64_t utc=(int64_t)seconds-2208988800LL;if(utc<MIN_TIME)utc+=4294967296LL;if(utc<MIN_TIME||utc>MAX_TIME)continue;
  int64_t ns=(int64_t)((uint64_t)fraction*1000000000ULL>>32)+(int64_t)(elapsed*500000000.0);
  struct timespec t={(time_t)(utc+ns/1000000000LL),(long)(ns%1000000000LL)};
  if(clock_settime(CLOCK_REALTIME,&t)){status("Could not set system time.");return 1;}
  save_now();char value[160];clock_zone();struct tm local;time_t now=time(NULL);localtime_r(&now,&local);char date_text[80];strftime(date_text,sizeof date_text,"%d %b %Y %H:%M:%S",&local);snprintf(value,sizeof value,"Internet time synchronized: %s %s",date_text,clock_zone_label());status(value);puts(value);return 0;
 }
 status("Internet sync unavailable; saved time retained. Check network Internet access.");return 1;
}
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--save")){save_now();return 0;}
 if(argc==2&&!strcmp(argv[1],"--sync"))return sync_time();
 if(argc==2&&!strcmp(argv[1],"--restore")){restore();return 0;}
 if(argc!=2||strcmp(argv[1],"--daemon"))return 2;
 int lock=open("/tmp/doveos-timekeeper.lock",O_CREAT|O_RDWR|O_CLOEXEC,0600);if(lock<0||flock(lock,LOCK_EX|LOCK_NB))return 0;
 sigset_t signals;sigemptyset(&signals);sigprocmask(SIG_SETMASK,&signals,NULL);
 restore();long retry=0,save=0;
 for(;;){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);if(online()&&t.tv_sec>=retry)retry=t.tv_sec+(sync_time()?60:1800);if(t.tv_sec>=save){save_now();save=t.tv_sec+300;}sleep(10);}
}
