/* Private RGB plane probe. ABI from exact stock ALSADaemon DWARF and GLDC
 * disassembly. No register writes or foreign buffer imports. Restores the
 * existing stock framebuffer using its unchanged view after relinquishing OSD1.
 * Parent retains all ION buffers until child confirms Stop + Destroy success.
 * GPU fence before display; reuse only after a newer display context is acknowledged. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/file.h>
#include <sys/prctl.h>
typedef struct {uint16_t x,y,w,h;} Rect;
typedef struct {uint32_t plane;Rect video,border;uint8_t color[4],enable,pad[3];} Config;
typedef struct {uint32_t agent,plane;uint8_t zero,realtime,pad[2];} DisplayCommand;
typedef struct {uint32_t handle,phys,bytes;int dma;void *cpu;} Buffer;
#include "rgb-session.h"
#include "watchdog-clock.h"
static RgbSession *session;static int session_fd;
static volatile sig_atomic_t stopping;
static void stop(int sig){stopping=sig;}
static void unblock_signals(void){sigset_t mask;sigemptyset(&mask);sigprocmask(SIG_SETMASK,&mask,NULL);signal(SIGCHLD,SIG_DFL);}
_Static_assert(sizeof(Config)==28,"config ABI");
_Static_assert(sizeof(DisplayCommand)==12,"display ABI");
static unsigned target_width=1280,target_height=720;static size_t frame_bytes;
static void *lib;static uint32_t client[5];static State *state;static uint32_t owner;static int lease_held;
static void *symbol(const char *n){void *p=dlsym(lib,n);if(!p){fprintf(stderr,"MISSING %s\n",n);_exit(3);}return p;}
static void rpc_init(void){
 lib=dlopen(getenv("DOVE_RPC_LIBRARY")?getenv("DOVE_RPC_LIBRARY"):"/usr/lib/libVOutWrapper.so",RTLD_LAZY|RTLD_LOCAL|RTLD_DEEPBIND);
 if(!lib){puts(dlerror());_exit(3);}
 void *(*reply)(void*)=symbol("ReplyHandler_register"),*(*audio)(void*)=symbol("AUDIO_AGENT_0_register");
 int (*init)(void*)=symbol("initRPCProxy");void (*prepare)(void*,uint32_t,uint32_t,uint32_t)=symbol("prepareCLNT");
 init(reply(audio(NULL)));prepare(client,7,201,0);
}
static int rpc(const char *name,void *arg){
 uint32_t *(*fn)(void*,void*)=symbol(name);printf("RGB_RPC_BEGIN %s\n",name);uint32_t *out=fn(arg,client);
 int ok=out&&out!=(void*)-1&&out[0]==0x10000000;
 printf("RGB_RPC %s status=%08x success=%d\n",name,out&&out!=(void*)-1?out[0]:0,ok);
 if(out&&out!=(void*)-1)free(out);
 return ok;
}
#include "stock-overlay-restore.inc"
static int cleanup(void){
 if(!state->created){
  state->safe=1;
  if(state->touched&&!state->restored){state->restored=restore_stock_overlay();}
  return state->restored||!state->touched;
 }
 uint32_t id=state->agent;if(!id)return 0;
 DisplayCommand d={.agent=id,.plane=255,.zero=1};
 int hidden=rpc("VIDEO_RPC_VO_FILTER_ToAgent_Display_0",&d);
 int stopped=rpc("VIDEO_RPC_ToAgent_Stop_0",&id);
 int destroyed=stopped&&rpc("VIDEO_RPC_ToAgent_Destroy_0",&id);
 printf("RGB_CLEANUP agent=%u detached=%d stopped=%d destroyed=%d\n",id,hidden,stopped,destroyed);
 if(destroyed){state->safe=1;state->created=0;}
 if(state->touched){state->restored=restore_stock_overlay();}
 else state->restored=1;
 printf("RGB_ORIGINAL_OWNER_RESTORED=%u owner=%08x\n",state->restored,state->owner);
 return destroyed&&state->restored;
}
static void query(unsigned plane){
 void *(*fn)(void*,void*)=symbol("VIDEO_RPC_VOUT_ToAgent_QueryDisplayWin_0");
 uint32_t *out=fn(&plane,client);printf("RGB_PLANE_QUERY plane=%u",plane);
 if(out&&out!=(void*)-1){int result=*(int16_t*)out;printf(" result=%d",result);if(result==1)for(int i=0;i<15;i++)printf(" %08x",out[i]);free(out);}puts("");
}
static uint32_t load(volatile uint32_t *p){__sync_synchronize();return ntohl(*p);}
static void store(volatile uint32_t *p,uint32_t n){*p=htonl(n);__sync_synchronize();}

static int wait_child(pid_t pid,int seconds);
/* After abnormal X exit, a previous frame fence is not sufficient evidence.
 * Require X to have been reaped and two independent idle scheduler snapshots. */
