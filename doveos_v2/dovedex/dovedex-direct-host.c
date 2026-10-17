/* SPDX-License-Identifier: GPL-2.0-or-later
 * scrcpy H.264 packets -> stock Realtek OMX -> Moonlight's direct VOUT ABI.
 * No RGB conversion, GTK video painting, or firmware writes. */
#define _GNU_SOURCE
#include <stdint.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include "vlc_rtk_protocol.h"
static int ended,client_fd=-1;
static uint64_t LiGetMicroseconds(void);
#include "hw_output.h"
#include "vlc_rtk_omx.h"
static OMX_BUFFERHEADERTYPE *held[8];
static unsigned released,packets;
static double first_frame,last_frame,latency_total,latency_max,max_gap;
static unsigned late_gaps;
static double input_last,input_max_gap,submit_max;
static unsigned input_stalls;
typedef struct DoveNal { unsigned char *data;int length;struct DoveNal *next; } *PLENTRY;
typedef struct {int frameNumber;uint64_t enqueueTimeUs;PLENTRY bufferList;} DoveUnit,*PDECODE_UNIT;
#define DR_OK 0
#define DR_NEED_IDR -1
#include "hevc_backend.h"
static uint64_t LiGetMicroseconds(void){return (uint64_t)(seconds()*1000000.0);}
static int full_io(int fd,void *p,size_t n,int sending){
 while(n){ssize_t k=sending?send(fd,p,n,MSG_NOSIGNAL):recv(fd,p,n,0);
  if(k<0&&errno==EINTR)continue;
  if(k<=0)return -1;
  p=(char*)p+k;n-=(size_t)k;
 }return 0;
}
static void recycle(void){
 for(unsigned i=0;i<8;i++)if(held[i]&&vo_status(i)==3){
  OMX_BUFFERHEADERTYPE *b=held[i];held[i]=NULL;b->nFilledLen=0;b->nOffset=0;b->nFlags=0;
  if(component->FillThisBuffer(component,b)!=OMX_ErrorNone){hw_failed=1;break;}
  released++;
 }
}
static void *output_loop(void *unused){
 (void)unused;
 while(!output_stop&&!hw_failed){
  if(hw_hevc){
   unsigned previous=displayed;hevc_pump();
   if(displayed>previous){
    if(!first_frame)first_frame=hevc.first;
    last_frame=hevc.last;
    rtk_msg ack={.magic=RTK_MAGIC,.type=RTK_FRAME,.width=hw_width,.height=hw_height};
    if(full_io(client_fd,&ack,sizeof ack,1)){hw_failed=1;break;}
   }
   delay_ms(1);continue;
  }
  if(port_change&&!configured){
   __atomic_store_n(&ready_read,__atomic_load_n(&ready_write,__ATOMIC_ACQUIRE),__ATOMIC_RELEASE);
   if(resize_output()){hw_failed=1;break;}
  }
  recycle();OMX_BUFFERHEADERTYPE *b=pop_output();
  if(!b){delay_ms(1);continue;}
  if(!configured){b->nFilledLen=0;component->FillThisBuffer(component,b);continue;}
  int slot=-1;for(unsigned i=0;i<ports[1].nBufferCountActual;i++)if(out[i]==b)slot=(int)i;
  size_t bytes=(size_t)output_stride*output_slice*3/2;
  if(slot<0||b->nFilledLen<(unsigned)(hw_width*hw_height*3/2)||b->nOffset+bytes>b->nAllocLen){hw_failed=1;break;}
  uint32_t physical=(uint32_t)(uintptr_t)b->pPlatformPrivate;
  if(physical)physical+=b->nOffset;else physical=physical_buffer(b->pBuffer+b->nOffset,bytes);
  if(!physical){puts("DIRECT_PHYSICAL_MAPPING_FAILED");hw_failed=1;break;}
  hw_vo_buffer image={.data=b->pBuffer+b->nOffset,.physical=physical,.bytes=bytes,
   .width=hw_width,.height=hw_height,.stride=output_stride,.aligned_height=hw_height};
  int64_t submitted=b->nTimeStamp;
  if(!output_queue(&image,slot)){hw_failed=1;break;}
  held[slot]=b;recycle();double now=seconds();
  if(!displayed)first_frame=now;else{double gap=now-last_frame;if(gap>max_gap)max_gap=gap;if(gap>.050)late_gaps++;}
  last_frame=now;displayed++;
  double latency=now*1000000.0-submitted;if(latency>=0&&latency<10000000){latency_total+=latency;if(latency>latency_max)latency_max=latency;}
  rtk_msg ack={.magic=RTK_MAGIC,.type=RTK_FRAME,.width=hw_width,.height=hw_height,.pts=submitted};
  if(full_io(client_fd,&ack,sizeof ack,1)){hw_failed=1;break;}
  if(displayed==1||displayed%120==0)printf("DIRECT_DISPLAY frames=%u released=%u fps=%.2f receive_to_queue_ms=%.2f\n",displayed,released,displayed>1?(displayed-1)/(last_frame-first_frame):0,latency_total/displayed/1000);
 }
 printf("DIRECT_SUMMARY packets=%u displayed=%u fps=%.2f receive_to_queue_ms=%.3f max_ms=%.3f max_gap_ms=%.3f gaps50=%u failed=%d render_dropped=%u\n",
  packets,displayed,displayed>1?(displayed-1)/(last_frame-first_frame):0,displayed?latency_total/displayed/1000:0,latency_max/1000,(hw_hevc?hevc.max_gap:max_gap)*1000,hw_hevc?hevc.gaps50:late_gaps,hw_failed,render_dropped);
 printf("DIRECT_INPUT max_gap_ms=%.3f gaps50=%u max_submit_ms=%.3f\n",input_max_gap*1000,input_stalls,submit_max*1000);
 shutdown(client_fd,SHUT_RDWR);return NULL;
}
static int submit(const uint8_t *data,size_t size){
 if(hw_hevc){
  struct DoveNal nal={.data=(unsigned char*)data,.length=(int)size};
  DoveUnit unit={.frameNumber=(int)packets+1,.enqueueTimeUs=LiGetMicroseconds(),.bufferList=&nal};
  return hevc_decode(&unit)==DR_OK?0:-1;
 }
 if(!size||size>in[0]->nAllocLen)return -1;
 memcpy(in[0]->pBuffer,data,size);in[0]->nOffset=0;in[0]->nFilledLen=size;in[0]->nTimeStamp=LiGetMicroseconds();in[0]->nFlags=OMX_BUFFERFLAG_ENDOFFRAME;
 if(component->EmptyThisBuffer(component,in[0])!=OMX_ErrorNone)return -1;
 double until=seconds()+3;
 while(seconds()<until&&!hw_failed){struct timespec t;clock_gettime(CLOCK_REALTIME,&t);t.tv_nsec+=2000000;
  if(t.tv_nsec>=1000000000){t.tv_sec++;t.tv_nsec-=1000000000;}
  if(!sem_timedwait(&input_done,&t))return 0;
  if(errno!=EINTR&&errno!=ETIMEDOUT)return -1;
 }return -1;
}
static void close_decoder(void){
 if(!component)return;
 component->SendCommand(component,OMX_CommandStateSet,OMX_StateIdle,NULL);
 if(wait_state(OMX_StateIdle))return;
 component->SendCommand(component,OMX_CommandStateSet,OMX_StateLoaded,NULL);
 for(unsigned p=0;p<2;p++)for(unsigned i=0;i<ports[p].nBufferCountActual;i++){OMX_BUFFERHEADERTYPE *b=p?out[i]:in[i];if(b)component->FreeBuffer(component,p,b);}
 if(!wait_state(OMX_StateLoaded))component->ComponentDeInit(component);
}
static void session(int fd){
 client_fd=fd;alarm(15);rtk_msg m;
 if(full_io(fd,&m,sizeof m,0)||m.magic!=RTK_MAGIC||m.type!=RTK_INIT||!m.width||!m.height||m.width>1920||m.height>1088||(m.width&1)||(m.height&1))return;
 hw_width=m.width;hw_height=m.height;
 hw_hevc=m.flags==1;
 if(hw_hevc?hevc_open():(omx_backend_init()||output_open()))goto close_session;
 m.type=RTK_ACK;if(full_io(fd,&m,sizeof m,1))goto close_session;
 alarm(0);if(pthread_create(&output_thread,NULL,output_loop,NULL))goto close_session;
 uint8_t *packet=malloc(RTK_LIMIT);
 if(packet)while(!hw_failed&&!full_io(fd,&m,sizeof m,0)){
  if(m.magic!=RTK_MAGIC||m.type!=RTK_PACKET||!m.size||m.size>RTK_LIMIT||full_io(fd,packet,m.size,0))break;
  double arrived=seconds();
  if(input_last){double gap=arrived-input_last;if(gap>input_max_gap)input_max_gap=gap;if(gap>.050)input_stalls++;}
  input_last=arrived;
  int result=submit(packet,m.size);
  double submit_time=seconds()-arrived;if(submit_time>submit_max)submit_max=submit_time;
  if(result)break;
  packets++;
 }
 __atomic_store_n(&output_stop,1,__ATOMIC_RELEASE);shutdown(fd,SHUT_RDWR);pthread_join(output_thread,NULL);free(packet);
close_session:
 alarm(10);if(hw_hevc){hevc_close();}else{output_close();close_decoder();}alarm(0);puts("DIRECT_SESSION_CLOSED");
}
int main(void){
 setvbuf(stdout,NULL,_IOLBF,0);sigset_t mask;sigemptyset(&mask);sigprocmask(SIG_SETMASK,&mask,NULL);signal(SIGCHLD,SIG_IGN);signal(SIGPIPE,SIG_IGN);
 int server=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(server<0)return 2;
 struct sockaddr_un addr={.sun_family=AF_UNIX};strcpy(addr.sun_path,RTK_SOCKET);unlink(RTK_SOCKET);
 if(bind(server,(struct sockaddr*)&addr,sizeof addr)||chmod(RTK_SOCKET,0600)||listen(server,1))return 3;
 puts("DIRECT_BROKER_READY");
 for(;;){int fd=accept4(server,NULL,NULL,SOCK_CLOEXEC);if(fd<0){if(errno==EINTR)continue;return 4;}
  struct ucred peer;socklen_t len=sizeof peer;if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&len)||peer.uid!=0){close(fd);continue;}
  pid_t parent=getpid(),child=fork();
  if(child==0){prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(1);close(server);session(fd);close(fd);_exit(0);}
  close(fd);
 }
}
