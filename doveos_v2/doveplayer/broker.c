/* Experimental local broker: valid NV12 IPC plus clocked original-buffer VOUT.
 * Separate control fd prevents compressed-input backpressure from blocking release. */
#define _GNU_SOURCE
#include <stdint.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <dirent.h>
#include "video-ipc.h"
static int drain_sent;
static int ended,client_fd=-1,control_fd=-1,claimed;
static uint64_t LiGetMicroseconds(void);
#include "hw_output.h"
#include "decoder.h"
typedef struct {OMX_BUFFERHEADERTYPE *buffer;hw_vo_buffer image;uint32_t token;int queued,released;} direct_slot;
static direct_slot held[8];
static unsigned generations[8],frames,recycles;
static unsigned display_width,display_height;
static pthread_mutex_t slots_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_t control_thread;
static double first_present,last_present,max_gap;
static double first_copy,last_copy;
static unsigned requests,valid_requests,releases;
static uint64_t LiGetMicroseconds(void){return (uint64_t)(seconds()*1000000.0);}
static int full_io(int fd,void *p,size_t n,int sending){
 while(n){ssize_t k=sending?send(fd,p,n,MSG_NOSIGNAL):recv(fd,p,n,0);
  if(k<0&&errno==EINTR)continue;if(k<=0)return -1;p=(char*)p+k;n-=k;}return 0;
}
static void stop_session(int signal_number){(void)signal_number;output_stop=1;ended=1;if(client_fd>=0)shutdown(client_fd,SHUT_RDWR);}
static void release_display(void){
 if(!claimed)return;
 FILE *file=fopen("/tmp/doveos-direct-display","r");long owner=0;
 if(file){if(fscanf(file,"%ld",&owner)!=1)owner=0;fclose(file);}
 if(owner==(long)getpid())unlink("/tmp/doveos-direct-display");
 claimed=0;puts("DOVEPLAYER_DISPLAY_RELEASED");
}
static int claim_display(void){
 int fd=open("/tmp/doveos-direct-display",O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return -1;
 char text[32];int n=snprintf(text,sizeof text,"%ld\n",(long)getpid());
 if(write(fd,text,n)!=n){close(fd);unlink("/tmp/doveos-direct-display");return -1;}
 close(fd);claimed=1;
 for(int i=0;i<100&&!output_stop;i++){
  FILE *file=fopen("/tmp/doveos-direct-display.ready","r");long owner=0;
  if(file){if(fscanf(file,"%ld",&owner)!=1)owner=0;fclose(file);}
  if(owner==(long)getpid()){puts("DOVEPLAYER_DISPLAY_ACQUIRED");return output_open();}
  delay_ms(100);
 }return -1;
}
static int send_control_fd(int fd,rtk_msg *m,int control){
 char ancillary[CMSG_SPACE(sizeof(int))]={0};struct iovec iov={m,sizeof *m};
 struct msghdr msg={.msg_iov=&iov,.msg_iovlen=1,.msg_control=ancillary,.msg_controllen=sizeof ancillary};
 struct cmsghdr *h=CMSG_FIRSTHDR(&msg);h->cmsg_level=SOL_SOCKET;h->cmsg_type=SCM_RIGHTS;h->cmsg_len=CMSG_LEN(sizeof(int));memcpy(CMSG_DATA(h),&control,sizeof control);
 ssize_t n;do n=sendmsg(fd,&msg,MSG_NOSIGNAL);while(n<0&&errno==EINTR);
 if(n<=0)return -1;
 return (size_t)n<sizeof *m?full_io(fd,(char*)m+n,sizeof *m-n,1):0;
}
static void recycle_slots(void){
 pthread_mutex_lock(&slots_lock);
 for(int i=0;i<8;i++)if(held[i].buffer&&held[i].released&&(!held[i].queued||vo_status(i)==3)){
  OMX_BUFFERHEADERTYPE *b=held[i].buffer;held[i].buffer=NULL;
  b->nFilledLen=0;b->nOffset=0;b->nFlags=0;
  if(component->FillThisBuffer(component,b)!=OMX_ErrorNone)hw_failed=1;
  recycles++;
 }
 pthread_mutex_unlock(&slots_lock);
}
static int process_control(rtk_msg *m){
 if(m->magic!=RTK_MAGIC||(m->type!=RTK_DISPLAY&&m->type!=RTK_RELEASE)||m->size)return -1;
 int slot=m->flags&7;
 pthread_mutex_lock(&slots_lock);
 int valid=held[slot].buffer&&held[slot].token==m->flags;
 if(m->type==RTK_DISPLAY){requests++;if(valid)valid_requests++;}
 else releases++;
 if(requests<10&&m->type==RTK_DISPLAY)printf("DIRECT_TOKEN token=%u current=%u valid=%d queued=%d released=%d\n",m->flags,held[slot].token,valid,held[slot].queued,held[slot].released);
 pthread_mutex_unlock(&slots_lock);
 if(!valid)return 0; /* Already recycled generation; do not touch another frame. */
 if(m->type==RTK_DISPLAY&&!claimed&&claim_display())return -1;
 pthread_mutex_lock(&slots_lock);
 direct_slot *s=&held[slot];
 if(s->buffer&&s->token==m->flags){
  if(m->type==RTK_RELEASE)s->released=1;
  else if(!s->queued&&!s->released){
   if(!output_queue(&s->image,slot)){pthread_mutex_unlock(&slots_lock);return -1;}
   s->queued=1;double now=seconds();if(!displayed)first_present=now;else if(now-last_present>max_gap)max_gap=now-last_present;
   last_present=now;displayed++;
   if(displayed==1||displayed%120==0)printf("DOVEPLAYER_QUEUED count=%u pts=%lld\n",displayed,(long long)s->buffer->nTimeStamp);
  }
 }
 pthread_mutex_unlock(&slots_lock);return 0;
}
static void *control_loop(void *unused){
 (void)unused;rtk_msg message;size_t have=0;
 double last_report=seconds();
 while(!output_stop&&!hw_failed){
  if(seconds()-last_report>1){
   last_report=seconds();pthread_mutex_lock(&slots_lock);
   printf("DIRECT_SLOT_STATE time=%.3f copies=%u queued=%u recycled=%u returned=%d ready=%u/%u",last_report,frames,displayed,recycles,returned,ready_read,ready_write);
   for(int i=0;i<8;i++)if(held[i].buffer)printf(" slot%d:t%u,q%d,r%d,v%d",i,held[i].token,held[i].queued,held[i].released,held[i].queued?vo_status(i):-1);
   puts("");pthread_mutex_unlock(&slots_lock);
  }
  ssize_t n=recv(control_fd,(char*)&message+have,sizeof message-have,MSG_DONTWAIT);
  if(n>0){have+=n;if(have==sizeof message){if(process_control(&message)){hw_failed=1;break;}have=0;}}
  else if(n==0){output_stop=1;break;}
  else if(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR){hw_failed=1;break;}
  recycle_slots();if(n<=0)delay_ms(1);
 }
 shutdown(client_fd,SHUT_RDWR);
 printf("DOVEPLAYER_SUMMARY copies=%u queued=%u recycled=%u queue_fps=%.3f max_gap_ms=%.3f failed=%d\n",frames,displayed,recycles,displayed>1?(displayed-1)/(last_present-first_present):0,max_gap*1000,hw_failed);
 printf("DOVEPLAYER_TIMING copy_fps=%.3f requests=%u valid=%u releases=%u\n",frames>1?(frames-1)/(last_copy-first_copy):0,requests,valid_requests,releases);
 return NULL;
}
static int exports[8]={-1,-1,-1,-1,-1,-1,-1,-1};
static uint32_t export_bytes[8];
static int export_own_buffer(OMX_BUFFERHEADERTYPE *b,unsigned slot){
 if(exports[slot]>=0)return exports[slot];
 uint32_t expected=(uint32_t)(uintptr_t)b->pPlatformPrivate;
 if(!expected)return -1;
 DIR *dir=opendir("/proc/self/fd");struct dirent *entry;int shared=-1;
 if(!dir)return -1;
 while((entry=readdir(dir))&&shared<0){
  char path[300],target[256];snprintf(path,sizeof path,"/proc/self/fd/%s",entry->d_name);
  ssize_t n=readlink(path,target,sizeof target-1);if(n<=0)continue;target[n]=0;
  if(strcmp(target,"/dev/ion"))continue;
  int owner=atoi(entry->d_name);
  for(unsigned handle=1;handle<=32;handle++){
   struct {uint32_t handle,address,length,reserved[2];} physical={handle,0,0,{0,0}};
   if(ioctl(owner,0xc00c4908,&physical)||physical.address!=expected||physical.length<b->nAllocLen||physical.length>RTK_LIMIT)continue;
   struct {uint32_t handle;int fd;} share={handle,-1};
   if(ioctl(owner,0xc0084904,&share))continue;
   fcntl(share.fd,F_SETFD,FD_CLOEXEC);
   void *mapped=mmap(NULL,physical.length,PROT_READ,MAP_SHARED,share.fd,0);
   if(mapped==MAP_FAILED){close(share.fd);continue;}
   int equal=!memcmp(mapped,b->pBuffer,64);munmap(mapped,physical.length);
   if(!equal){close(share.fd);continue;}
   exports[slot]=shared=share.fd;export_bytes[slot]=physical.length;
   printf("SHARED_OMX_EXPORT slot=%u bytes=%u verified=1\n",slot,physical.length);break;
  }
 }
 closedir(dir);return shared;
}
static void *output_loop(void *unused){
 (void)unused;
 while(!output_stop&&!hw_failed){
  if(port_change&&!configured){
   __atomic_store_n(&ready_read,__atomic_load_n(&ready_write,__ATOMIC_ACQUIRE),__ATOMIC_RELEASE);
   if(resize_output()){hw_failed=1;break;}
  }
  OMX_BUFFERHEADERTYPE *b=pop_output();if(!b){
   if(!drain_sent&&__atomic_load_n(&eos_seen,__ATOMIC_ACQUIRE)&&exports[0]>=0){
    rtk_msg finish={.magic=RTK_MAGIC,.type=RTK_DRAIN};
    if(send_control_fd(client_fd,&finish,exports[0])){hw_failed=1;break;}
    drain_sent=1;printf("DIRECT_DRAIN_MARKER frames=%u queued=%u\n",frames,displayed);
   }
   delay_ms(1);continue;
  }
  if(!configured){b->nFilledLen=0;component->FillThisBuffer(component,b);continue;}
  int slot=-1;for(unsigned i=0;i<ports[1].nBufferCountActual;i++)if(out[i]==b)slot=i;
  size_t allocation=(size_t)output_stride*output_slice*3/2,input_bytes=(size_t)hw_width*hw_height*3/2;
  if(slot<0||b->nFilledLen<input_bytes||b->nOffset+allocation>b->nAllocLen){hw_failed=1;break;}
  int chroma_start=b->nFilledLen==input_bytes?hw_height:output_slice;
  int shared=export_own_buffer(b,slot);if(shared<0){puts("SHARED_OMX_EXPORT_FAILED");hw_failed=1;break;}
  pthread_mutex_lock(&slots_lock);
  if(held[slot].buffer){pthread_mutex_unlock(&slots_lock);hw_failed=1;break;}
  uint32_t token=(++generations[slot]<<3)|(unsigned)slot;
  held[slot]=(direct_slot){.buffer=b,.token=token,.image={.data=b->pBuffer+b->nOffset,.physical=(uint32_t)(uintptr_t)b->pPlatformPrivate+b->nOffset,.bytes=allocation,.width=hw_width,.height=hw_height,.stride=output_stride,.aligned_height=chroma_start}};
  pthread_mutex_unlock(&slots_lock);
  rtk_msg m={.magic=RTK_MAGIC,.type=RTK_SHARED_FRAME,.size=export_bytes[slot],.width=hw_width,.height=hw_height,.flags=token,.pts=b->nTimeStamp};
  rtk_shared_info info={b->nOffset,output_stride,output_stride*chroma_start};
  if(send_control_fd(client_fd,&m,shared)||full_io(client_fd,&info,sizeof info,1)){hw_failed=1;break;}
  if(!frames)first_copy=seconds();last_copy=seconds();frames++;
 }
 for(unsigned i=0;i<8;i++)if(exports[i]>=0)close(exports[i]);
 shutdown(client_fd,SHUT_RDWR);return NULL;
}
static int submit_packet(const uint8_t *data,size_t size,int64_t pts,int eos){
 /* Use only buffers the OMX component already allocated. Ownership returns
  * in EmptyBufferDone; never reuse a compressed input still owned by OMX. */
 double end=seconds()+40;
 while(seconds()<end&&!hw_failed&&!output_stop){
  for(unsigned i=0;i<ports[0].nBufferCountActual;i++){
   int expected=0;
   if(!__atomic_compare_exchange_n(&input_busy[i],&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))continue;
   OMX_BUFFERHEADERTYPE *b=in[i];
   if(size>b->nAllocLen){__atomic_store_n(&input_busy[i],0,__ATOMIC_RELEASE);return -1;}
   if(size)memcpy(b->pBuffer,data,size);
   b->nOffset=0;b->nFilledLen=size;b->nTimeStamp=pts;
   b->nFlags=OMX_BUFFERFLAG_ENDOFFRAME|(eos?OMX_BUFFERFLAG_EOS:0);
   if(component->EmptyThisBuffer(component,b)==OMX_ErrorNone)return 0;
   __atomic_store_n(&input_busy[i],0,__ATOMIC_RELEASE);return -1;
  }
  struct timespec t;clock_gettime(CLOCK_REALTIME,&t);t.tv_nsec+=2000000;
  if(t.tv_nsec>=1000000000){t.tv_sec++;t.tv_nsec-=1000000000;}
  if(sem_timedwait(&input_done,&t)&&errno!=ETIMEDOUT&&errno!=EINTR)return -1;
 }
 return -1;
}
static void close_omx(void){
 if(!component)return;component->SendCommand(component,OMX_CommandStateSet,OMX_StateIdle,NULL);if(wait_state(OMX_StateIdle))return;
 component->SendCommand(component,OMX_CommandStateSet,OMX_StateLoaded,NULL);
 for(unsigned p=0;p<2;p++)for(unsigned i=0;i<ports[p].nBufferCountActual;i++){OMX_BUFFERHEADERTYPE *b=p?out[i]:in[i];if(b)component->FreeBuffer(component,p,b);}
 if(!wait_state(OMX_StateLoaded))component->ComponentDeInit(component);
}
static void session(int fd){
 client_fd=fd;signal(SIGTERM,stop_session);signal(SIGINT,stop_session);alarm(0);rtk_msg m;
 if(full_io(fd,&m,sizeof m,0)||m.magic!=RTK_MAGIC||m.type!=RTK_INIT||!m.width||!m.height||m.width>1920||m.height>1088||(m.width&1)||(m.height&1))return;
 hw_width=m.width;hw_height=m.height;display_width=m.flags>>16;display_height=m.flags&65535;
 if(!display_width||!display_height||display_width>1280||display_height>720||display_width>(unsigned)hw_width||display_height>(unsigned)hw_height||(display_width&1)||(display_height&1))return;
 int controls[2];if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,controls))return;
 control_fd=controls[0];struct timeval timeout={40,0};setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
 if(omx_backend_init()){close(controls[0]);close(controls[1]);return;}
 /* Finish display handoff before VLC starts its playback clock. Doing this on
  * the first scheduled frame makes subsequent 60 fps frames late and dropped. */
 if(claim_display()){output_close();release_display();close(controls[0]);close(controls[1]);alarm(8);close_omx();return;}
 m.type=RTK_ACK;if(send_control_fd(fd,&m,controls[1])){output_close();release_display();close(controls[0]);close(controls[1]);alarm(8);close_omx();return;}close(controls[1]);
 if(pthread_create(&control_thread,NULL,control_loop,NULL)){output_close();release_display();close(control_fd);alarm(8);close_omx();return;}
 if(pthread_create(&output_thread,NULL,output_loop,NULL)){output_stop=1;shutdown(control_fd,SHUT_RDWR);pthread_join(control_thread,NULL);output_close();release_display();close(control_fd);alarm(8);close_omx();return;}
 uint8_t *packet=malloc(RTK_LIMIT);
 if(packet)while(!hw_failed&&!output_stop&&!full_io(fd,&m,sizeof m,0)){
  if(m.magic!=RTK_MAGIC||m.size>RTK_LIMIT||(m.type!=RTK_PACKET&&m.type!=RTK_DRAIN))break;
  if(m.size&&full_io(fd,packet,m.size,0))break;
  if(submit_packet(packet,m.size,m.pts,m.type==RTK_DRAIN))break;
 }
 output_stop=1;shutdown(fd,SHUT_RDWR);shutdown(control_fd,SHUT_RDWR);
 pthread_join(output_thread,NULL);pthread_join(control_thread,NULL);free(packet);close(control_fd);
 output_close();release_display();
 alarm(8);close_omx();alarm(0);puts("DOVEPLAYER_SESSION_CLOSED");
}
int main(void){
 setvbuf(stdout,NULL,_IOLBF,0);sigset_t mask;sigemptyset(&mask);sigprocmask(SIG_SETMASK,&mask,NULL);signal(SIGCHLD,SIG_DFL);signal(SIGPIPE,SIG_IGN);
 int server=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(server<0)return 2;
 struct sockaddr_un addr={.sun_family=AF_UNIX};strcpy(addr.sun_path,RTK_SOCKET);unlink(RTK_SOCKET);
 if(bind(server,(struct sockaddr*)&addr,sizeof addr)||chown(RTK_SOCKET,65534,29)||chmod(RTK_SOCKET,0600)||listen(server,2))return 3;
 puts("DOVEPLAYER_BROKER_READY");
 for(;;){int fd=accept4(server,NULL,NULL,SOCK_CLOEXEC);if(fd<0){if(errno==EINTR)continue;return 4;}
  struct ucred peer;socklen_t length=sizeof peer;if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&length)||(peer.uid!=0&&peer.uid!=65534)){close(fd);continue;}
  pid_t parent=getpid(),child=fork();if(child==0){prctl(PR_SET_PDEATHSIG,SIGTERM);if(getppid()!=parent)_exit(1);close(server);session(fd);close(fd);_exit(0);}close(fd);
  if(child>0){FILE *pidfile=fopen("/tmp/doveplayer/broker-child.pid","w");if(pidfile){fprintf(pidfile,"%ld\n",(long)child);fclose(pidfile);}int status;while(waitpid(child,&status,0)<0&&errno==EINTR){}printf("SHARED_SESSION_REAPED status=%d\n",WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status));unlink("/tmp/doveplayer/broker-child.pid");}
 }
}