static int gpu_idle_once(void){
 char buf[8192]={0};FILE *f=fopen("/proc/1/root/sys/kernel/debug/mali/state_dump","r");if(!f)return 0;
 size_t n=fread(buf,1,sizeof buf-1,f);int complete=feof(f);fclose(f);if(n<100||!complete)return 0;
 if(strstr(buf,"state: 1")||strstr(buf,"state: 2")||!strstr(buf,"GP job:   (null)")||!strstr(buf,"PP job:   (null), subjob 0")||!strstr(buf,"PP job:   (null), subjob 1"))return 0;
 int empty=0;for(char *q=buf;(q=strstr(q,"Queue is empty"));q++)empty++;return empty==2;
}
static int retired_gpu_after_exit(void){
 if(!session->display_reaped)return 0;
 for(int i=0,consecutive=0;i<20;i++){
  consecutive=gpu_idle_once()?consecutive+1:0;
  if(consecutive==2){puts("RGB_ABNORMAL_EXIT_GPU_IDLE_CONFIRMED");return 1;}usleep(100000);
 }
 return 0;
}

static uint32_t clock_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint32_t)(t.tv_sec*1000u+t.tv_nsec/1000000u);}
static void marker(const char *path,unsigned value){char temp[180];snprintf(temp,sizeof temp,"%s.new",path);FILE *f=fopen(temp,"w");if(f){fprintf(f,"%u\n",value);if(!fclose(f))rename(temp,path);}}
static int activate(Buffer *b){
 memset(b[0].cpu,0,b[0].bytes);memset(b[1].cpu,0,b[1].bytes);
 uint32_t type=10,*(*create)(void*,void*)=symbol("VIDEO_RPC_ToAgent_Create_0");
 puts("RGB_RPC_BEGIN CREATE");uint32_t *out=create(&type,client);
 if(!out||out==(void*)-1||out[0]!=0x10000000||!out[1]){puts("RGB_CREATE_REJECT");if(out&&out!=(void*)-1)free(out);state->safe=1;return 0;}
 if(out[1]==owner){puts("OWNER_COLLISION_ABORT");free(out);return 0;}state->agent=out[1];state->created=1;state->safe=0;free(out);
 printf("RGB_AGENT_CREATED %u\n",state->agent);
 DisplayCommand d={.agent=state->agent,.plane=255,.zero=1};
 if(!rpc("VIDEO_RPC_VO_FILTER_ToAgent_Display_0",&d))return 0;
 void (*reset)(void*)=symbol("reset_refclock");reset(b[0].cpu);
 uint32_t ref[2]={state->agent,b[0].phys};
 if(!rpc("VIDEO_RPC_ToAgent_SetRefClock_0",ref))return 0;
 volatile uint32_t *head=b[1].cpu;uint32_t base=b[1].phys+4096,size=b[1].bytes-4096;
 store(head+1,base);store(head+2,size);store(head+3,1);store(head+4,base);store(head+5,1);
 for(int i=8;i<12;i++)store(head+i,base);
 uint32_t ring[4]={state->agent,0,0,b[1].phys};
 if(!rpc("VIDEO_RPC_ToAgent_InitRingBuffer_0",ring))return 0;
 uint32_t id=state->agent;if(!rpc("VIDEO_RPC_ToAgent_Run_0",&id))return 0;
 state->phase=2;
 /* Keep the existing OSD1 geometry/rescale; source matches stock 1280x720.
  * Temporarily detach the verified original agent without stopping it. */
 DisplayCommand original={.agent=state->owner,.plane=255,.zero=1};state->touched=1;state->restored=0;
 if(!rpc("VIDEO_RPC_VO_FILTER_ToAgent_Display_0",&original))return 0;
 d.plane=3;if(!rpc("VIDEO_RPC_VO_FILTER_ToAgent_Display_0",&d))return 0;
 state->phase=3;
 state->phase=5;

 session->epoch++;__sync_synchronize();return 1;
}
static void enter_desktop_root(void){
 unblock_signals();
 if(chroot("/tmp/doveos-v2-root")||chdir("/")){perror("RGB_CHROOT");_exit(8);}
 setenv("LD_LIBRARY_PATH","/tmp/doveos-gpu-desktop:/tmp/doveos-gpu-desktop/usr/lib/arm-linux-gnueabihf:/tmp/doveos-gpu-desktop/lib/arm-linux-gnueabihf:/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf",1);
 unsetenv("LD_PRELOAD");
}
static void child(Buffer *b,unsigned plane){
 (void)plane;session->owner_heartbeat=clock_ms();rpc_init();unblock_signals();state->phase=1;
 if(!activate(b)){cleanup();_exit(6);}
 pid_t display_child=fork();if(display_child<0){cleanup();_exit(6);}
 if(!display_child){
  for(int fd=3;fd<1024;fd++)fcntl(fd,F_SETFD,FD_CLOEXEC);
  for(int i=0;i<5;i++)fcntl(b[i].dma,F_SETFD,0);fcntl(session_fd,F_SETFD,0);
  char value[32];snprintf(value,sizeof value,"%d",session_fd);setenv("DOVE_RGB_SESSION_FD",value,1);
  enter_desktop_root();
  setenv("LD_PRELOAD","/tmp/doveos-gpu-desktop/gpu-dma-shim.so",1);
  setenv("DOVE_DIRECT_PRESENTER","/tmp/doveos-rgb/libgpu-rgb-present.so",1);
  setenv("DOVE_DIRECT_TEXTURE","1",1);setenv("DOVE_SMALL_PBUFFER","1",1);setenv("DOVE_EGL_DAMAGE","1",1);
  setenv("DOVE_RGB_CURSOR","1",1);setenv("DOVE_RGB_PACE60","1",1);setenv("DOVE_GPU_DESKTOP","1",1);setenv("DOVE_COPY_NATIVE","1",1);unsetenv("DOVE_RGB_NATIVE_COPY");setenv("DOVE_COMPOSITOR_OUTPUT","1",1);setenv("DOVE_COMPOSITOR_PREVIEW","1",1);setenv("DOVE_COMPOSITOR_LIBRARY","/tmp/doveos-rgb/libdove-compositor-bridge.so",1);setenv("DOVE_COMPOSITOR_SOCKET","/tmp/doveos-rgb/native-windows.sock",1);setenv("DOVE_NATIVE_MALI","/tmp/doveos-gpu-desktop/libMali-r4p1-native-trial.so",1);setenv("DOVE_ION_LIBRARY","/tmp/doveos-gpu-desktop/libVOutAllocator.so",1);unsetenv("DOVE_SHARED_ROOT_TRIAL");
  execl("/tmp/doveos-rgb/Xephyr-rgb-720","Xephyr-rgb-desktop",":7","-screen","1280x720","-dpi","100","-fullscreen","-sw-cursor","-glamor","-nolisten","tcp","-ac","-noreset",(char*)NULL);
  perror("RGB_X_EXEC");_exit(8);
 }
 session->display_pid=display_child;marker("/tmp/doveos-rgb-x.pid",display_child);
 pid_t cursor_child=fork();
 if(!cursor_child){
  for(int fd=3;fd<1024;fd++)fcntl(fd,F_SETFD,FD_CLOEXEC);fcntl(session_fd,F_SETFD,0);
  char fdarg[32],pidarg[32];snprintf(fdarg,sizeof fdarg,"%d",session_fd);snprintf(pidarg,sizeof pidarg,"%d",display_child);
  enter_desktop_root();setenv("DISPLAY",":7",1);
  execl("/tmp/doveos-rgb/rgb-720-cursor","rgb-720-cursor",fdarg,pidarg,(char*)NULL);_exit(8);
 }
 if(cursor_child>0)session->cursor_pid=cursor_child;
 printf("RGB_GUARDIAN_DISPLAY_CHILD=%d RPC_OWNER=%d\n",display_child,getpid());
 uint32_t started=clock_ms();int result=0,display_status=0;unsigned ack_owner=0,fresh_races=0;
 while(!stopping){
  uint32_t now=clock_ms();session->owner_heartbeat=now;
  if(waitpid(display_child,&display_status,WNOHANG)==display_child){session->display_reaped=1;result=WIFEXITED(display_status)?WEXITSTATUS(display_status):128+WTERMSIG(display_status);printf("RGB_OWNER_EXIT reason=x_exited code=%d uptime_ms=%u\n",result,now);break;}
  if(cursor_child<=0||kill(cursor_child,0)||session->errors){printf("RGB_OWNER_EXIT reason=cursor_or_presenter_error errors=%u uptime_ms=%u\n",session->errors,now);result=70;break;}
  uint32_t xbeat=session->heartbeat,cbeat=session->cursor_heartbeat;
  if((xbeat&&rgb_heartbeat_ahead(now,xbeat))||(cbeat&&rgb_heartbeat_ahead(now,cbeat))){
   if(++fresh_races==1)printf("RGB_WATCHDOG_FRESH_HEARTBEAT_RACE now=%u x=%u cursor=%u ignored=1\n",now,xbeat,cbeat);
  }
  if((uint32_t)(now-started)>12000&&(!session->ready||!xbeat||rgb_heartbeat_expired(now,xbeat,6000)||!cbeat||rgb_heartbeat_expired(now,cbeat,6000))){
   printf("RGB_WATCHDOG_X_OR_CURSOR_STALLED now=%u x=%u cursor=%u ready=%u\n",now,xbeat,cbeat,session->ready);result=71;break;
  }
  unsigned request=session->pause_request;
  if(request){
   if(!session->paused){
    /* Submission has stopped on the X thread before this request appeared. */
    session->paused=request;__sync_synchronize();
    if(!cleanup()){result=72;break;}
    puts("RGB_DESKTOP_DISPLAY_RELEASED");
   }
   if(session->paused!=request)session->paused=request;
   if(ack_owner!=request){
    marker("/tmp/doveos-gpu-display-paused",request);
    marker("/tmp/doveos-gpu-cursor-paused",request);
    marker("/tmp/doveos-direct-display.ready",request);ack_owner=request;
   }
  }else if(session->paused){
   if(!activate(b)){result=73;break;}
   unlink("/tmp/doveos-gpu-display-paused");unlink("/tmp/doveos-gpu-cursor-paused");unlink("/tmp/doveos-direct-display.ready");
   __sync_synchronize();session->paused=0;ack_owner=0;
  }
  usleep(25000);
 }
 printf("RGB_WATCHDOG_FRESH_RACES=%u\n",fresh_races);
 if(stopping)printf("RGB_OWNER_EXIT reason=signal signal=%d uptime_ms=%u\n",(int)stopping,clock_ms());
 if(!session->display_reaped){kill(display_child,SIGTERM);int r=wait_child(display_child,3);session->display_reaped=r!=125;if(r==125)result=125;}
 if(cursor_child>0){kill(cursor_child,SIGTERM);session->cursor_reaped=wait_child(cursor_child,2)!=125;}else session->cursor_reaped=1;
 int cleaned=cleanup();printf("RGB_DISPLAY_CHILD_RESULT=%d\n",result);_exit(cleaned?result:7);
}
static int wait_child(pid_t pid,int seconds){int status=0;for(int i=0;i<seconds*10;i++){if(waitpid(pid,&status,WNOHANG)==pid)return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);usleep(100000);}kill(pid,SIGTERM);for(int i=0;i<30;i++){if(waitpid(pid,&status,WNOHANG)==pid)return 124;usleep(100000);}kill(pid,SIGKILL);for(int i=0;i<20;i++){if(waitpid(pid,&status,WNOHANG)==pid)return 124;usleep(50000);}puts("WATCHDOG_CHILD_STILL_IN_KERNEL_BUFFERS_MUST_STAY_PINNED");return 125;}
static int pid_marker(const char *path){FILE *f=fopen(path,"r");int pid=0;if(f){if(fscanf(f,"%d",&pid)!=1)pid=0;fclose(f);}return pid;}
static void remove_private_socket(void){
 if(!session->display_pid||!session->display_reaped||!session->cursor_reaped)return;
 if(pid_marker("/tmp/.X7-lock")!=(int)session->display_pid)return;
 if(!kill(session->display_pid,0)||errno!=ESRCH)return;
 FILE *f=fopen("/proc/net/unix","r");if(!f)return;char line[512];int active=0;
 while(fgets(line,sizeof line,f))if(strstr(line,"/tmp/.X11-unix/X7"))active=1;fclose(f);
 if(active){puts("RGB_STALE_SOCKET_STILL_ACTIVE_NOT_REMOVED");return;}
 struct stat st;if(!lstat("/tmp/.X11-unix/X7",&st)&&(!S_ISSOCK(st.st_mode)||st.st_uid!=geteuid()))return;
 if(unlink("/tmp/.X11-unix/X7")&&errno!=ENOENT)return;
 if(!unlink("/tmp/.X7-lock"))puts("RGB_OWNED_STALE_X_SOCKET_REMOVED");
}
static void release_lease(void){
 if(lease_held&&pid_marker("/tmp/doveos-direct-display")==getpid()){unlink("/tmp/doveos-direct-display");puts("RGB_DISPLAY_LEASE_RELEASED");}lease_held=0;
}
static int __attribute__((unused)) acquire_lease(void){
 int fd=open("/tmp/doveos-direct-display",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(fd<0)return 0;
 char value[32];int n=snprintf(value,sizeof value,"%d\n",getpid());int wrote=write(fd,value,n);close(fd);lease_held=1;
 if(wrote!=n){release_lease();return 0;}
 for(int i=0;i<160;i++){if(pid_marker("/tmp/doveos-direct-display.ready")==getpid()&&pid_marker("/tmp/doveos-gpu-cursor-paused")==getpid()){puts("RGB_DISPLAY_LEASE_READY");return 1;}usleep(50000);}
 release_lease();return 0;
}
static int read_owner(long pid){
 char path[64];snprintf(path,sizeof path,"/proc/%ld/mem",pid);int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return 0;
 uint32_t tag[32];ssize_t n=pread(fd,tag,sizeof tag,0xbd148);close(fd);
 if(n!=sizeof tag||!tag[16]||tag[17]!=3||tag[15]!=39||tag[7]!=0x04380780||tag[11]!=0x02d00500)return 0;
 owner=tag[16];printf("RGB_VERIFIED_STOCK_OWNER=%08x\n",owner);return 1;
}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IOLBF,0);
 int lock=open("/tmp/doveos-rgb-owner.lock",O_CREAT|O_RDWR|O_CLOEXEC,0600);
 if(lock<0||flock(lock,LOCK_EX|LOCK_NB)||prctl(PR_SET_CHILD_SUBREAPER,1,0,0,0)){perror("RGB_GUARDIAN_LOCK");return 2;}
 signal(SIGTERM,stop);signal(SIGINT,stop);unsigned plane=3;
 if(getenv("DOVE_RGB_1080")){puts("ONLY_720P_ACCEPTED");return 2;}frame_bytes=((size_t)target_width*target_height*4+4095)&~(size_t)4095;
 printf("GPU_TARGET %ux%u bytes=%zu\n",target_width,target_height,frame_bytes);
 char *end=NULL;long pid=argc==2?strtol(argv[1],&end,10):0;
 if(pid<2||!end||*end||!read_owner(pid)){puts("STOCK_OWNER_VALIDATION_FAILED");return 2;}
 void *ionlib=dlopen("/tmp/doveos-gpu-desktop/libVOutAllocator.so",RTLD_LAZY|RTLD_LOCAL|RTLD_DEEPBIND);if(!ionlib){puts(dlerror());return 2;}
 int (*alloc)(int,size_t,size_t,uint32_t,uint32_t,uint32_t*)=dlsym(ionlib,"ion_alloc");
 int (*share)(int,uint32_t,int*)=dlsym(ionlib,"ion_share"),(*phys)(int,uint32_t,uint32_t*,uint32_t*)=dlsym(ionlib,"ion_phys"),(*release)(int,uint32_t)=dlsym(ionlib,"ion_free");
 if(!alloc||!share||!phys||!release)return 2;
 int ion=open("/dev/ion",O_RDWR|O_CLOEXEC);if(ion<0)return 2;
 unsigned long long free_kb=0,cached_kb=0,shmem_kb=0,cma=0,v;char line[160],key[64];
 FILE *f=fopen("/proc/meminfo","r");if(!f)return 2;
 while(fgets(line,sizeof line,f))if(sscanf(line,"%63s %llu",key,&v)==2){if(!strcmp(key,"MemFree:"))free_kb=v;else if(!strcmp(key,"Cached:"))cached_kb=v;else if(!strcmp(key,"Shmem:"))shmem_kb=v;}fclose(f);
 f=fopen("/proc/vmstat","r");if(f){while(fgets(line,sizeof line,f))if(sscanf(line,"%63s %llu",key,&v)==2&&!strcmp(key,"nr_free_cma"))cma=v;fclose(f);}
 unsigned long long avail=(free_kb+(cached_kb>shmem_kb?(cached_kb-shmem_kb)/2:0))*1024,reserved=cma*4096,need=frame_bytes*6+16*1024*1024;
 avail=avail>reserved?avail-reserved:0;printf("GPU_MEMORY_GATE estimated_available=%llu required=%llu\n",avail,need);if(avail<need)return 2;
 Buffer b[5]={0};unsigned allocated=0;
 for(unsigned i=0;i<5;i++){
  b[i].dma=-1;b[i].bytes=i==0?4096:i==1?69632:frame_bytes;uint32_t actual=0;
  if(alloc(ion,b[i].bytes,4096,128,128,&b[i].handle)||phys(ion,b[i].handle,&b[i].phys,&actual)||actual!=b[i].bytes||share(ion,b[i].handle,&b[i].dma))return 2;
  b[i].cpu=mmap(NULL,b[i].bytes,PROT_READ|PROT_WRITE,MAP_SHARED,b[i].dma,0);if(b[i].cpu==MAP_FAILED)return 2;
  memset(b[i].cpu,0,b[i].bytes);allocated++;
  if(i>=2){uint32_t colors[]={0xffff0000,0xff00ff00,0xff0000ff};for(unsigned p=0;p<target_width*target_height;p++)((uint32_t*)b[i].cpu)[p]=colors[i-2];}
 }
 char path[]="/tmp/dove-rgb-session-XXXXXX";session_fd=mkstemp(path);if(session_fd<0)return 2;unlink(path);if(ftruncate(session_fd,RGB_SESSION_BYTES))return 2;
 session=mmap(NULL,RGB_SESSION_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,session_fd,0);if(session==MAP_FAILED)return 2;
 session->magic=RGB_SESSION_MAGIC;session->width=target_width;session->height=target_height;session->parent=getpid();
 for(int i=0;i<5;i++)session->buffers[i]=(WireBuffer){b[i].phys,b[i].bytes,b[i].dma};state=&session->state;
 state->safe=1;state->gpu_safe=1;state->owner=owner;state->restored=1;
 if(!access("/tmp/doveos-direct-display",F_OK)||!access("/tmp/gpu-rgb-retained",F_OK)){puts("RGB_DISPLAY_BUSY");return 2;}
 marker("/tmp/doveos-rgb-guardian.pid",getpid());
 unblock_signals();
 pid_t child_pid=fork();if(child_pid==0)child(b,plane);if(child_pid<0){release_lease();return 2;}
 int status=0;uint32_t launched=clock_ms();
 for(;;){
  int out=0;if(waitpid(child_pid,&out,WNOHANG)==child_pid){status=WIFEXITED(out)?WEXITSTATUS(out):128+WTERMSIG(out);break;}
  uint32_t now=clock_ms(),owner_beat=session->owner_heartbeat;
  if(!owner_beat)owner_beat=launched;
  if(stopping||pid_marker("/tmp/doveos-rgb-stop")==getpid()||rgb_heartbeat_expired(now,owner_beat,12000)){
   printf("RGB_GUARDIAN_STOP reason=%s signal=%d heartbeat_age_ms=%u uptime_ms=%u\n",stopping?"signal":pid_marker("/tmp/doveos-rgb-stop")==getpid()?"stop_request":"owner_heartbeat_timeout",(int)stopping,(unsigned)(now-owner_beat),now);
   kill(child_pid,SIGTERM);status=wait_child(child_pid,5);break;
  }
  usleep(100000);
 }
 /* If RPC-owner recovery required a kill, retire its adopted clients before
    attempting display recovery. PR_SET_CHILD_SUBREAPER makes waitpid valid. */
 unsigned clients[2]={session->display_pid,session->cursor_pid};
 for(int i=0;i<2;i++){
  volatile uint32_t *reaped=i?&session->cursor_reaped:&session->display_reaped;
  if(clients[i]&&!*reaped){kill(clients[i],SIGTERM);int r=wait_child(clients[i],2);*reaped=r!=125;}
 }
