/* Private PCM clock trial. Stock ALSA stays outside the optional VLC root. */
#define _GNU_SOURCE
#include <alsa/asoundlib.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <signal.h>
#include <time.h>
#include "audio-ipc.h"
static volatile sig_atomic_t stopped;
static int client=-1;
static void stop(int sig){(void)sig;stopped=1;if(client>=0)shutdown(client,SHUT_RDWR);}
static int io(int fd,void *buf,size_t len,int sending){
 while(len&&!stopped){ssize_t n=sending?send(fd,buf,len,MSG_NOSIGNAL):recv(fd,buf,len,0);if(n<0&&errno==EINTR)continue;if(n<=0){if(!n)errno=ECONNRESET;return -1;}buf=(char*)buf+n;len-=n;}return stopped?-1:0;
}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
#define API_LIST(X) X(snd_pcm_open) X(snd_pcm_close) X(snd_pcm_set_params) X(snd_pcm_writei) X(snd_pcm_wait) X(snd_pcm_recover) X(snd_pcm_delay) X(snd_pcm_pause) X(snd_pcm_drop) X(snd_pcm_prepare) X(snd_pcm_drain) X(snd_pcm_state)
#define DECL(n) static __typeof__(&n) a_##n;
API_LIST(DECL)
#undef DECL
int main(void){
 setvbuf(stdout,NULL,_IOLBF,0);sigset_t set;sigemptyset(&set);sigprocmask(SIG_SETMASK,&set,NULL);
 signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGALRM,stop);signal(SIGPIPE,SIG_IGN);alarm(0);
 const char *library=getenv("DOVE_AUDIO_ALSA_LIBRARY"),*device=getenv("DOVE_AUDIO_PCM");
 if(!library||!*library)library="/usr/lib/libasound.so.2";
 if(!device||!*device)device="plughw:RTKAlsaCard,0";
 void *lib=dlopen(library,RTLD_NOW|RTLD_LOCAL);if(!lib){puts(dlerror());return 2;}
#define LOAD(n) a_##n=dlsym(lib,#n);if(!a_##n)return 3;
 API_LIST(LOAD)
