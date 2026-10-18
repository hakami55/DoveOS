/* Selected HDMI capture audio to the native speaker/headphone playback PCM.
 * Capture is matched to the selected USB device; never opens the dock microphone. */
#define _GNU_SOURCE
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sched.h>
static volatile sig_atomic_t stopping;
static void stop(int n){(void)n;stopping=1;}
static double seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static int number(const char *directory,const char *name,int hex){
 char path[PATH_MAX];if(snprintf(path,sizeof path,"%s/%s",directory,name)>=(int)sizeof path)return -1;
 FILE *f=fopen(path,"r");unsigned v=0;if(!f)return -1;int ok=fscanf(f,hex?"%x":"%u",&v)==1;fclose(f);return ok&&v<65536?(int)v:-1;
}
static int usb_card(int bus,int address,int vid,int pid){
 DIR *dir=opendir("/sys/class/sound");if(!dir)return -1;struct dirent *entry;int found=-1;
 while((entry=readdir(dir))){int card=-1;char tail;
  if(sscanf(entry->d_name,"card%d%c",&card,&tail)!=1||card<0||card>31)continue;
  char link[128],path[PATH_MAX];snprintf(link,sizeof link,"/sys/class/sound/card%d/device",card);
  if(!realpath(link,path))continue;
  for(int depth=0;depth<6;depth++){
   int b=number(path,"busnum",0),a=number(path,"devnum",0);
   if(b>=0&&a>=0){
    if((bus<0||b==bus)&&(address<0||a==address)&&number(path,"idVendor",1)==vid&&number(path,"idProduct",1)==pid){
     if(found>=0&&found!=card){closedir(dir);return -2;}found=card;
    }break;
   }
   char *slash=strrchr(path,'/');if(!slash||slash==path)break;*slash=0;
  }
 }
 closedir(dir);return found;
}
static int native_card(void){
 for(int n=0;n<32;n++){char path[80],id[80];snprintf(path,sizeof path,"/proc/asound/card%d/id",n);FILE *f=fopen(path,"r");if(!f)continue;
  int ok=fscanf(f,"%79s",id)==1;fclose(f);if(ok&&!strcmp(id,"RTKAlsaCard"))return n;
 }return -1;
}
static int relay(const char *input,const char *output,int duration,int selftest){
 snd_pcm_t *capture=NULL,*playback=NULL;int result=4,err;
 if(!selftest){struct sched_param priority={.sched_priority=3};printf("Audio priority: rc=%d\n",sched_setscheduler(0,SCHED_FIFO,&priority));}
 const char *phase="capture open";
 err=snd_pcm_open(&capture,input,SND_PCM_STREAM_CAPTURE,SND_PCM_NONBLOCK);if(err<0)goto fail;
 phase="capture format";
 err=snd_pcm_set_params(capture,SND_PCM_FORMAT_S16_LE,SND_PCM_ACCESS_RW_INTERLEAVED,2,48000,0,40000);if(err<0)goto fail;
 phase="speaker open";
 err=snd_pcm_open(&playback,output,SND_PCM_STREAM_PLAYBACK,SND_PCM_NONBLOCK);if(err<0)goto fail;
 phase="speaker format";
 unsigned playback_us=getenv("DOVE_HDMI_AUDIO_BUFFER_US")?(unsigned)atoi(getenv("DOVE_HDMI_AUDIO_BUFFER_US")):40000;
 if(playback_us<20000||playback_us>100000)playback_us=20000;
 err=snd_pcm_set_params(playback,SND_PCM_FORMAT_S16_LE,SND_PCM_ACCESS_RW_INTERLEAVED,2,48000,0,playback_us);if(err<0)goto fail;
 snd_pcm_uframes_t cb=0,cp=0,pb=0,pp=0;snd_pcm_get_params(capture,&cb,&cp);snd_pcm_get_params(playback,&pb,&pp);
 printf("Audio buffers: capture=%lu/%lu playback=%lu/%lu frames, 48 kHz.\n",cb,cp,pb,pp);
 snd_pcm_sw_params_t *sw;snd_pcm_sw_params_alloca(&sw);phase="speaker start threshold";
 /* USB capture delivers bursts. Starting after one 10-ms period can empty
    the speaker queue before the next burst; prime half the 40-ms buffer. */
 snd_pcm_uframes_t priming=pb/2;if(priming<pp)priming=pp;
 if((err=snd_pcm_sw_params_current(playback,sw))<0||(err=snd_pcm_sw_params_set_start_threshold(playback,sw,priming))<0||(err=snd_pcm_sw_params(playback,sw))<0)goto fail;
 printf("Audio start threshold: %lu frames (%.1f ms).\n",priming,priming/48.0);
 /* Nonblocking read on this ALSA stack stays PREPARED until explicitly started. */
 phase="capture start";err=snd_pcm_start(capture);if(err<0)goto fail;
 int16_t samples[240*2]={0};uint64_t frames=0;unsigned overruns=0,underruns=0,peak=0;
 double start=seconds(),last=seconds(),report=seconds(),max_write_wait=0;puts(selftest?"Audio software check: null input/output, no hardware.":"Audio connected: USB capture to Lap Dock speakers.");
 while(!stopping&&(!duration||seconds()-start<duration)){
  if(seconds()-last>5){err=-ETIMEDOUT;phase="capture no samples";goto fail;}
  snd_pcm_sframes_t n=snd_pcm_readi(capture,samples,240);
  if(n==-EAGAIN||n==-EINTR){snd_pcm_wait(capture,10);if(seconds()-last>5){err=-ETIMEDOUT;phase="capture no samples";goto fail;}continue;}
  if(n==-EPIPE){overruns++;err=snd_pcm_prepare(capture);if(err>=0)err=snd_pcm_start(capture);if(err<0){phase="capture recovery";goto fail;}continue;}
  if(n<0){err=(int)n;phase="capture read";goto fail;}
  if(n==0){usleep(1000);continue;}last=seconds();for(snd_pcm_sframes_t k=0;k<n*2;k++){unsigned p=samples[k]<0?-(int)samples[k]:samples[k];if(p>peak)peak=p;}snd_pcm_sframes_t sent=0;double write_begin=seconds(),until=write_begin+.5;
  while(sent<n&&!stopping){
   if(seconds()>until){err=-ETIMEDOUT;phase="speaker write timeout";goto fail;}
   snd_pcm_sframes_t w=snd_pcm_writei(playback,samples+sent*2,n-sent);
   if(w==-EAGAIN||w==-EINTR||w==0){if(seconds()>until){err=-ETIMEDOUT;phase="speaker write timeout";goto fail;}snd_pcm_wait(playback,10);continue;}
   if(w==-EPIPE){underruns++;err=snd_pcm_prepare(playback);if(err<0){phase="speaker recovery";goto fail;}continue;}
   if(w<0){err=(int)w;phase="speaker write";goto fail;}sent+=w;
  }
  frames+=sent;
  double write_wait=seconds()-write_begin;if(write_wait>max_write_wait)max_write_wait=write_wait;
  if(seconds()-report>=5){snd_pcm_sframes_t delay=0;snd_pcm_delay(playback,&delay);printf("Audio timing: seconds=%.3f frames=%llu rate=%.1f delay_frames=%ld max_write_ms=%.2f overruns=%u underruns=%u peak=%u\n",seconds()-start,(unsigned long long)frames,frames/(seconds()-start),(long)delay,max_write_wait*1000,overruns,underruns,peak);report=seconds();}
  if(selftest){if(selftest==1&&frames>=48000)break;usleep(1000);}
 }
 printf("Audio stopped: frames=%llu overruns=%u underruns=%u peak=%u selftest=%d\n",(unsigned long long)frames,overruns,underruns,peak,selftest);
 result=selftest&&frames<48000?5:0;goto done;
fail:
 fprintf(stderr,"Audio unavailable (%s): %s. Video can continue.\n",phase,snd_strerror(err));
done:
 if(capture){snd_pcm_drop(capture);snd_pcm_close(capture);}
 if(playback){snd_pcm_drop(playback);snd_pcm_close(playback);}return result;
}
int main(int argc,char **argv){
 if(argc>=4&&!strcmp(argv[argc-2],"--log")){
  int fd=open(argv[argc-1],O_WRONLY|O_APPEND|O_NOFOLLOW);struct stat st;
  if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||st.st_nlink!=1){if(fd>=0)close(fd);return 8;}
  if(dup2(fd,STDOUT_FILENO)<0||dup2(fd,STDERR_FILENO)<0){close(fd);return 8;}close(fd);argc-=2;
 }
 setvbuf(stdout,NULL,_IOLBF,0);setvbuf(stderr,NULL,_IOLBF,0);
 signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGPIPE,SIG_IGN);
 pid_t parent=getppid();if(prctl(PR_SET_PDEATHSIG,SIGTERM)<0||getppid()!=parent)return 3;
 if(argc==2&&!strcmp(argv[1],"--self-test"))return relay("null","null",5,1);
 if(argc==2&&!strcmp(argv[1],"--self-test-hold"))return relay("null","null",5,2);
 if(argc!=6){fprintf(stderr,"Usage: capture-audio bus address vendor product seconds\n");return 2;}
 char *ends[5];long v[5];for(int i=0;i<5;i++){errno=0;v[i]=strtol(argv[i+1],&ends[i],10);if(errno||*ends[i]||v[i]<(i==4?0:1)||v[i]>65535)return 2;}
 if(v[0]>255||v[1]>127||v[4]>1800)return 2;
 int capture=usb_card(v[0],v[1],v[2],v[3]),speaker=native_card();
 if(capture<0){puts("Audio unavailable: this capture card has no unique Linux USB audio device. Video can continue.");return 6;}
 if(speaker<0||capture==speaker){puts("Audio unavailable: native Lap Dock speaker playback device missing. Video can continue.");return 7;}
 char in[32],out[32];snprintf(in,sizeof in,"hw:%d,0",capture);snprintf(out,sizeof out,"hw:%d,0",speaker);
 printf("Audio selected: USB %ld:%ld card=%d speaker=%d, 48 kHz stereo S16LE.\n",v[0],v[1],capture,speaker);
 return relay(in,out,v[4],0);
}
