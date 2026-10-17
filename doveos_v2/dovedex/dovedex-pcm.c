/* SPDX-License-Identifier: MIT
 * One owned PCM session. Static ARM helper runs in the stock host root.
 * No mixer changes, device resets, exclusive grabs, or persistent children.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static volatile sig_atomic_t stopped;
static void stop(int s){(void)s;stopped=1;}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
int main(int argc,char **argv){
 if(argc!=2)return 2;
 sigset_t empty;sigemptyset(&empty);sigprocmask(SIG_SETMASK,&empty,NULL);
 struct sigaction sa={0};sa.sa_handler=stop;sigemptyset(&sa.sa_mask);
 sigaction(SIGTERM,&sa,NULL);sigaction(SIGINT,&sa,NULL);signal(SIGPIPE,SIG_IGN);signal(SIGCHLD,SIG_DFL);
 int input=open(argv[1],O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(input<0){perror("audio fifo");return 3;}
 int output[2];if(pipe(output)){close(input);return 4;}
 /* Each byte pipe previously could retain hundreds of ms after a Wi-Fi burst.
  * Keep this session's pending PCM bounded to one small, whole-sample block. */
 if(fcntl(output[1],F_SETPIPE_SZ,4096)!=4096){perror("speaker pipe size");close(input);close(output[0]);close(output[1]);return 4;}
 pid_t child=fork();if(child<0)return 5;
 if(!child){
  dup2(output[0],STDIN_FILENO);close(output[0]);close(output[1]);close(input);
  unsetenv("LD_LIBRARY_PATH");unsetenv("LD_PRELOAD");
  execl("/usr/bin/aplay","aplay","-q","-v","-N","-B","40000","-F","10000","-D","plughw:RTKAlsaCard,0","-t","raw","-f","S16_LE","-r","48000","-c","2",(char*)NULL);
  perror("speaker player");_exit(127);
 }
 close(output[0]);fcntl(output[1],F_SETFL,O_NONBLOCK);
 unsigned long long bytes=0;int connected=0,status=0,reaped=0,failure=0;
 double deadline=now()+15;
 fprintf(stderr,"DOVEDEX_PCM_READY rate=48000 channels=2 buffer_ms=40 period_ms=10 child=%ld\n",(long)child);
 unsigned char data[4096];
 while(!stopped){
  if(waitpid(child,&status,WNOHANG)==child){reaped=1;failure=1;break;}
  struct pollfd p={input,POLLIN,0};poll(&p,1,50);
  ssize_t n=read(input,data,sizeof data);
  if(n<0&&(errno==EINTR||errno==EAGAIN))continue;
  if(n<0){failure=1;break;}
  if(!n){if(connected)break;if(now()>deadline){failure=1;break;}struct timespec t={0,50000000};nanosleep(&t,NULL);continue;}
  connected=1;size_t offset=0;double write_deadline=now()+1;
  while(offset<(size_t)n&&!stopped){
   ssize_t w=write(output[1],data+offset,(size_t)n-offset);
   if(w>0){offset+=(size_t)w;bytes+=(unsigned long long)w;continue;}
   if(w<0&&errno==EINTR)continue;
   if(w<0&&errno==EAGAIN&&now()<write_deadline){struct pollfd out={output[1],POLLOUT,0};poll(&out,1,50);continue;}
   stopped=1;failure=1;break;
  }
 }
 close(input);close(output[1]);
 if(!reaped){
  if(stopped)kill(child,SIGTERM);
  for(int n=0;n<20;n++){if(waitpid(child,&status,WNOHANG)==child){reaped=1;break;}struct timespec t={0,100000000};nanosleep(&t,NULL);}
  if(!reaped){kill(child,SIGKILL);while(waitpid(child,&status,0)<0&&errno==EINTR){}}
 }
 fprintf(stderr,"DOVEDEX_PCM_END bytes=%llu status=%d stopped=%d failure=%d\n",bytes,status,(int)stopped,failure);
 return failure||!WIFEXITED(status)||WEXITSTATUS(status)?6:0;
}
