#define _GNU_SOURCE
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <grp.h>
static volatile sig_atomic_t stop;
static void stopped(int s){stop=s;}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
int main(int argc,char**argv){
 if(argc<4)return 2;int seconds=atoi(argv[1]);if(seconds<0||seconds>43200)return 2;
 sigset_t set;sigemptyset(&set);sigprocmask(SIG_SETMASK,&set,NULL);signal(SIGCHLD,SIG_DFL);signal(SIGTERM,stopped);signal(SIGINT,stopped);signal(SIGHUP,stopped);
 pid_t p=fork();if(p<0)return 3;
 if(!p){setpgid(0,0);signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);signal(SIGHUP,SIG_DFL);
  if(chroot(argv[2])||chdir("/tmp/browser-profile")){perror("browser root");_exit(4);}
  gid_t internet_group=3003;
  if(setgroups(1,&internet_group)||setgid(65534)||setuid(65534)){perror("browser user");_exit(5);}
  setenv("HOME","/tmp/browser-profile",1);setenv("XDG_CONFIG_HOME","/tmp/browser-profile/.config",1);setenv("XDG_CACHE_HOME","/tmp/browser-profile/.cache",1);setenv("XDG_RUNTIME_DIR","/tmp/browser-profile/runtime",1);
  setenv("DISPLAY",":7",1);setenv("GDK_BACKEND","x11",1);setenv("NO_AT_BRIDGE","1",1);setenv("GSETTINGS_BACKEND","memory",1);setenv("LANG","C",1);
  setenv("LD_LIBRARY_PATH","/usr/lib/firefox-esr:/tmp/mesa/lib:/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf",1);unsetenv("GTK_PATH");unsetenv("GTK_THEME");unsetenv("GDK_PIXBUF_MODULEDIR");
  setenv("GDK_PIXBUF_MODULE_FILE","/tmp/browser-loaders.cache",1);
  execv(argv[3],argv+3);perror("browser exec");_exit(127);
 }
 setpgid(p,p);double deadline=now()+seconds;int status=0;const char*reason="TIME_LIMIT";
 while((!seconds||now()<deadline)&&!stop){if(waitpid(p,&status,WNOHANG)==p){kill(-p,SIGTERM);return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);}
  FILE*f=fopen("/proc/meminfo","r");char line[180];unsigned long free_k=0,cache=0,buf=0,shared=0,swap=0,cma=0;
  if(f){while(fgets(line,sizeof(line),f)){sscanf(line,"MemFree: %lu",&free_k);sscanf(line,"Cached: %lu",&cache);sscanf(line,"Buffers: %lu",&buf);sscanf(line,"Shmem: %lu",&shared);sscanf(line,"SwapFree: %lu",&swap);sscanf(line,"CmaFree: %lu",&cma);}fclose(f);}
  if(!cma){FILE*v=fopen("/proc/vmstat","r");if(v){while(fgets(line,sizeof(line),v)){unsigned long pages;if(sscanf(line,"nr_free_cma %lu",&pages)==1)cma=pages*4;}fclose(v);}}
  unsigned long available=free_k+buf+(cache>shared?(cache-shared)/2:0);
  if(available<cma+20000 || swap<16000){reason="MEMORY_GUARD";break;}usleep(250000);
 }
 printf("BROWSER_TRIAL_STOP=%s\n",stop?"INTERRUPTED":reason);fflush(stdout);kill(-p,SIGTERM);usleep(600000);kill(-p,SIGKILL);waitpid(p,&status,0);return 124;
}
