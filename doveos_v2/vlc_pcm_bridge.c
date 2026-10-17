#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

static volatile sig_atomic_t stopped;
static void stop(int sig){(void)sig;stopped=1;}
static unsigned u16(const unsigned char *p){return p[0]|(p[1]<<8);}
static unsigned u32(const unsigned char *p){return u16(p)|(u16(p+2)<<16);}
static int exact(int fd,unsigned char *p,size_t size){
    while(size&&!stopped){ssize_t n=read(fd,p,size);if(n<0&&errno==EINTR)continue;if(n<=0)return -1;p+=n;size-=(size_t)n;}
    return stopped?-1:0;
}
int main(int argc,char **argv){
    if(argc!=2)return 2;
    sigset_t unblocked;sigemptyset(&unblocked);sigprocmask(SIG_SETMASK,&unblocked,NULL);
    signal(SIGCHLD,SIG_DFL);
    struct sigaction sa={0};sa.sa_handler=stop;sigemptyset(&sa.sa_mask);sigaction(SIGTERM,&sa,NULL);sigaction(SIGINT,&sa,NULL);signal(SIGPIPE,SIG_IGN);
    unsigned long long total=0;
    while(!stopped){
        int input=open(argv[1],O_RDONLY);if(input<0){if(errno==EINTR)continue;perror("fifo");return 3;}
        unsigned char header[44];if(exact(input,header,sizeof header)){close(input);continue;}
        unsigned channels=u16(header+22),rate=u32(header+24),bits=u16(header+34);
        if(memcmp(header,"RIFF",4)||memcmp(header+8,"WAVEfmt ",8)||u16(header+20)!=1||bits!=16||channels<1||channels>2||rate<8000||rate>96000){fprintf(stderr,"Unsupported PCM header\n");close(input);return 4;}
        int output[2];if(pipe(output)){close(input);return 5;}
        pid_t child=fork();
        if(child==0){
            char ratestr[12],chanstr[4];snprintf(ratestr,sizeof ratestr,"%u",rate);snprintf(chanstr,sizeof chanstr,"%u",channels);
            dup2(output[0],STDIN_FILENO);close(output[0]);close(output[1]);close(input);
            if(access("/media/usb0/DoveOS-v2/audio-route.sh",R_OK)==0)
                execl("/bin/sh","sh","/media/usb0/DoveOS-v2/audio-route.sh","raw",ratestr,chanstr,(char*)NULL);
            execl("/usr/bin/aplay","aplay","-q","-N","-B","100000","-F","25000","-D","plughw:RTKAlsaCard,0","-t","raw","-f","S16_LE","-r",ratestr,"-c",chanstr,(char*)NULL);_exit(127);
        }
        close(output[0]);if(child<0){close(output[1]);close(input);return 6;}
        fprintf(stderr,"PCM_BRIDGE_START rate=%u channels=%u\n",rate,channels);
        unsigned char buffer[4096];
        while(!stopped){
            ssize_t n=read(input,buffer,sizeof buffer);if(n<0&&errno==EINTR)continue;if(n<=0)break;
            size_t offset=0;while(offset<(size_t)n&&!stopped){ssize_t w=write(output[1],buffer+offset,(size_t)n-offset);if(w<0&&errno==EINTR)continue;if(w<=0){stopped=1;break;}offset+=(size_t)w;total+=(unsigned long long)w;}
        }
        close(input);close(output[1]);if(stopped)kill(child,SIGTERM);
        int status=0;pid_t result=0;
        struct timespec tick={0,100000000};
        for(int i=0;i<20&&(result=waitpid(child,&status,WNOHANG))==0;i++)nanosleep(&tick,NULL);
        if(result==0){kill(child,SIGTERM);for(int i=0;i<10&&(result=waitpid(child,&status,WNOHANG))==0;i++)nanosleep(&tick,NULL);}
        if(result==0){kill(child,SIGKILL);waitpid(child,&status,WNOHANG);}
        fprintf(stderr,"PCM_BRIDGE_END bytes=%llu status=%d\n",total,status);
    }
    return 0;
}