#undef LOAD
 int server=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(server<0)return 4;
 struct sockaddr_un addr={.sun_family=AF_UNIX};strcpy(addr.sun_path,DA_SOCKET);
 /* Never unlink an existing socket owned by a different live trial. */
 if(bind(server,(struct sockaddr*)&addr,sizeof addr)){close(server);return 5;}
 if(chown(DA_SOCKET,65534,29)||chmod(DA_SOCKET,0600)||listen(server,1)){close(server);unlink(DA_SOCKET);return 5;}
 puts("CLOCKED_AUDIO_LISTENING");
 struct pollfd pollfd={.fd=server,.events=POLLIN};
 while(!stopped){
  if(poll(&pollfd,1,250)<=0)continue;
  client=accept4(server,NULL,NULL,SOCK_CLOEXEC);if(client<0)continue;
  struct ucred cred;socklen_t len=sizeof cred;
  if(getsockopt(client,SOL_SOCKET,SO_PEERCRED,&cred,&len)||(cred.uid!=65534&&cred.uid!=0)){close(client);client=-1;continue;}
  struct timeval timeout={30,0};setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
  snd_pcm_t *pcm=NULL;unsigned rate=0,blocks=0,recoveries=0,delays=0,pauses=0,flushes=0;uint64_t frames=0;long maxdelay=0;int failed=0;
  double clock_start=0,clock_report=0;int64_t clock_frames=0;
  double pcm_origin=0,paused_at=0;uint64_t pcm_frames=0;unsigned delay_clamps=0,empty_writes=0,fast_waits=0;
  struct rusage cpu_begin;getrusage(RUSAGE_SELF,&cpu_begin);
  unsigned backoff=getenv("DOVE_AUDIO_RETRY_BACKOFF")?1000:0;
  int causal_delay=getenv("DOVE_AUDIO_CAUSAL_DELAY")!=NULL;
  for(;;){
   da_message m;if(io(client,&m,sizeof m,0)){if(!stopped){failed=1;printf("CLOCKED_AUDIO_RECEIVE_END errno=%d\n",errno);}break;}
   if(m.magic!=DA_MAGIC||m.size>DA_LIMIT||(m.op!=DA_PLAY&&m.size)){failed=1;break;}
   int rc=0;int16_t samples[DA_LIMIT/2];
   if(m.size&&io(client,samples,m.size,0))break;
   if(m.op==DA_OPEN){
    if(pcm||m.arg<8000||m.arg>96000){rc=-EINVAL;}
    else{
     rate=(unsigned)m.arg;rc=a_snd_pcm_open(&pcm,device,SND_PCM_STREAM_PLAYBACK,SND_PCM_NONBLOCK);
     if(!rc)rc=a_snd_pcm_set_params(pcm,SND_PCM_FORMAT_S16_LE,SND_PCM_ACCESS_RW_INTERLEAVED,2,rate,1,80000);
     printf("CLOCKED_AUDIO_OPEN rate=%u rc=%d\n",rate,rc);
    }
   }else if(!pcm)rc=-ENODEV;
   else if(m.op==DA_PLAY){
    if(!m.size||m.size%4)rc=-EINVAL;
    else{unsigned offset=0,total=m.size/4;double limit=now()+3;
     while(offset<total&&!stopped){
      if(!pcm_origin)pcm_origin=now();
      snd_pcm_sframes_t n=a_snd_pcm_writei(pcm,samples+offset*2,total-offset);
      if(n>0){offset+=(unsigned)n;pcm_frames+=(unsigned)n;continue;}
      if(now()>limit){rc=-ETIMEDOUT;break;}
      if(n==-EAGAIN||n==0){
       double before_wait=now();empty_writes++;a_snd_pcm_wait(pcm,100);
       if(now()-before_wait<.0005){fast_waits++;if(backoff)usleep(backoff);}
       continue;
      }
      rc=a_snd_pcm_recover(pcm,(int)n,1);recoveries++;pcm_origin=0;pcm_frames=0;paused_at=0;if(rc<0)break;
     }frames+=offset;blocks++;
    }
   }else if(m.op==DA_DELAY){
    snd_pcm_sframes_t delay=0;rc=a_snd_pcm_delay(pcm,&delay);
    if(rc==0){
     if(causal_delay&&pcm_origin){
      /* The reported queue cannot imply playback before the first write.
       * This nominal-rate floor is experimental, not a physical A/V clock. */
      double elapsed=(paused_at?paused_at:now())-pcm_origin;
      int64_t floor=(int64_t)pcm_frames-(int64_t)(elapsed*rate);
      if(floor>delay){delay=(snd_pcm_sframes_t)floor;delay_clamps++;}
     }
     m.delay=(int64_t)(delay>0?delay:0)*1000000/rate;if(delay>maxdelay)maxdelay=delay;
    }delays++;
    if(!rc){
     double at=now();int64_t played=(int64_t)frames-delay;
     if(!clock_start){clock_start=clock_report=at;clock_frames=played;printf("CLOCKED_AUDIO_BASE frames=%llu delay=%ld state=%d\n",(unsigned long long)frames,(long)delay,(int)a_snd_pcm_state(pcm));}
     if(at-clock_report>=1){
      printf("CLOCKED_AUDIO_RATE seconds=%.3f played=%lld rate=%.3f delay=%ld\n",at-clock_start,(long long)(played-clock_frames),(played-clock_frames)/(at-clock_start),(long)delay);
      clock_report=at;
     }
    }
   }else if(m.op==DA_PAUSE){
    clock_start=0;
    rc=a_snd_pcm_pause(pcm,m.arg!=0);pauses++;printf("CLOCKED_AUDIO_PAUSE value=%d rc=%d\n",m.arg,rc);
    if(!rc){if(m.arg&&!paused_at)paused_at=now();else if(!m.arg&&paused_at){if(pcm_origin)pcm_origin+=now()-paused_at;paused_at=0;}}
   }else if(m.op==DA_FLUSH){
    clock_start=0;
    if(m.arg){double limit=now()+2;do{rc=a_snd_pcm_drain(pcm);if(rc!=-EAGAIN)break;usleep(5000);}while(now()<limit&&!stopped);}
    else rc=a_snd_pcm_drop(pcm);
    if(rc>=0)rc=a_snd_pcm_prepare(pcm);
    pcm_origin=0;pcm_frames=0;paused_at=0;
    flushes++;printf("CLOCKED_AUDIO_FLUSH drain=%d rc=%d\n",m.arg,rc);
   }else if(m.op==DA_STOP){rc=a_snd_pcm_drop(pcm);}
   else rc=-EINVAL;
   da_message reply={.magic=DA_MAGIC,.op=m.op,.arg=rc,.delay=m.op==DA_DELAY?m.delay:0};
   if(io(client,&reply,sizeof reply,1))break;
   if(rc<0){failed=1;printf("CLOCKED_AUDIO_COMMAND_ERROR op=%u rc=%d\n",m.op,rc);}
   if(m.op==DA_STOP)break;
  }
  if(pcm){a_snd_pcm_drop(pcm);a_snd_pcm_close(pcm);}
  close(client);client=-1;
  printf("CLOCKED_AUDIO_END frames=%llu blocks=%u delay_queries=%u max_delay_frames=%ld recoveries=%u pauses=%u flushes=%u failed=%d causal_clamps=%u\n",(unsigned long long)frames,blocks,delays,maxdelay,recoveries,pauses,flushes,failed,delay_clamps);
  struct rusage cpu_end;getrusage(RUSAGE_SELF,&cpu_end);
  double cpu=(cpu_end.ru_utime.tv_sec-cpu_begin.ru_utime.tv_sec)+(cpu_end.ru_stime.tv_sec-cpu_begin.ru_stime.tv_sec)+(cpu_end.ru_utime.tv_usec-cpu_begin.ru_utime.tv_usec+cpu_end.ru_stime.tv_usec-cpu_begin.ru_stime.tv_usec)/1e6;
  printf("CLOCKED_AUDIO_CPU seconds=%.3f empty_writes=%u fast_waits=%u backoff_us=%u\n",cpu,empty_writes,fast_waits,backoff);
 }
 close(server);unlink(DA_SOCKET);dlclose(lib);return 0;
}
