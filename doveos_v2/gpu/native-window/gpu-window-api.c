#define DOVE_SHARED_LIBRARY
#include "shared-window.c"
#include "dove_gpu_window.h"
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
typedef struct {uint32_t version,width,height,frames,verify,window;} Hello;
struct DoveGpuWindow {
 Display *x;Window window;EGLDisplay display;EGLContext context;pthread_t thread;
 Target target[3];int socket,width,height,begun,failed;unsigned sequence,completed;
 size_t bytes;int capacity_width,capacity_height;
 XWindowAttributes cached;int known,watching;double next_query;
};
static pthread_mutex_t owner_lock=PTHREAD_MUTEX_INITIALIZER;
static size_t api_bytes;
static int api_poisoned;
static pthread_t api_thread;
static unsigned api_windows;
static Display *query_display;
static int query_error;
static XErrorHandler previous_handler;
static int query_handler(Display*d,XErrorEvent*e){
 if(d==query_display){query_error=1;return 0;}
 return previous_handler?previous_handler(d,e):0;
}
static int attributes(DoveGpuWindow*w,XWindowAttributes*a){
 /* Private events do not consume the application's own X event queue.
  * Recheck ancestors periodically: hiding a parent need not notify this child. */
 int count=0;
 while(count++<64&&XPending(w->x)){
  XEvent e;XNextEvent(w->x,&e);
  if(e.xany.window!=w->window)continue;
  if(e.type==DestroyNotify){w->failed=1;return 0;}
  if(e.type==ConfigureNotify||e.type==MapNotify||e.type==UnmapNotify||e.type==ReparentNotify)w->known=0;
 }
 if(XPending(w->x))w->known=0;
 double time=now();
 if(w->known&&time<w->next_query){*a=w->cached;return 1;}
 /* The synchronous geometry reply also consumes preceding select/attribute
  * errors, without an additional pre/post XSync round trip. */
 query_display=w->x;query_error=0;
 previous_handler=XSetErrorHandler(query_handler);
 if(!w->watching)XSelectInput(w->x,w->window,StructureNotifyMask);
 int ok=XGetWindowAttributes(w->x,w->window,a);
 XSetErrorHandler(previous_handler);query_display=NULL;
 if(!ok||query_error)return 0;
 w->watching=1;w->known=1;w->cached=*a;w->next_query=now()+0.1;return 1;
}
static int valid(DoveGpuWindow*w){
 return w&&!w->failed&&pthread_equal(w->thread,pthread_self())&&
        eglGetCurrentDisplay()==w->display&&eglGetCurrentContext()==w->context;
}
static int ack(DoveGpuWindow*w,unsigned expected){
 struct pollfd p={.fd=w->socket,.events=POLLIN};int r;
 do{r=poll(&p,1,2000);}while(r<0&&errno==EINTR);
 uint32_t value;if(r<=0||recv(w->socket,&value,sizeof value,0)!=sizeof value||value!=expected)return 0;
 w->completed=value;return 1;
}
static int hello(int s,int fd,Hello*h){
 char control[CMSG_SPACE(sizeof(int))]={0};struct iovec i={h,sizeof*h};
 struct msghdr m={.msg_iov=&i,.msg_iovlen=1,.msg_control=control,.msg_controllen=sizeof control};
 struct cmsghdr*c=CMSG_FIRSTHDR(&m);c->cmsg_level=SOL_SOCKET;c->cmsg_type=SCM_RIGHTS;c->cmsg_len=CMSG_LEN(sizeof fd);
 memcpy(CMSG_DATA(c),&fd,sizeof fd);return sendmsg(s,&m,MSG_NOSIGNAL)==sizeof*h;
}
static void release(DoveGpuWindow*w){
 if(w->socket>=0){close(w->socket);w->socket=-1;}
 for(int i=0;i<3;i++){if(w->target[i].x)close_target(&w->target[i]);memset(&w->target[i],0,sizeof(Target));}
 api_bytes-=w->bytes;w->bytes=0;w->sequence=w->completed=0;w->begun=0;
}
static int resize(DoveGpuWindow*w,int width,int height){
 size_t bytes=(((size_t)width*height*4+4095)&~(size_t)4095)*3;
 if(width<32||height<32||width>1280||height>720||api_bytes-w->bytes+bytes>32u*1024*1024)return 0;
 release(w);gpu_only=1;
 for(int i=0;i<3;i++){
  if(!create_target(&w->target[i],w->display,w->x,width,height)){if(w->target[i].attempted)api_poisoned=1;release(w);return 0;}
  w->bytes+=w->target[i].bytes;api_bytes+=w->target[i].bytes;
 }
 const char*path=getenv("DOVE_COMPOSITOR_SOCKET");if(!path)path="/tmp/doveos-rgb/native-windows.sock";
 if(strlen(path)>=108){release(w);return 0;}
 struct sockaddr_un address={.sun_family=AF_UNIX};strcpy(address.sun_path,path);
 /* A just-closed resize session may still be awaiting its server dispatch.
  * Retry the handshake with the same new targets, without allocating again. */
 for(int attempt=0;attempt<3;attempt++){
  w->socket=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);
 Hello h={6,width,height,0,0,w->window};int good=w->socket>=0&&!connect(w->socket,(void*)&address,sizeof address);
  for(int i=0;i<3&&good;i++)good=hello(w->socket,w->target[i].dma,&h);
  if(good&&ack(w,0)){w->width=width;w->height=height;return 1;}
  if(w->socket>=0)close(w->socket);w->socket=-1;usleep(20000);
 }
 release(w);return 0;
}
DoveGpuWindow*dove_gpu_window_attach_capacity(Window window,int width,int height){
 if(width<32||height<32||width>1280||height>720)return NULL;
 EGLDisplay d=eglGetCurrentDisplay();EGLContext c=eglGetCurrentContext();EGLint version=0;
 pthread_mutex_lock(&owner_lock);
 if((api_windows&&!pthread_equal(api_thread,pthread_self()))||api_poisoned||!window||!d||!c||!eglQueryContext(d,c,EGL_CONTEXT_CLIENT_VERSION,&version)||version!=2||getenv("DOVE_SHARED_CACHED")){pthread_mutex_unlock(&owner_lock);return NULL;}
 DoveGpuWindow*w=calloc(1,sizeof*w);if(!w){pthread_mutex_unlock(&owner_lock);return NULL;}
 w->socket=-1;w->capacity_width=width;w->capacity_height=height;w->display=d;w->context=c;w->thread=pthread_self();w->window=window;w->x=XOpenDisplay(NULL);
 if(!w->x){free(w);pthread_mutex_unlock(&owner_lock);return NULL;}
 api_thread=w->thread;api_windows++;pthread_mutex_unlock(&owner_lock);return w;
}
DoveGpuWindow*dove_gpu_window_attach(Window window){return dove_gpu_window_attach_capacity(window,1280,720);}
int dove_gpu_window_begin(DoveGpuWindow*w,int*width,int*height){
 if(!valid(w)||!width||!height||w->begun)return -1;
 XWindowAttributes a;if(!attributes(w,&a)||a.depth!=24){w->failed=1;return -1;}
 if(a.map_state!=IsViewable)return 0;
 if(a.width<32||a.height<32||a.width>w->capacity_width||a.height>w->capacity_height){w->failed=1;return -1;}
 if(w->socket<0||w->sequence>=0x7fffffff){
  /* Reserve the desktop bounds once. Logical viewport updates then require no
   * allocation/import handshake during live resizing. Three targets: 10.55 MiB. */
  if(!resize(w,w->capacity_width,w->capacity_height)){w->failed=1;return -1;}
 }
 w->width=a.width;w->height=a.height;
 unsigned next=w->sequence+1;
 while(next>=4&&w->completed<next-2)
  if(!ack(w,w->completed+1)){w->failed=1;return -1;}
 glBindFramebuffer(GL_FRAMEBUFFER,w->target[(next-1)%3].fbo);
 glViewport(0,0,w->width,w->height);glDisable(GL_SCISSOR_TEST);
 *width=w->width;*height=w->height;w->begun=1;return 1;
}
int dove_gpu_window_present(DoveGpuWindow*w){
 if(!valid(w)||!w->begun)return 0;
 GLint fb;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fb);
 unsigned next=w->sequence+1;if((GLuint)fb!=w->target[(next-1)%3].fbo)return 0;
 uint32_t packet[3]={next,w->width,w->height};
 glFinish();if(glGetError()!=GL_NO_ERROR||send(w->socket,packet,sizeof packet,MSG_NOSIGNAL)!=sizeof packet){w->failed=1;return 0;}
 if(next==1&&!ack(w,next)){w->failed=1;return 0;}
 w->sequence=next;w->begun=0;return 1;
}
int dove_gpu_window_detach(DoveGpuWindow*w){
 if(!w||!pthread_equal(w->thread,pthread_self())||eglGetCurrentDisplay()!=w->display||eglGetCurrentContext()!=w->context)return 0;
 release(w);XCloseDisplay(w->x);pthread_mutex_lock(&owner_lock);api_windows--;pthread_mutex_unlock(&owner_lock);free(w);return 1;
}
int dove_gpu_window_flush(DoveGpuWindow*w){
 if(!valid(w)||w->begun)return 0;
 while(w->completed<w->sequence)if(!ack(w,w->completed+1)){w->failed=1;return 0;}
 return 1;
}
int dove_gpu_window_copy_framebuffer(DoveGpuWindow*w,unsigned source,int width,int height){
 /* Explicit compatibility bridge: one GPU copy, no CPU readback. Preserve the
  * caller's bindings and raster state, including on allocation/visibility errors. */
 if(!valid(w)||w->begun||width<1||height<1||width>w->capacity_width||height>w->capacity_height)return -1;
 GLint framebuffer,texture,viewport[4],scissor[4];GLboolean enabled=glIsEnabled(GL_SCISSOR_TEST);
 glGetIntegerv(GL_FRAMEBUFFER_BINDING,&framebuffer);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
 glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_SCISSOR_BOX,scissor);
 int tw,th,result=dove_gpu_window_begin(w,&tw,&th);
 if(result==1){
  unsigned next=w->sequence+1;
  glBindFramebuffer(GL_FRAMEBUFFER,source);
  glBindTexture(GL_TEXTURE_2D,w->target[(next-1)%3].texture);
  glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,width,height);
  glBindFramebuffer(GL_FRAMEBUFFER,w->target[(next-1)%3].fbo);
  w->width=width;w->height=height;
  if(!dove_gpu_window_present(w))result=-1;
 }
 glBindTexture(GL_TEXTURE_2D,texture);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
 glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glScissor(scissor[0],scissor[1],scissor[2],scissor[3]);
 if(enabled)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
 return result;
}