printf("RGB_DESKTOP_SERVER_EXIT=%d agent=%u phase=%u safe=%u\n",status,state->agent,state->phase,state->safe);
 if(!state->safe||!state->restored){child_pid=fork();if(child_pid==0){rpc_init();_exit(cleanup()?0:7);}if(child_pid>0)printf("RGB_RECOVERY_EXIT=%d\n",wait_child(child_pid,15));}
 if(session->display_pid&&!session->closed)state->gpu_safe=retired_gpu_after_exit();
 remove_private_socket();
 if(state->restored)release_lease();
 if(!state->safe||!state->restored||!state->gpu_safe||(session->display_pid&&!session->display_reaped)||(session->cursor_pid&&!session->cursor_reaped)){FILE *f=fopen("/tmp/gpu-rgb-retained","w");if(f){fprintf(f,"keeper=%u agent=%u plane=%u\n",getpid(),state->agent,plane);fclose(f);}puts("RGB_BUFFERS_RETAINED: firmware retirement not confirmed");for(;;)sleep(30);}
 printf("RGB_PROCESS_RETIREMENT display=%u reaped=%u cursor=%u reaped=%u gpu_safe=%u\n",session->display_pid,session->display_reaped,session->cursor_pid,session->cursor_reaped,state->gpu_safe);
 int release_errors=0;for(unsigned i=0;i<allocated;i++){release_errors+=munmap(b[i].cpu,b[i].bytes)!=0;release_errors+=close(b[i].dma)!=0;release_errors+=release(ion,b[i].handle)!=0;}release_errors+=close(ion)!=0;
 printf("RGB_DESKTOP_STATS frames=%u copied=%u reused=%u bad=%u errors=%u ready=%u closed=%u\n",session->frames,session->copies,session->reused,session->bad,session->errors,session->ready,session->closed);printf("RGB_CURSOR_STATS updates=%u frames=%u\n",session->cursor_updates,session->cursor_frames);
 if(!release_errors)puts("RGB_ALL_OWNED_BUFFERS_RELEASED");else printf("RGB_BUFFER_RELEASE_ERRORS=%d\n",release_errors);
 unlink("/tmp/doveos-rgb-guardian.pid");unlink("/tmp/doveos-rgb-x.pid");
 if(pid_marker("/tmp/doveos-rgb-stop")==getpid())unlink("/tmp/doveos-rgb-stop");
 unlink("/tmp/doveos-gpu-display-paused");unlink("/tmp/doveos-gpu-cursor-paused");
 munmap(session,RGB_SESSION_BYTES);close(session_fd);return release_errors?10:status;
}
