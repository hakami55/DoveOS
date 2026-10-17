#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XTest.h>
#include <X11/Xlib-xcb.h>
#include <X11/extensions/xfixesproto.h>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <string.h>
#include "rgb-session.h"
#include "busy-frames.h"
#include "wait-frames.h"
static volatile sig_atomic_t done;
static void stop(int sig){(void)sig;done=1;}
static unsigned cursor_request(xcb_connection_t *connection){
 static xcb_extension_t extension={"XFIXES",0};
 static const xcb_protocol_request_t protocol={2,&extension,X_XFixesGetCursorImageAndName,0};
 xXFixesGetCursorImageAndNameReq request={0};struct iovec parts[4];
 parts[2].iov_base=&request;parts[2].iov_len=sizeof request;
 parts[3].iov_base=NULL;parts[3].iov_len=(-sizeof request)&3;
 return xcb_send_request(connection,XCB_REQUEST_CHECKED,parts+2,&protocol);
}
static int closed_display(Display *d){(void)d;puts("RGB_CURSOR_X_CLOSED");fflush(stdout);_exit(0);}
static uint32_t clock_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint32_t)(t.tv_sec*1000u+t.tv_nsec/1000000u);}
static int launch_busy(void){
 DIR *d=opendir("/tmp/doveos-cursor-busy");int busy=0;
 if(d){struct dirent *e;int checked=0;while((e=readdir(d))&&checked++<64){
  char path[320];long pid=0;unsigned deadline=0;
  if(e->d_name[0]=='.')continue;
  snprintf(path,sizeof path,"/tmp/doveos-cursor-busy/%s",e->d_name);
  FILE *f=fopen(path,"r");if(!f)continue;
  int n=fscanf(f,"%ld %u",&pid,&deadline);fclose(f);
  if(n==2&&pid>1&&pid<4194304&&!kill(pid,0)&&(int32_t)(deadline-clock_ms())>0){busy=1;break;}
 }closedir(d);}
 if(!busy){struct stat st;FILE *f=fopen("/tmp/doveos-vlc-opening","r");long pid=0;
  if(f){int n=fscanf(f,"%ld",&pid);fclose(f);busy=n==1&&pid>1&&pid<4194304&&!kill(pid,0)&&!stat("/tmp/doveos-vlc-opening",&st)&&time(NULL)-st.st_mtime<60;}
 }return busy;
}

