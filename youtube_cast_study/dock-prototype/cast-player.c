/* DovePlayer: local-file demux/audio plus guarded Realtek video presentation.
 * Video pixels stay in decoder-owned buffers. No software video decoder.
 * Pause/seek end the current epoch; the UI resumes in a fresh decoder session.
 */
#define _GNU_SOURCE
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include "video-ipc.h"
#include "audio-ipc.h"

static atomic_int stopping,failed,audio_ready,audio_finished,video_finished;
static atomic_int cast_volume=50;
static volatile sig_atomic_t action;
static int video_fd=-1,control_fd=-1,audio_fd=-1;
static const char *filename,*audio_filename;
static double target,limit_seconds,duration,origin,position;
static pthread_mutex_t clock_lock=PTHREAD_MUTEX_INITIALIZER;
static atomic_uint shown,dropped;
static unsigned packets,audio_blocks;
static double max_gap,last_present,max_late;
static int use_audio=1,video_index=-1,audio_index=-1;
static AVFormatContext *vf,*af;
static AVBSFContext *bsf;
static AVCodecContext *ac;
static double media_zero;

static void cast_update_volume(void){
 FILE *vol=fopen("/tmp/doveplayer/cast-volume","r");int gain=50;
 if(vol){if(fscanf(vol,"%d",&gain)==1&&gain>=0&&gain<=100)atomic_store(&cast_volume,gain);fclose(vol);}
}
static int cast_loopback_url(const char *url){
 if(!url||strncmp(url,"http://127.0.0.1:",17)||strpbrk(url,"\r\n"))return 0;
 char *end;long port=strtol(url+17,&end,10);
 return end>url+17&&*end=='/'&&port>0&&port<=65535;
}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static int interrupted(void *unused){(void)unused;return atomic_load(&stopping)||action;}
static void signal_action(int n){action=n==SIGUSR1?21:n==SIGUSR2?22:20;}
static void fail(const char *what){fprintf(stderr,"DOVEPLAYER_ERROR %s errno=%d\n",what,errno);atomic_store(&failed,1);atomic_store(&stopping,1);}
static int io(int fd,void *buf,size_t len,int send_it){
 while(len&&!atomic_load(&stopping)){
  ssize_t n=send_it?send(fd,buf,len,MSG_NOSIGNAL):recv(fd,buf,len,0);
  if(n<0&&errno==EINTR)continue;
  if(n<=0)return -1;
  buf=(char*)buf+n;len-=(size_t)n;
 }return len?-1:0;
}
static int connect_local(const char *name){
 int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd<0)return -1;
 struct sockaddr_un a={.sun_family=AF_UNIX};snprintf(a.sun_path,sizeof a.sun_path,"%s",name);
 struct timeval timeout={8,0};setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
 if(connect(fd,(struct sockaddr*)&a,sizeof a)){close(fd);return -1;}return fd;
}
static int receive_fd(int fd,rtk_msg *m,int *received){
 char c[CMSG_SPACE(4*sizeof(int))]={0};struct iovec v={m,sizeof *m};
 struct msghdr h={.msg_iov=&v,.msg_iovlen=1,.msg_control=c,.msg_controllen=sizeof c};
 ssize_t n;do n=recvmsg(fd,&h,MSG_CMSG_CLOEXEC);while(n<0&&errno==EINTR&&!action);
 if(n<=0)return -1;
 int first=-1,count=0,bad=h.msg_flags&MSG_CTRUNC;
 for(struct cmsghdr *p=CMSG_FIRSTHDR(&h);p;p=CMSG_NXTHDR(&h,p))if(p->cmsg_level==SOL_SOCKET&&p->cmsg_type==SCM_RIGHTS){
  if(p->cmsg_len<CMSG_LEN(0)){bad=1;continue;}
  int *fds=(int*)CMSG_DATA(p);for(size_t i=0;i<(p->cmsg_len-CMSG_LEN(0))/sizeof(int);i++)if(!count++)first=fds[i];else {close(fds[i]);bad=1;}
 }
 if(bad||count!=1){if(first>=0)close(first);return -1;}
 if((size_t)n<sizeof *m&&io(fd,(char*)m+n,sizeof *m-n,0)){close(first);return -1;}
 *received=first;return 0;
}
static int video_control(int op,uint32_t token){rtk_msg m={.magic=RTK_MAGIC,.type=op,.flags=token};return io(control_fd,&m,sizeof m,1);}
static int audio_command(int op,int arg,const void *data,unsigned bytes,int64_t *delay){
 da_message m={.magic=DA_MAGIC,.op=op,.size=bytes,.arg=arg};
 if(io(audio_fd,&m,sizeof m,1)||(bytes&&io(audio_fd,(void*)data,bytes,1))||io(audio_fd,&m,sizeof m,0)||m.magic!=DA_MAGIC||m.op!=(unsigned)op||m.arg<0)return -1;
 if(delay)*delay=m.delay;
 return 0;
}
static double clock_origin(void){pthread_mutex_lock(&clock_lock);double t=origin;pthread_mutex_unlock(&clock_lock);return t;}
static double get_position(void){pthread_mutex_lock(&clock_lock);double t=position;pthread_mutex_unlock(&clock_lock);return t;}
static void set_position(double t){pthread_mutex_lock(&clock_lock);position=t;pthread_mutex_unlock(&clock_lock);}
static void start_clock(void){pthread_mutex_lock(&clock_lock);if(!origin){origin=now()+.20-target;printf("PLAYER_CLOCK target=%.6f wall=%.6f\n",target,origin);}pthread_mutex_unlock(&clock_lock);}
static int wait_media(double pts,double ahead){
 while(!atomic_load(&stopping)&&!action){double o=clock_origin();if(o&&now()>=o+pts-ahead)return 0;usleep(1000);}return -1;
}
static void save_state(const char *state){
 FILE *f=fopen("/tmp/doveplayer/state.part","w");if(!f)return;
 fprintf(f,"%s\n%.6f\n%.6f\n%u\n%u\n",state,get_position(),duration,atomic_load(&shown),atomic_load(&dropped));fclose(f);rename("/tmp/doveplayer/state.part","/tmp/doveplayer/state");
}
static int open_file(AVFormatContext **ctx){
 *ctx=avformat_alloc_context();if(!*ctx)return -1;
 (*ctx)->interrupt_callback=(AVIOInterruptCB){interrupted,NULL};
 AVDictionary *options=NULL;av_dict_set(&options,"protocol_whitelist","http,tcp",0);
 av_dict_set(&options,"rw_timeout","45000000",0);
 av_dict_set(&options,"probesize","262144",0);
 int rc=avformat_open_input(ctx,ctx==&af?audio_filename:filename,NULL,&options);av_dict_free(&options);
 return rc<0||avformat_find_stream_info(*ctx,NULL)<0?-1:0;
}
static int seek_file(AVFormatContext *f){return target>0?av_seek_frame(f,-1,(int64_t)((target+media_zero)*AV_TIME_BASE),AVSEEK_FLAG_BACKWARD):0;}
static void *send_video(void *unused){
 (void)unused;AVPacket *p=av_packet_alloc(),*out=av_packet_alloc();int done=0;
 if(!p||!out){fail("packet allocation");goto end;}
 while(!atomic_load(&stopping)&&!action){
  int rc=av_read_frame(vf,p);if(rc<0){if(rc!=AVERROR_EOF&&!action)fail("video read");break;}
  if(p->stream_index!=video_index){av_packet_unref(p);continue;}
  if(av_bsf_send_packet(bsf,p)<0){fail("H264 packet conversion");break;}
  while(av_bsf_receive_packet(bsf,out)==0){
   int64_t pts=out->pts==AV_NOPTS_VALUE?out->dts:out->pts;
   if(pts==AV_NOPTS_VALUE||out->size<=0||(unsigned)out->size>RTK_LIMIT){av_packet_unref(out);fail("unsupported video timestamps or packet size");goto end;}
   double seconds=pts*av_q2d(bsf->time_base_out)-media_zero;
   if(limit_seconds>0&&seconds>target+limit_seconds+1){done=1;av_packet_unref(out);break;}
   rtk_msg m={.magic=RTK_MAGIC,.type=RTK_PACKET,.size=out->size,.pts=(int64_t)llround(seconds*1e6)};
   if(io(video_fd,&m,sizeof m,1)||io(video_fd,out->data,out->size,1)){av_packet_unref(out);if(!atomic_load(&stopping)&&!action)fail("video input channel");goto end;}
   packets++;av_packet_unref(out);
  }if(done)break;
 }
 if(!atomic_load(&stopping)&&!action){rtk_msg m={.magic=RTK_MAGIC,.type=RTK_DRAIN};if(io(video_fd,&m,sizeof m,1))fail("video drain");}
end:av_packet_free(&p);av_packet_free(&out);return NULL;
}
static int output_audio_frame(AVFrame *f,SwrContext **resampler,double *next_pts){
 if(!*resampler){
  int64_t layout=f->channel_layout?(int64_t)f->channel_layout:av_get_default_channel_layout(f->channels);
  *resampler=swr_alloc_set_opts(NULL,AV_CH_LAYOUT_STEREO,AV_SAMPLE_FMT_S16,48000,layout,f->format,f->sample_rate,0,NULL);
  if(!*resampler||swr_init(*resampler)<0)return -1;
 }
 double pts=f->best_effort_timestamp==AV_NOPTS_VALUE?*next_pts:f->best_effort_timestamp*av_q2d(af->streams[audio_index]->time_base)-media_zero;
 int capacity=(int)av_rescale_rnd(swr_get_delay(*resampler,f->sample_rate)+f->nb_samples,48000,f->sample_rate,AV_ROUND_UP);
 if(capacity<=0||capacity>32768)return -1;
 uint8_t *pcm=av_malloc((size_t)capacity*4);if(!pcm)return -1;
 int count=swr_convert(*resampler,&pcm,capacity,(const uint8_t**)f->extended_data,f->nb_samples);
 if(count<0){av_free(pcm);return -1;}
 int gain=atomic_load(&cast_volume);
 if(gain<100){int16_t *samples=(int16_t*)pcm;for(int i=0;i<count*2;i++)samples[i]=(int16_t)((int32_t)samples[i]*gain/100);}
 *next_pts=pts+(double)count/48000;
 int skip=pts<target?(int)ceil((target-pts)*48000):0;if(skip>count)skip=count;
 for(int at=skip;at<count;){
  double t=pts+(double)at/48000;if(limit_seconds>0&&t>=target+limit_seconds)break;
  int n=count-at;if(n>960)n=960; // <=20 ms per bounded audio command.
  if(wait_media(t,.065)){av_free(pcm);return 0;}
  if(audio_command(DA_PLAY,0,pcm+at*4,n*4,NULL)){av_free(pcm);return -1;}
  at+=n;audio_blocks++;
  if(video_index<0)set_position(t);
  if(audio_blocks%100==0){int64_t delay=0;if(audio_command(DA_DELAY,0,NULL,0,&delay)){av_free(pcm);return -1;}
   printf("PLAYER_AUDIO pts=%.6f clock=%.6f reported_delay_ms=%.3f\n",t,now()-clock_origin(),delay/1000.0);}
 }
 av_free(pcm);return 0;
}
static void *audio_loop(void *unused){
 (void)unused;AVPacket *p=av_packet_alloc();AVFrame *f=av_frame_alloc();SwrContext *resampler=NULL;double next_pts=target;
 if(!p||!f||audio_command(DA_OPEN,48000,NULL,0,NULL)){fail("audio output open");goto end;}
 atomic_store(&audio_ready,1);if(video_index<0)start_clock();
 int eof=0,consecutive_bad=0,window_bad=0,total_bad=0;double error_window=now();
 while(!atomic_load(&stopping)&&!action){
  if(!eof){int rc=av_read_frame(af,p);if(rc<0){if(rc!=AVERROR_EOF&&!action){fail("audio read");break;}eof=1;}
   else if(p->stream_index!=audio_index){av_packet_unref(p);continue;}}
  int rc=avcodec_send_packet(ac,eof?NULL:p);
  if(rc==AVERROR_INVALIDDATA&&!eof){
   if(now()-error_window>60){error_window=now();window_bad=0;}
   consecutive_bad++;window_bad++;total_bad++;
   fprintf(stderr,"CAST_AUDIO_BAD_PACKET code=%d pts=%lld pos=%lld bytes=%d consecutive=%d minute=%d total=%d\n",
     rc,(long long)p->pts,(long long)p->pos,p->size,consecutive_bad,window_bad,total_bad);
   if(total_bad==1&&p->size>0&&p->size<=65536){
    FILE *bad=fopen("/tmp/doveos-youtube-live/audio-error.bin","wb");
    if(bad){fwrite(p->data,1,p->size,bad);fclose(bad);}
   }
   av_packet_unref(p);
   if(consecutive_bad>3||window_bad>10||total_bad>64){fail("repeated invalid audio packets");break;}
   avcodec_flush_buffers(ac);continue;
  }
  av_packet_unref(p);
  if(rc<0&&rc!=AVERROR_EOF){fprintf(stderr,"CAST_AUDIO_DECODE_CODE %d\n",rc);fail("audio packet decode");break;}
  consecutive_bad=0;
  while((rc=avcodec_receive_frame(ac,f))==0){
   if(output_audio_frame(f,&resampler,&next_pts)){if(!action&&!atomic_load(&stopping))fail("audio playback");break;}
   av_frame_unref(f);if(atomic_load(&stopping)||action)break;
  }
  if(eof||(limit_seconds>0&&next_pts>=target+limit_seconds))break;
 }
 if(!atomic_load(&stopping)&&!action){
  /* This legacy ALSA driver leaves nonblocking drain at EAGAIN. Finish the
   * scheduled samples plus one device buffer before closing, without looping
   * an unsupported drain operation or truncating the final sound. */
  double end=limit_seconds>0?fmin(next_pts,target+limit_seconds):next_pts;
  if(!wait_media(end+.12,0)&&audio_command(DA_STOP,0,NULL,0,NULL))fail("audio stop");
 }
end:swr_free(&resampler);av_frame_free(&f);av_packet_free(&p);atomic_store(&audio_finished,1);return NULL;
}
static void *receive_video(void *unused){
 (void)unused;double report=now();
 while(!atomic_load(&stopping)&&!action){
  rtk_msg m;rtk_shared_info info;int shared=-1;
  if(receive_fd(video_fd,&m,&shared)){if(!atomic_load(&stopping)&&!action)fail("video output channel");break;}
  close(shared); // Metadata only; client never maps or alters decoder pixels.
  if(m.magic==RTK_MAGIC&&m.type==RTK_DRAIN)break;
  if(m.magic!=RTK_MAGIC||m.type!=RTK_SHARED_FRAME||m.size>RTK_LIMIT||io(video_fd,&info,sizeof info,0)){fail("video metadata");break;}
  double pts=m.pts/1e6;
  if(pts+0.0005<target){if(video_control(RTK_RELEASE,m.flags))fail("preroll release");continue;}
  if(limit_seconds>0&&pts>=target+limit_seconds){video_control(RTK_RELEASE,m.flags);break;}
  while(!atomic_load(&audio_ready)&&!atomic_load(&stopping)&&!action)usleep(1000);
  start_clock();if(wait_media(pts,0))break;
  double t=now(),late=t-clock_origin()-pts;if(late>max_late)max_late=late;
  if(late>.100){dropped++;}
  else{
   if(video_control(RTK_DISPLAY,m.flags)){fail("presentation channel");break;}
   if(shown&&t-last_present>max_gap)max_gap=t-last_present;
   last_present=t;shown++;set_position(pts);
  }
  if(video_control(RTK_RELEASE,m.flags)){fail("buffer release channel");break;}
  if(t-report>=1){printf("PLAYER_VIDEO position=%.3f shown=%u dropped=%u late_ms=%.3f\n",get_position(),atomic_load(&shown),atomic_load(&dropped),late*1000);report=t;}
 }
 atomic_store(&video_finished,1);return NULL;
}
static int open_keyboard(void){
 for(int i=0;i<16;i++){char path[64],name[128]={0};snprintf(path,sizeof path,"/dev/input/event%d",i);
  int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(fd<0)continue;
  unsigned long bits[(KEY_MAX+8*sizeof(long))/(8*sizeof(long))];memset(bits,0,sizeof bits);
  if(ioctl(fd,EVIOCGNAME(sizeof name),name)>=0&&ioctl(fd,EVIOCGBIT(EV_KEY,sizeof bits),bits)>=0&&
     (bits[KEY_A/(8*sizeof(long))]&(1UL<<(KEY_A%(8*sizeof(long)))))&&
     (bits[KEY_SPACE/(8*sizeof(long))]&(1UL<<(KEY_SPACE%(8*sizeof(long)))))){
   if(!ioctl(fd,EVIOCGRAB,1)){printf("PLAYER_KEYBOARD %s %s\n",path,name);return fd;}}
  close(fd);
 }return -1;
}
static unsigned grab_pointers(int *fds){
 unsigned count=0;
 for(int i=0;i<16;i++){
  char path[64];snprintf(path,sizeof path,"/dev/input/event%d",i);
  int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(fd<0)continue;
  struct input_id id;unsigned long bits[(KEY_MAX+8*sizeof(long))/(8*sizeof(long))]={0};
  if(!ioctl(fd,EVIOCGID,&id)&&id.bustype==BUS_USB&&ioctl(fd,EVIOCGBIT(EV_KEY,sizeof bits),bits)>=0&&
    (bits[BTN_LEFT/(8*sizeof(long))]&(1UL<<(BTN_LEFT%(8*sizeof(long)))))&&!ioctl(fd,EVIOCGRAB,1)){
   fds[count++]=fd;printf("PLAYER_POINTER_GRABBED %s\n",path);
  }else close(fd);
 }return count;
}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IOLBF,0);sigset_t clear;sigemptyset(&clear);sigprocmask(SIG_SETMASK,&clear,NULL);signal(SIGPIPE,SIG_IGN);
 struct sigaction sa={.sa_handler=signal_action};sigemptyset(&sa.sa_mask);
 sigaction(SIGINT,&sa,NULL);sigaction(SIGTERM,&sa,NULL);sigaction(SIGUSR1,&sa,NULL);sigaction(SIGUSR2,&sa,NULL);
 if(argc<2){puts("DovePlayer: FILE [START_SECONDS] [TEST_SECONDS] [--silent]");return 2;}
 const char *au=getenv("DOVE_YT_AUDIO");
 if(!cast_loopback_url(argv[1])||!cast_loopback_url(au)){
  puts("CAST_INPUT_ERROR private loopback URLs required");return 2;}
 char *resolved=strdup(argv[1]);audio_filename=au;
 if(!resolved)return 2;
 cast_update_volume();
 filename=resolved;target=argc>2?strtod(argv[2],NULL):0;limit_seconds=argc>3?strtod(argv[3],NULL):0;
 if(!isfinite(target)||target<0||!isfinite(limit_seconds)||limit_seconds<0){free(resolved);return 2;}
 if(argc>4&&!strcmp(argv[4],"--silent"))use_audio=0;
 av_log_set_level(AV_LOG_WARNING);
 if(open_file(&vf)){fail("cannot read this media file");goto finish;}
 duration=vf->duration>0?(double)vf->duration/AV_TIME_BASE:0;
 media_zero=vf->start_time==AV_NOPTS_VALUE?0:(double)vf->start_time/AV_TIME_BASE;
 video_index=av_find_best_stream(vf,AVMEDIA_TYPE_VIDEO,-1,-1,NULL,0);
 audio_index=use_audio?0:-1;
 if(video_index<0&&audio_index<0){fail("no supported media streams");goto finish;}
 if(duration&&target>=duration)target=0;
 position=target;save_state("Starting");
 if(video_index>=0){
  AVCodecParameters *v=vf->streams[video_index]->codecpar;
  int profile=v->profile&~FF_PROFILE_H264_CONSTRAINED;
  AVRational rate=av_guess_frame_rate(vf,vf->streams[video_index],NULL);
  if(v->codec_id!=AV_CODEC_ID_H264||v->width<=0||v->height<=0||v->width>1280||v->height>720||(v->width&15)||(v->height&1)||v->bits_per_raw_sample>8||abs(v->width*9-v->height*16)>32||av_q2d(rate)>60.01||
     (profile!=FF_PROFILE_H264_BASELINE&&profile!=FF_PROFILE_H264_MAIN&&profile!=FF_PROFILE_H264_EXTENDED&&profile!=FF_PROFILE_H264_HIGH)||
     (v->format!=AV_PIX_FMT_YUV420P&&v->format!=AV_PIX_FMT_YUVJ420P)){
   fail("Use a 16:9 H.264 video at 720p or lower, up to 60 fps (8-bit 4:2:0). Higher resolutions are not enabled in this preview.");goto finish;}
  const AVBitStreamFilter *filter=av_bsf_get_by_name("h264_mp4toannexb");
  if(!filter||av_bsf_alloc(filter,&bsf)<0||avcodec_parameters_copy(bsf->par_in,v)<0){fail("video converter");goto finish;}
  bsf->time_base_in=vf->streams[video_index]->time_base;
  if(av_bsf_init(bsf)<0||seek_file(vf)<0){fail("video seek");goto finish;}
 }
 if(audio_index>=0){
  if(open_file(&af)||seek_file(af)<0){fail("audio source");goto finish;}
  audio_index=av_find_best_stream(af,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0);
  if(audio_index<0){fail("audio stream unavailable");goto finish;}
  AVCodecParameters *p=af->streams[audio_index]->codecpar;AVCodec *decoder=avcodec_find_decoder(p->codec_id);
  ac=decoder?avcodec_alloc_context3(decoder):NULL;
  if(!ac||avcodec_parameters_to_context(ac,p)<0||avcodec_open2(ac,decoder,NULL)<0){fail("unsupported audio codec");goto finish;}
  audio_fd=connect_local(DA_SOCKET);if(audio_fd<0){fail("audio service unavailable");goto finish;}
 }else{atomic_store(&audio_ready,1);atomic_store(&audio_finished,1);}
 if(video_index>=0){
  AVCodecParameters *v=vf->streams[video_index]->codecpar;
  video_fd=connect_local(RTK_SOCKET);rtk_msg m={.magic=RTK_MAGIC,.type=RTK_INIT,.width=v->width,.height=v->height,.flags=((v->width>1280?1280:v->width)<<16)|(v->height>720?720:v->height)};
  if(video_fd<0||io(video_fd,&m,sizeof m,1)||receive_fd(video_fd,&m,&control_fd)||m.type!=RTK_ACK){fail("hardware decoder startup");goto finish;}
  printf("PLAYER_HARDWARE H264 %dx%d output_max=1280x720 CPU_video_decode=0\n",v->width,v->height);
 }else atomic_store(&video_finished,1);
 FILE *pidfile=fopen("/tmp/doveplayer/core.pid","w");if(pidfile){fprintf(pidfile,"%ld\n",(long)getpid());fclose(pidfile);}
 pthread_t at,vt,rt;int a_started=0,v_started=0,r_started=0;
 if(audio_index>=0){if(pthread_create(&at,NULL,audio_loop,NULL)){fail("audio thread");}else a_started=1;}
 if(video_index>=0&&!atomic_load(&failed)){
  if(pthread_create(&rt,NULL,receive_video,NULL))fail("display thread");else r_started=1;
  if(pthread_create(&vt,NULL,send_video,NULL))fail("video feed thread");else v_started=1;
 }
 int keyboard=video_index>=0?open_keyboard():-1,pointers[16];
 unsigned pointer_count=video_index>=0?grab_pointers(pointers):0;
 double beginning=now(),report=0;
 if(video_index>=0&&keyboard<0)fail("fullscreen keyboard unavailable");
 save_state("Playing");
 while(!atomic_load(&stopping)&&!action){
  if(atomic_load(&video_finished)&&atomic_load(&audio_finished))break;
  if(keyboard>=0){struct input_event ev;while(read(keyboard,&ev,sizeof ev)==sizeof ev)if(ev.type==EV_KEY&&ev.value==1){
   if(ev.code==KEY_ESC||ev.code==KEY_Q)action=20;
   else if(ev.code==KEY_SPACE)action=21;
  }}
  if(now()-beginning>(limit_seconds>0?limit_seconds+45:duration+90)&&!action){fail("playback deadline");break;}
  if(now()-report>=.25){
   cast_update_volume();
   save_state("Playing");report=now();}
  usleep(10000);
 }
 atomic_store(&stopping,1);
 if(video_fd>=0)shutdown(video_fd,SHUT_RDWR);
 if(control_fd>=0)shutdown(control_fd,SHUT_RDWR);
 if(audio_fd>=0)shutdown(audio_fd,SHUT_RDWR);
 if(v_started)pthread_join(vt,NULL);
 if(r_started)pthread_join(rt,NULL);
 if(a_started)pthread_join(at,NULL);
 if(keyboard>=0){ioctl(keyboard,EVIOCGRAB,0);close(keyboard);}
 for(unsigned i=0;i<pointer_count;i++){ioctl(pointers[i],EVIOCGRAB,0);close(pointers[i]);}
 unlink("/tmp/doveplayer/core.pid");
finish:
 if(video_fd>=0)close(video_fd);
 if(control_fd>=0)close(control_fd);
 if(audio_fd>=0)close(audio_fd);
 av_bsf_free(&bsf);avcodec_free_context(&ac);avformat_close_input(&vf);avformat_close_input(&af);free(resolved);
 printf("PLAYER_RESULT shown=%u dropped=%u packets=%u audio_blocks=%u max_gap_ms=%.3f max_late_ms=%.3f position=%.3f failed=%d action=%d\n",shown,dropped,packets,audio_blocks,max_gap*1000,max_late*1000,position,atomic_load(&failed),action);
 save_state(atomic_load(&failed)?"Error":action==21?"Paused":action?"Stopped":"Finished");
 return atomic_load(&failed)?1:action==21?21:0;
}
