/* SPDX-License-Identifier: GPL-2.0-or-later
 * scrcpy 4.1 Opus packets -> bounded PCM FIFO -> owned host speaker player.
 * Included after the viewer's framing helpers. Audio failure preserves video.
 */
#include <dlfcn.h>
#include <poll.h>
#include <time.h>
#ifndef F_SETPIPE_SZ
#define F_SETPIPE_SZ 1031
#endif
static _Atomic int audio_socket=-1;
static const char *audio_fifo;
static unsigned audio_delay_ms;
#ifndef DOVEDEX_OPUS_LIBRARY
#define DOVEDEX_OPUS_LIBRARY "/tmp/dovedex-runtime-v1/lib/libopus.so.0"
#endif
static int64_t audio_now_us(void){
 struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
 return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;
}
static int audio_write_pcm(int fd,const void *data,size_t size,unsigned *waits){
 const unsigned char *at=data;int64_t deadline=audio_now_us()+1000000;
 while(size&&!ended){
  /* Atomic, whole stereo samples; retain a packet when the pipe is full.
   * Dropping here shortens the audio timeline and defeats the sound offset. */
  size_t part=size>4096?4096:size;ssize_t n=write(fd,at,part);
  if(n==(ssize_t)part){at+=part;size-=part;continue;}
  if(n<0&&errno==EINTR)continue;
  if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)&&audio_now_us()<deadline){
   (*waits)++;struct pollfd output={fd,POLLOUT,0};poll(&output,1,20);continue;
  }
  return -1;
 }
 return ended?-1:0;
}
static void *receive_audio(void *unused){
 (void)unused;
 unsigned char header[12],packet[65536];int16_t pcm[5760*2];
 unsigned packets=0,errors=0,waits=0,peak=0,resets=0,late_packets=0;unsigned long long samples=0;
 int64_t base_pts=0,base_time=0,last_pts=0,max_late=0;int clock_ready=0;
 int fd=-1,err=0;void *lib=NULL,*dec=NULL;
 void *(*create)(int,int,int*)=NULL;
 int (*decode)(void*,const unsigned char*,int,int16_t*,int,int)=NULL;
 void (*destroy)(void*)=NULL;
 if(io(audio_socket,header,4,0))goto done;
 uint32_t codec_id=be32(header);
 if(codec_id==0||codec_id==1){fprintf(stderr,"DOVEDEX_AUDIO_UNAVAILABLE server_code=%u; video continues\n",codec_id);goto done;}
 if(codec_id!=0x6f707573){fprintf(stderr,"DOVEDEX_AUDIO_UNSUPPORTED codec=%08x\n",codec_id);goto done;}
 lib=dlopen(DOVEDEX_OPUS_LIBRARY,RTLD_NOW|RTLD_LOCAL);
 if(!lib){fprintf(stderr,"DOVEDEX_AUDIO_LIBRARY_FAILED %s\n",dlerror());goto done;}
 create=dlsym(lib,"opus_decoder_create");decode=dlsym(lib,"opus_decode");destroy=dlsym(lib,"opus_decoder_destroy");
 if(!create||!decode||!destroy)goto done;
 dec=create(48000,2,&err);if(!dec||err)goto done;
 /* The host opens the read end first. Never wait indefinitely for it. */
 for(int n=0;n<30&&!ended;n++){
  fd=open(audio_fifo,O_WRONLY|O_NONBLOCK|O_CLOEXEC);
  if(fd>=0)break;
  if(errno!=ENXIO)break;
  struct timespec wait={0,100000000};nanosleep(&wait,NULL);
 }
 if(fd<0){fprintf(stderr,"DOVEDEX_AUDIO_OUTPUT_FAILED errno=%d\n",errno);goto done;}
 if(fcntl(fd,F_SETPIPE_SZ,4096)!=4096){fprintf(stderr,"DOVEDEX_AUDIO_PIPE_BOUND_FAILED errno=%d\n",errno);goto done;}
 printf("DOVEDEX_AUDIO_READY codec=opus rate=48000 channels=2 output=built-in-speakers delay_ms=%u\n",audio_delay_ms);fflush(stdout);
 while(!ended&&!io(audio_socket,header,12,0)){
  unsigned size=be32(header+8);
  if(!size||size>sizeof packet||io(audio_socket,packet,size,0)){errors++;break;}
  if(header[0]&0x40){
   if(size<19||memcmp(packet,"OpusHead",8)||packet[9]!=2||packet[18]!=0){errors++;break;}
   continue;
  }
  int n=decode(dec,packet,(int)size,pcm,5760,0);
  if(n<=0){errors++;continue;}
  /* scrcpy sends capture-time microseconds. Keep the configured offset after
   * network bursts/silence too, rather than inserting silence only at startup.
   * No extra audio or video queue is allocated. */
  int64_t pts=(int64_t)(be64(header)&0x1fffffffffffffffull),now=audio_now_us();
  if(!clock_ready||pts<last_pts-100000||pts-last_pts>5000000){
   if(clock_ready)resets++;
   base_pts=pts;base_time=now+(int64_t)audio_delay_ms*1000;clock_ready=1;
  }
  last_pts=pts;int64_t due=base_time+pts-base_pts;
  /* Video already discards old decoded frames after a network burst. Decode
   * every Opus packet to retain its codec state, but don't queue stale sound.
   * This is a timestamp decision, never an arbitrary full-pipe discard. */
  if(now-due>100000){late_packets++;continue;}
  while(!ended&&(now=audio_now_us())<due){
   int64_t wait=due-now;if(wait>20000)wait=20000;
   struct timespec pause={0,(long)wait*1000};nanosleep(&pause,NULL);
  }
  if(ended)break;
  int64_t late=audio_now_us()-due;if(late>max_late)max_late=late;
  packets++;
  for(int i=0;i<n*2;i++){int v=pcm[i];unsigned a=v<0?-v:v;if(a>peak)peak=a;}
  samples+=(unsigned)n*2;
  if(audio_write_pcm(fd,pcm,(size_t)n*4,&waits)){if(!ended)errors++;goto done;}
 }
done:
 if(fd>=0)close(fd);
 if(dec&&destroy)destroy(dec);
 if(lib)dlclose(lib);
 if(audio_socket>=0)shutdown(audio_socket,SHUT_RDWR);
 printf("DOVEDEX_AUDIO_END packets=%u errors=%u backpressure_drops=0 backpressure_waits=%u stale_packets=%u clock_resets=%u max_late_ms=%.3f samples=%llu peak=%u\n",packets,errors,waits,late_packets,resets,max_late/1000.0,samples,peak);fflush(stdout);
 return NULL;
}