int main(int argc,char **argv){
 sigset_t mask;sigemptyset(&mask);sigprocmask(SIG_SETMASK,&mask,NULL);
 if(argc!=3)return 2;int fd=atoi(argv[1]),owner=atoi(argv[2]);struct stat st;
 if(fd<3||owner<2||fstat(fd,&st)||st.st_size!=RGB_SESSION_BYTES)return 2;
 RgbSession *s=mmap(NULL,RGB_SESSION_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
 if(s==MAP_FAILED||s->magic!=RGB_SESSION_MAGIC)return 2;
 signal(SIGTERM,stop);signal(SIGINT,stop);Display *d=NULL;
 for(int i=0;i<100&&!done&&!kill(owner,0)&&!d;i++){d=XOpenDisplay(NULL);if(!d)usleep(50000);}
 if(!d)return 3;
 XSetIOErrorHandler(closed_display);
 XTestGrabControl(d,True);XFixesHideCursor(d,DefaultRootWindow(d));XSync(d,False);
 xcb_connection_t *connection=XGetXCBConnection(d);if(!connection)return 3;
 XSetEventQueueOwner(d,XCBOwnsEventQueue);
 unsigned request=0;XFixesCursorImage cached={0};uint32_t pixels[RGB_CURSOR_SIDE*RGB_CURSOR_SIDE];
 char name[80]={0};int have_cursor=0;unsigned replies=0,waits=0;
 unsigned long serial=0;int last_busy=-1,last_frame=-1;uint32_t check=0;int busy=0;
 FILE *pidfile=fopen("/tmp/doveos-gpu-cursor.pid","w");if(pidfile){fprintf(pidfile,"%d\n",getpid());fclose(pidfile);}
 puts("GPU_CURSOR_OVERLAY_READY renderer=rgb720");
 puts("RGB_CURSOR_BRIDGE_READY");fflush(stdout);
 while(!done&&!kill(owner,0)){
  s->cursor_heartbeat=clock_ms();
  if(xcb_connection_has_error(connection))break;
  if(!request){request=cursor_request(connection);if(!request||xcb_flush(connection)<=0)break;}
  xXFixesGetCursorImageAndNameReply *reply=NULL;xcb_generic_error_t *error=NULL;
  if(xcb_poll_for_reply(connection,request,(void**)&reply,&error)){
   request=0;
   if(error){free(error);free(reply);break;}
   if(reply){
    size_t count=(size_t)reply->width*reply->height,bytes=(size_t)reply->length*4;
    if(reply->width&&reply->height&&reply->width<=RGB_CURSOR_SIDE&&reply->height<=RGB_CURSOR_SIDE&&count*4+reply->nbytes<=bytes){
     cached.x=reply->x;cached.y=reply->y;cached.width=reply->width;cached.height=reply->height;
     cached.xhot=reply->xhot;cached.yhot=reply->yhot;cached.cursor_serial=reply->cursorSerial;
     memcpy(pixels,(char*)reply+32,count*4);size_t length=reply->nbytes;if(length>=sizeof name)length=sizeof name-1;
     memcpy(name,(char*)reply+32+count*4,length);name[length]=0;cached.name=name;have_cursor=1;replies++;
    }
    free(reply);
   }
  }else waits++;
  for(int i=0;i<8;i++){xcb_generic_event_t*event=xcb_poll_for_event(connection);if(!event)break;free(event);}
  if(!have_cursor){usleep(4000);continue;}
  XFixesCursorImage image=cached,*c=&image;
  uint32_t now=clock_ms();if((uint32_t)(now-check)>=100){busy=launch_busy();check=now;}
  /* This X server reports animated theme cursors as a transparent 1x1
     placeholder (serial 0), while preserving their XFixes name. Supply the
     matching installed theme frames; intentionally hidden cursors stay hidden. */
  int kind=busy;
  if(c->name&&(!strcmp(c->name,"watch")||!strcmp(c->name,"wait")||!strcmp(c->name,"clock")))kind=2;
  else if(c->name&&(!strcmp(c->name,"left_ptr_watch")||!strcmp(c->name,"progress")||!strcmp(c->name,"half-busy")))kind=1;
  const struct BusyFrame *art=kind==2?wait_frames:busy_frames;
  unsigned count=kind==2?sizeof wait_frames/sizeof wait_frames[0]:sizeof busy_frames/sizeof busy_frames[0];
  int frame=kind?(int)((now/150)%count):-1;
  if(kind){const struct BusyFrame *b=&art[frame];c->width=b->w;c->height=b->h;c->xhot=b->hx;c->yhot=b->hy;}
  int nx=c->x-c->xhot,ny=c->y-c->yhot;int shape=serial!=c->cursor_serial||kind!=last_busy||frame!=last_frame||!s->cursor_updates;
  if(c->width<=RGB_CURSOR_SIDE&&c->height<=RGB_CURSOR_SIDE&&(shape||nx!=s->cursor.x||ny!=s->cursor.y)){
   __atomic_add_fetch(&s->cursor_seq,1,__ATOMIC_SEQ_CST);
   s->cursor.x=nx;s->cursor.y=ny;s->cursor.w=c->width;s->cursor.h=c->height;s->cursor.visible=1;
   if(shape){for(unsigned i=0;i<(unsigned)c->width*c->height;i++)s->cursor.pixels[i]=kind?art[frame].pixels[i]:pixels[i];s->cursor.shape++;}
   serial=c->cursor_serial;last_busy=kind;last_frame=frame;s->cursor_updates++;
   __atomic_add_fetch(&s->cursor_seq,1,__ATOMIC_SEQ_CST);
  }
  usleep(request?4000:12000);
 }
 unlink("/tmp/doveos-gpu-cursor.pid");
 /* Closing this process releases its XFixes hide count. Do not wait for
  * a synchronous X reply during retirement; a WM may hold the server grab. */
 printf("RGB_CURSOR_ASYNC replies=%u pending_polls=%u\n",replies,waits);fflush(stdout);
 munmap(s,RGB_SESSION_BYTES);return 0;
}
