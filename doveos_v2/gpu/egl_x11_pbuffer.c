/* GLES2 X11 windows through standard Mali pbuffers and checked readback.
 * Avoids the native-pixmap resolve path that produced PP command faults.
 * GPU rendering, CPU/XShm presentation; not an accelerated X server. */
#define _GNU_SOURCE
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>
#include <unistd.h>
#include <arm_neon.h>

/* Entry points are wrapped below in a recursive registry lock. */
#define eglGetDisplay impl_eglGetDisplay
#define eglInitialize impl_eglInitialize
#define eglGetError impl_eglGetError
#define eglChooseConfig impl_eglChooseConfig
#define eglGetConfigAttrib impl_eglGetConfigAttrib
#define eglCreateWindowSurface impl_eglCreateWindowSurface
#define eglCreatePixmapSurface impl_eglCreatePixmapSurface
#define eglCopyBuffers impl_eglCopyBuffers
#define eglMakeCurrent impl_eglMakeCurrent
#define eglGetCurrentSurface impl_eglGetCurrentSurface
#define eglDestroySurface impl_eglDestroySurface
#define eglQuerySurface impl_eglQuerySurface
#define eglSwapInterval impl_eglSwapInterval
#define eglSwapBuffers impl_eglSwapBuffers
#define eglSurfaceAttrib impl_eglSurfaceAttrib
#define eglBindTexImage impl_eglBindTexImage
#define eglReleaseTexImage impl_eglReleaseTexImage
#define eglWaitNative impl_eglWaitNative
#define eglTerminate impl_eglTerminate
#define dove_egl_set_damage impl_dove_egl_set_damage
#define eglGetConfigs impl_eglGetConfigs
#define eglBindAPI impl_eglBindAPI
#define eglQueryAPI impl_eglQueryAPI
#define eglCreatePbufferSurface impl_eglCreatePbufferSurface
#define eglCreatePbufferFromClientBuffer impl_eglCreatePbufferFromClientBuffer
#define eglCreateContext impl_eglCreateContext
#define eglDestroyContext impl_eglDestroyContext
#define eglQueryContext impl_eglQueryContext
#define eglGetCurrentContext impl_eglGetCurrentContext
#define eglGetCurrentDisplay impl_eglGetCurrentDisplay
#define eglQueryString impl_eglQueryString
#define eglWaitGL impl_eglWaitGL
#define eglWaitClient impl_eglWaitClient
#define eglReleaseThread impl_eglReleaseThread

static EGLDisplay eglGetDisplay(EGLNativeDisplayType native);
static EGLBoolean eglInitialize(EGLDisplay d,EGLint *major,EGLint *minor);
static EGLint eglGetError(void);
static EGLBoolean eglChooseConfig(EGLDisplay d,const EGLint *attrs,EGLConfig *configs,EGLint max,EGLint *count);
static EGLBoolean eglGetConfigAttrib(EGLDisplay d,EGLConfig cfg,EGLint attr,EGLint *out);
static EGLSurface eglCreateWindowSurface(EGLDisplay d,EGLConfig cfg,EGLNativeWindowType win,const EGLint *attrs);
static EGLSurface eglCreatePixmapSurface(EGLDisplay d,EGLConfig cfg,EGLNativePixmapType pix,const EGLint *attrs);
static EGLBoolean eglCopyBuffers(EGLDisplay d,EGLSurface surface,EGLNativePixmapType pix);
static EGLBoolean eglMakeCurrent(EGLDisplay d,EGLSurface draw,EGLSurface read,EGLContext context);
static EGLSurface eglGetCurrentSurface(EGLint type);
static EGLBoolean eglDestroySurface(EGLDisplay d,EGLSurface surface);
static EGLBoolean eglQuerySurface(EGLDisplay d,EGLSurface surface,EGLint attr,EGLint *out);
static EGLBoolean eglSwapInterval(EGLDisplay d,EGLint interval);
static EGLBoolean eglSwapBuffers(EGLDisplay d,EGLSurface surface);
static EGLBoolean eglSurfaceAttrib(EGLDisplay d,EGLSurface surface,EGLint attr,EGLint value);
static EGLBoolean eglBindTexImage(EGLDisplay d,EGLSurface surface,EGLint buffer);
static EGLBoolean eglReleaseTexImage(EGLDisplay d,EGLSurface surface,EGLint buffer);
static EGLBoolean eglWaitNative(EGLint engine);
static EGLBoolean eglTerminate(EGLDisplay d);
static int dove_egl_set_damage(const int *rects,int count);
static EGLBoolean eglGetConfigs(EGLDisplay d,EGLConfig *c,EGLint n,EGLint *out);
static EGLBoolean eglBindAPI(EGLenum api);
static EGLenum eglQueryAPI(void);
static EGLSurface eglCreatePbufferSurface(EGLDisplay d,EGLConfig c,const EGLint *a);
static EGLSurface eglCreatePbufferFromClientBuffer(EGLDisplay d,EGLenum t,EGLClientBuffer b,EGLConfig c,const EGLint *a);
static EGLContext eglCreateContext(EGLDisplay d,EGLConfig c,EGLContext share,const EGLint *a);
static EGLBoolean eglDestroyContext(EGLDisplay d,EGLContext c);
static EGLBoolean eglQueryContext(EGLDisplay d,EGLContext c,EGLint a,EGLint *v);
static EGLContext eglGetCurrentContext(void);
static EGLDisplay eglGetCurrentDisplay(void);
static const char *eglQueryString(EGLDisplay d,EGLint name);
static EGLBoolean eglWaitGL(void);
static EGLBoolean eglWaitClient(void);
static EGLBoolean eglReleaseThread(void);
typedef struct {uint32_t h,w,bytespp;uint8_t bits,r,g,b,a,l,pad[2];uint32_t flags;void *memory;uint32_t yuv;} Native;
typedef struct Surface {
 struct Surface *next;Display *x11,*exposure;EGLSurface real;EGLDisplay display;EGLConfig config;Window window;
 Native native;int ion,dma,width,height,pending;size_t bytes;uint32_t handle,physical;
 void *cpu,*base,*memory,*readback;XImage *image;GC gc;
 XShmSegmentInfo shm;int shm_attached;
 double wait_ms,present_ms,measured,read_ms,convert_ms;unsigned frames;
 int interval;double next_swap;int initialized,damage_set,damage_count,damage_rects[64];unsigned long long read_pixels;
} Surface;
static void *mali;static Display *xdisplay;static Display *owned_display;
static Surface *surfaces;static size_t owned_bytes;
static __thread EGLint error=EGL_SUCCESS;
static pthread_mutex_t registry_lock=PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
/* Track every thread, not only the thread destroying a surface. A successful
 * vendor unbind must precede retiring CPU/XShm staging of a pending surface. */
typedef struct Binding {struct Binding *next;pthread_t thread;EGLDisplay display;EGLSurface draw,read;EGLContext context;} Binding;
static Binding *bindings;
static pthread_key_t binding_key;
static pthread_once_t binding_once=PTHREAD_ONCE_INIT;
static int binding_key_valid;
static void binding_destructor(void *);
static void create_binding_key(void){binding_key_valid=pthread_key_create(&binding_key,binding_destructor)==0;}
static Binding *get_binding(void){
 pthread_once(&binding_once,create_binding_key);
 if(!binding_key_valid)return NULL;
 Binding *b=pthread_getspecific(binding_key);
 if(!b){b=calloc(1,sizeof *b);if(!b)return NULL;
  b->thread=pthread_self();
  if(pthread_setspecific(binding_key,b)){free(b);return NULL;}
  b->next=bindings;bindings=b;
 }return b;
}
static int surface_bound(Surface *s){
 for(Binding *b=bindings;b;b=b->next)if(b->draw==(EGLSurface)s||b->read==(EGLSurface)s)return 1;
 return 0;
}
static void *symbol(const char *name){
 if(!mali)mali=dlopen(getenv("DOVE_EGL_MALI_LIBRARY")?getenv("DOVE_EGL_MALI_LIBRARY"):"libMali-r4p1-r1p1-trial.so",RTLD_NOW|RTLD_LOCAL|RTLD_DEEPBIND);
 if(!mali){error=EGL_NOT_INITIALIZED;return NULL;}return dlsym(mali,name);
}
#define R(name) ((__typeof__(&name))symbol(#name))
static EGLBoolean fail(EGLint e){error=e;return EGL_FALSE;}
static Surface *lookup(EGLSurface surface){for(Surface *s=surfaces;s;s=s->next)if((void*)s==surface)return s;return NULL;}
static EGLSurface underlying(EGLSurface surface){Surface *s=lookup(surface);return s?s->real:surface;}
static double seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static void pace(Surface *s){if(s->interval){double now=seconds();if(s->next_swap>now&&s->next_swap-now<.1){double delay=s->next_swap-now;struct timespec t={(time_t)delay,(long)((delay-(time_t)delay)*1e9)};while(nanosleep(&t,&t)&&errno==EINTR){}}
  now=seconds();s->next_swap=(s->next_swap>now-.1?s->next_swap:now)+s->interval/60.0;
 }}
static size_t available_window_memory(void){
 unsigned long long f=0,b=0,c=0,sh=0,cma=0,value;char line[160],key[64];
 FILE *p=fopen("/proc/meminfo","r");if(!p)return 0;
 while(fgets(line,sizeof line,p))if(sscanf(line,"%63s %llu",key,&value)==2){
  if(!strcmp(key,"MemFree:"))f=value;
  else if(!strcmp(key,"Buffers:"))b=value;
  else if(!strcmp(key,"Cached:"))c=value;
  else if(!strcmp(key,"Shmem:"))sh=value;
 }fclose(p);
 p=fopen("/proc/vmstat","r");if(p){while(fgets(line,sizeof line,p))if(sscanf(line,"%63s %llu",key,&value)==2&&!strcmp(key,"nr_free_cma"))cma=value;fclose(p);}
 unsigned long long estimate=f+b+(c>sh?(c-sh)/2:0),reserved=cma*(unsigned long long)sysconf(_SC_PAGESIZE)/1024;
 return estimate>reserved?(size_t)(estimate-reserved)*1024:0;
}
static void free_buffer(Surface *s){
 if(s->real){R(eglDestroySurface)(s->display,s->real);s->real=EGL_NO_SURFACE;}
 if(s->shm_attached){XShmDetach(s->x11,&s->shm);XSync(s->x11,False);s->shm_attached=0;}
 if(s->image){s->image->data=NULL;XDestroyImage(s->image);s->image=NULL;}
 if(s->shm.shmaddr&&(void*)s->shm.shmaddr!=MAP_FAILED){shmdt(s->shm.shmaddr);s->shm.shmaddr=NULL;}
 if(s->cpu!=MAP_FAILED){free(s->cpu);s->cpu=MAP_FAILED;}
 free(s->readback);s->readback=NULL;
 if(s->bytes){owned_bytes-=s->bytes;s->bytes=0;}
}
static int allocate_buffer(Surface *s,int w,int h){
 if(w<1||h<1||w>1920||h>1080)return fail(EGL_BAD_ALLOC);
 size_t bytes=(size_t)w*h*4;
 /* Bound CPU staging plus GPU target conservatively per process. */
 if(owned_bytes+bytes*4>64u*1024*1024)return fail(EGL_BAD_ALLOC);
 if(available_window_memory()<bytes*4+16u*1024*1024)return fail(EGL_BAD_ALLOC);
 s->bytes=bytes*4;owned_bytes+=s->bytes;
 s->cpu=calloc(1,bytes);s->readback=malloc(bytes);
 if(!s->cpu){s->cpu=MAP_FAILED;goto bad;}
 if(!s->readback)goto bad;
 EGLint attrs[]={EGL_WIDTH,w,EGL_HEIGHT,h,EGL_NONE};
 s->real=R(eglCreatePbufferSurface)(s->display,s->config,attrs);
 if(!s->real)goto bad;
 XWindowAttributes a;if(!XGetWindowAttributes(s->x11,s->window,&a)||a.depth!=24)goto bad;
 if(!getenv("DOVE_EGL_NO_SHM")&&XShmQueryExtension(s->x11)){
  s->image=XShmCreateImage(s->x11,a.visual,a.depth,ZPixmap,NULL,&s->shm,w,h);
  if(s->image){
   s->shm.shmid=shmget(IPC_PRIVATE,(size_t)s->image->bytes_per_line*h,IPC_CREAT|0600);
   if(s->shm.shmid>=0){s->shm.shmaddr=shmat(s->shm.shmid,NULL,0);s->shm.readOnly=False;
    if((void*)s->shm.shmaddr!=MAP_FAILED){s->image->data=s->shm.shmaddr;s->shm_attached=XShmAttach(s->x11,&s->shm);XSync(s->x11,False);}
    shmctl(s->shm.shmid,IPC_RMID,NULL);
   }
   if(!s->shm_attached){s->image->data=NULL;XDestroyImage(s->image);s->image=NULL;if(s->shm.shmaddr&&(void*)s->shm.shmaddr!=MAP_FAILED)shmdt(s->shm.shmaddr);s->shm.shmaddr=NULL;}
  }
 }
 if(!s->image)s->image=XCreateImage(s->x11,a.visual,a.depth,ZPixmap,0,s->cpu,w,h,32,w*4);
 if(!s->image)goto bad;
 if(s->shm_attached){free(s->cpu);s->cpu=MAP_FAILED;}
 s->width=w;s->height=h;s->initialized=0;return 1;
bad:free_buffer(s);return fail(EGL_BAD_ALLOC);
}
static void remove_surface(Surface *s){
 Surface **p=&surfaces;while(*p&&*p!=s)p=&(*p)->next;if(*p)*p=s->next;
 free_buffer(s);if(s->gc)XFreeGC(s->x11,s->gc);if(s->exposure)XCloseDisplay(s->exposure);free(s);
}
EGLDisplay eglGetDisplay(EGLNativeDisplayType native){
 if(!symbol("eglGetDisplay"))return EGL_NO_DISPLAY;
 if(native){
  if(xdisplay&&strcmp(DisplayString(xdisplay),DisplayString(native))){fail(EGL_BAD_ACCESS);return EGL_NO_DISPLAY;}
  xdisplay=native;
 }else if(!xdisplay&&getenv("DISPLAY")){xdisplay=XOpenDisplay(NULL);owned_display=xdisplay;}
 return R(eglGetDisplay)(EGL_DEFAULT_DISPLAY);
}
EGLBoolean eglInitialize(EGLDisplay d,EGLint *major,EGLint *minor){return symbol("eglInitialize")?R(eglInitialize)(d,major,minor):EGL_FALSE;}
EGLint eglGetError(void){if(error!=EGL_SUCCESS){EGLint e=error;error=EGL_SUCCESS;return e;}return symbol("eglGetError")?R(eglGetError)():EGL_NOT_INITIALIZED;}
EGLBoolean eglChooseConfig(EGLDisplay d,const EGLint *attrs,EGLConfig *configs,EGLint max,EGLint *count){
 EGLint copy[260];int n=0,window=0,has_surface=0;
 if(attrs)for(int i=0;attrs[i]!=EGL_NONE;i+=2){if(n>246)return fail(EGL_BAD_ATTRIBUTE);EGLint key=attrs[i],v=attrs[i+1];
  if(key==EGL_SURFACE_TYPE)has_surface=1;
  if(key==EGL_SURFACE_TYPE&&(v&EGL_WINDOW_BIT)){window=1;v=(v&~EGL_WINDOW_BIT)|EGL_PBUFFER_BIT;}
  copy[n++]=key;copy[n++]=v;
 }
 if(!has_surface){window=1;copy[n++]=EGL_SURFACE_TYPE;copy[n++]=EGL_PBUFFER_BIT;}
 if(window){
  const EGLint channels[]={EGL_RED_SIZE,EGL_GREEN_SIZE,EGL_BLUE_SIZE,EGL_ALPHA_SIZE};
  for(unsigned k=0;k<4;k++){int found=0;for(int i=0;i<n;i+=2)if(copy[i]==channels[k]){if(copy[i+1]<8)copy[i+1]=8;found=1;}if(!found){copy[n++]=channels[k];copy[n++]=8;}}
 }
 copy[n]=EGL_NONE;return R(eglChooseConfig)(d,copy,configs,max,count);
}
EGLBoolean eglGetConfigAttrib(EGLDisplay d,EGLConfig cfg,EGLint attr,EGLint *out){
 if(!out)return fail(EGL_BAD_PARAMETER);
 if(attr==EGL_NATIVE_VISUAL_ID&&xdisplay){*out=(EGLint)XVisualIDFromVisual(DefaultVisual(xdisplay,DefaultScreen(xdisplay)));return EGL_TRUE;}
 EGLBoolean ok=R(eglGetConfigAttrib)(d,cfg,attr,out);if(ok&&attr==EGL_SURFACE_TYPE&&(*out&EGL_PBUFFER_BIT))*out|=EGL_WINDOW_BIT;return ok;
}
EGLSurface eglCreateWindowSurface(EGLDisplay d,EGLConfig cfg,EGLNativeWindowType win,const EGLint *attrs){
 if(!xdisplay||!win){fail(EGL_BAD_NATIVE_WINDOW);return EGL_NO_SURFACE;}
 if(attrs)for(int i=0;attrs[i]!=EGL_NONE;i+=2)if(attrs[i]!=EGL_RENDER_BUFFER||attrs[i+1]!=EGL_BACK_BUFFER){fail(EGL_BAD_ATTRIBUTE);return EGL_NO_SURFACE;}
 EGLint bits=0;if(!R(eglGetConfigAttrib)(d,cfg,EGL_ALPHA_SIZE,&bits)||bits!=8){fail(EGL_BAD_MATCH);return EGL_NO_SURFACE;}
 XWindowAttributes a;if(!XGetWindowAttributes(xdisplay,win,&a)){fail(EGL_BAD_NATIVE_WINDOW);return EGL_NO_SURFACE;}
 Surface *s=calloc(1,sizeof(*s));if(!s){fail(EGL_BAD_ALLOC);return EGL_NO_SURFACE;}
 s->x11=xdisplay;s->ion=s->dma=-1;s->cpu=MAP_FAILED;s->display=d;s->config=cfg;s->window=win;s->interval=1;
 s->gc=XCreateGC(xdisplay,win,0,NULL);
 if(!s->gc||!allocate_buffer(s,a.width,a.height)){if(s->gc)XFreeGC(xdisplay,s->gc);free(s);return EGL_NO_SURFACE;}
 s->exposure=XOpenDisplay(DisplayString(xdisplay));
 if(s->exposure){XSelectInput(s->exposure,win,ExposureMask);XSync(s->exposure,False);}
 s->next=surfaces;surfaces=s;fprintf(stderr,"DOVE_EGL_PBUFFER_WINDOW_READY %dx%d hardware=Mali-400\n",s->width,s->height);return (EGLSurface)s;
}
/* An X11 Pixmap is an XID, not the private descriptor expected by this blob.
 * Refuse this unsupported path instead of passing an integer as a pointer. */
EGLSurface eglCreatePixmapSurface(EGLDisplay d,EGLConfig cfg,EGLNativePixmapType pix,const EGLint *attrs){
 (void)d;(void)cfg;(void)pix;(void)attrs;fail(EGL_BAD_NATIVE_PIXMAP);return EGL_NO_SURFACE;
}
EGLBoolean eglCopyBuffers(EGLDisplay d,EGLSurface surface,EGLNativePixmapType pix){
 (void)d;(void)surface;(void)pix;return fail(EGL_BAD_NATIVE_PIXMAP);
}
static void reap_pending(void){
 Surface *s=surfaces,*next;
 while(s){next=s->next;if(s->pending&&!surface_bound(s))remove_surface(s);s=next;}
}
EGLBoolean eglMakeCurrent(EGLDisplay d,EGLSurface draw,EGLSurface read,EGLContext context){
 Binding *b=get_binding();if(!b)return fail(EGL_BAD_ALLOC);
 EGLBoolean ok=R(eglMakeCurrent)(d,underlying(draw),underlying(read),context);
 if(ok){b->display=d;b->draw=draw;b->read=read;b->context=context;reap_pending();}
 return ok;
}

EGLSurface eglGetCurrentSurface(EGLint type){EGLSurface real=R(eglGetCurrentSurface)(type);for(Surface *s=surfaces;s;s=s->next)if(s->real==real)return (EGLSurface)s;return real;}
EGLBoolean eglDestroySurface(EGLDisplay d,EGLSurface surface){
 Surface *s=lookup(surface);if(!s)return R(eglDestroySurface)(d,surface);
 if(s->pending)return fail(EGL_BAD_SURFACE);
 if(surface_bound(s)){s->pending=1;return EGL_TRUE;}
 remove_surface(s);return EGL_TRUE;
}
EGLBoolean eglQuerySurface(EGLDisplay d,EGLSurface surface,EGLint attr,EGLint *out){
 Surface *s=lookup(surface);if(s){if(!out)return fail(EGL_BAD_PARAMETER);
  if(attr==EGL_WIDTH||attr==EGL_HEIGHT){/* This synchronous reply fences the previous XShmPutImage on the same
  * X connection before its shared staging bytes are modified again.
  * free_buffer separately waits for XShmDetach before releasing memory. */
 XWindowAttributes a;if(!XGetWindowAttributes(s->x11,s->window,&a))return fail(EGL_BAD_NATIVE_WINDOW);*out=attr==EGL_WIDTH?a.width:a.height;return EGL_TRUE;}
  if(attr==EGL_RENDER_BUFFER){*out=EGL_BACK_BUFFER;return EGL_TRUE;}
 }return R(eglQuerySurface)(d,underlying(surface),attr,out);
}
EGLBoolean eglSwapInterval(EGLDisplay d,EGLint interval){
 Surface *s=lookup(eglGetCurrentSurface(EGL_DRAW));if(s){if(interval<0||interval>4)return fail(EGL_BAD_PARAMETER);s->interval=interval;s->next_swap=0;return EGL_TRUE;}
 return R(eglSwapInterval)(d,interval);
}
EGLBoolean eglSwapBuffers(EGLDisplay d,EGLSurface surface){
 Surface *s=lookup(surface);if(!s)return R(eglSwapBuffers)(d,surface);
 /* This synchronous reply fences the previous XShmPutImage on the same
  * X connection before its shared staging bytes are modified again.
  * free_buffer separately waits for XShmDetach before releasing memory. */
 XWindowAttributes a;if(!XGetWindowAttributes(s->x11,s->window,&a))return fail(EGL_BAD_NATIVE_WINDOW);
 if(a.width!=s->width||a.height!=s->height){
  EGLContext ctx=R(eglGetCurrentContext)();EGLSurface draw=R(eglGetCurrentSurface)(EGL_DRAW),read=R(eglGetCurrentSurface)(EGL_READ);int was_draw=draw==s->real,was_read=read==s->real;
  if(!was_draw||!was_read)return fail(EGL_BAD_ACCESS);
  /* Keep the working target and binding until its replacement is ready.
   * Rejected sizes / memory pressure must not silently detach the app's
   * context or destroy the last usable surface. Both targets count against
   * the memory budget during this short transition. */
  Surface replacement={.x11=s->x11,.display=s->display,.config=s->config,
   .window=s->window,.cpu=MAP_FAILED,.ion=-1,.dma=-1};
  if(!allocate_buffer(&replacement,a.width,a.height))return EGL_FALSE;
  if(!R(eglMakeCurrent)(d,replacement.real,replacement.real,ctx)){
   free_buffer(&replacement);return EGL_FALSE;
  }
  Surface old=*s;
  replacement.next=s->next;replacement.gc=s->gc;replacement.exposure=s->exposure;
  replacement.interval=s->interval;replacement.pending=s->pending;
  *s=replacement;
  /* XShmCreateImage retains the address of its XShmSegmentInfo. Rebase
   * that pointer after transferring the replacement's buffer ownership. */
  if(s->shm_attached)s->image->obdata=(XPointer)&s->shm;
  if(old.shm_attached)old.image->obdata=(XPointer)&old.shm;
  free_buffer(&old);
  fprintf(stderr,"DOVE_EGL_WINDOW_RESIZED %dx%d\n",s->width,s->height);return EGL_TRUE;
 }
 double t=seconds();
 if(R(eglGetCurrentSurface)(EGL_DRAW)!=s->real)return fail(EGL_BAD_ACCESS);
 /* No X11 consumer while unmapped. Retire GPU commands normally, but
  * avoid CPU readback/conversion and invalidate staging for the next map. */
 if(a.map_state!=IsViewable){s->initialized=0;s->damage_set=0;EGLBoolean ok=R(eglSwapBuffers)(d,s->real);pace(s);return ok;}
 /* A private connection observes exposure without consuming app events. */
 if(s->exposure){XSync(s->exposure,False);XEvent event;while(XCheckWindowEvent(s->exposure,s->window,ExposureMask,&event))s->initialized=0;}
 else s->initialized=0; /* Without exposure tracking, preserve full refreshes. */
 int count=s->initialized&&s->damage_set?s->damage_count:1;
 int rects[64];
 if(s->initialized&&s->damage_set)memcpy(rects,s->damage_rects,sizeof rects);
 else{rects[0]=0;rects[1]=0;rects[2]=s->width;rects[3]=s->height;}
 s->damage_set=0;
 int present_count=0,present_rects[64];
 GLint alignment=4;R(glGetIntegerv)(GL_PACK_ALIGNMENT,&alignment);
 R(glPixelStorei)(GL_PACK_ALIGNMENT,1);
 uint8_t *dest=s->shm_attached?(uint8_t*)s->image->data:s->cpu;
 for(int i=0;i<count;i++){
  int l=rects[i*4],top=rects[i*4+1],w=rects[i*4+2],h=rects[i*4+3];
  double rt=seconds();
  R(glReadPixels)(l,s->height-top-h,w,h,GL_RGBA,GL_UNSIGNED_BYTE,s->readback);
  s->read_ms+=(seconds()-rt)*1000;rt=seconds();
  s->read_pixels+=(unsigned long long)w*h;
  /* Convert each row in place, and send only changed rows to X11. */
  int dirty_top=h,dirty_bottom=0;
  for(int y=0;y<h;y++){
   uint8_t *src=(uint8_t*)s->readback+(size_t)(h-y-1)*w*4;
   uint8_t *dst=dest+(size_t)(top+y)*s->image->bytes_per_line+l*4;int x=0;
   int changed=!s->initialized;
   int samples[3]={0,w/2,w-1};
   for(int k=0;k<3&&!changed;k++){int z=samples[k]*4;changed=dst[z]!=src[z+2]||dst[z+1]!=src[z+1]||dst[z+2]!=src[z]||dst[z+3]!=src[z+3];}
   uint8_t *out=changed?dst:src;
   for(;x+16<=w;x+=16){uint8x16x4_t v=vld4q_u8(src+x*4);uint8x16_t r=v.val[0];v.val[0]=v.val[2];v.val[2]=r;vst4q_u8(out+x*4,v);}
   for(;x<w;x++){uint8_t r=src[x*4],g=src[x*4+1],b=src[x*4+2],a=src[x*4+3];out[x*4]=b;out[x*4+1]=g;out[x*4+2]=r;out[x*4+3]=a;}
   if(!changed&&memcmp(dst,src,(size_t)w*4)){memcpy(dst,src,(size_t)w*4);changed=1;}
   if(changed){if(y<dirty_top)dirty_top=y;dirty_bottom=y+1;}
  }
  if(dirty_bottom>dirty_top){int *r=present_rects+present_count++*4;r[0]=l;r[1]=top+dirty_top;r[2]=w;r[3]=dirty_bottom-dirty_top;}
  s->convert_ms+=(seconds()-rt)*1000;
 }
 R(glPixelStorei)(GL_PACK_ALIGNMENT,alignment);
 GLenum read_error=R(glGetError)();
 if(read_error!=GL_NO_ERROR){fprintf(stderr,"DOVE_EGL_READBACK_ERROR=%x\n",read_error);return fail(EGL_BAD_ACCESS);}
 count=present_count;memcpy(rects,present_rects,(size_t)count*4*sizeof(int));
 s->initialized=1;
 s->wait_ms+=(seconds()-t)*1000;t=seconds();
 if(a.map_state==IsViewable){
  if(s->shm_attached){for(int i=0;i<count;i++)XShmPutImage(s->x11,s->window,s->gc,s->image,rects[i*4],rects[i*4+1],rects[i*4],rects[i*4+1],rects[i*4+2],rects[i*4+3],False);if(count)XFlush(s->x11);}
  else{for(int i=0;i<count;i++)XPutImage(s->x11,s->window,s->gc,s->image,rects[i*4],rects[i*4+1],rects[i*4],rects[i*4+1],rects[i*4+2],rects[i*4+3]);if(count)XFlush(s->x11);}
 }
 /* Keep the vendor driver's frame lifecycle even for pbuffer targets.
  * Presentation has already consumed the pixels; standard pbuffer swap is
  * valid and lets the driver retire its per-frame command resources. */
 if(!R(eglSwapBuffers)(d,s->real))return EGL_FALSE;
 s->present_ms+=(seconds()-t)*1000;s->frames++;
 if(!s->measured)s->measured=seconds();
 if(seconds()-s->measured>5){fprintf(stderr,"PROFILE read_ms=%.3f convert_ms=%.3f\n",s->read_ms/s->frames,s->convert_ms/s->frames);s->read_ms=s->convert_ms=0;fprintf(stderr,"DOVE_EGL_TIMING wait_ms=%.3f present_ms=%.3f frames=%u shm=%d read_pixels=%llu\n",s->wait_ms/s->frames,s->present_ms/s->frames,s->frames,s->shm_attached,s->read_pixels);s->wait_ms=s->present_ms=0;s->frames=0;s->read_pixels=0;s->measured=seconds();}
 pace(s);return EGL_TRUE;
}
EGLBoolean eglSurfaceAttrib(EGLDisplay d,EGLSurface surface,EGLint attr,EGLint value){return R(eglSurfaceAttrib)(d,underlying(surface),attr,value);}
EGLBoolean eglBindTexImage(EGLDisplay d,EGLSurface surface,EGLint buffer){return R(eglBindTexImage)(d,underlying(surface),buffer);}
EGLBoolean eglReleaseTexImage(EGLDisplay d,EGLSurface surface,EGLint buffer){return R(eglReleaseTexImage)(d,underlying(surface),buffer);}
EGLBoolean eglWaitNative(EGLint engine){if(xdisplay&&engine==EGL_CORE_NATIVE_ENGINE){XSync(xdisplay,False);for(Surface *s=surfaces;s;s=s->next)s->initialized=0;return EGL_TRUE;}return R(eglWaitNative)(engine);}
EGLBoolean eglTerminate(EGLDisplay d){
 /* This adapter deliberately refuses cross-thread termination; it must not
  * free staging while another thread still has a current context. */
 for(Binding *b=bindings;b;b=b->next)if(b->display==d&&b->context&&!pthread_equal(b->thread,pthread_self()))return fail(EGL_BAD_ACCESS);
 if(!eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT))return EGL_FALSE;
 while(surfaces)remove_surface(surfaces);
 EGLBoolean result=R(eglTerminate)(d);if(owned_display)XCloseDisplay(owned_display);xdisplay=owned_display=NULL;return result;
}
/* Private optional contract used by our Xephyr frontend. Coordinates are
 * top-left X11 rectangles. Invalid input retains the full-frame fallback. */
int dove_egl_set_damage(const int *rects,int count){
 Surface *s=lookup(eglGetCurrentSurface(EGL_DRAW));if(!s||count<0||count>16||(!rects&&count))return 0;
 for(int i=0;i<count;i++)if(rects[i*4]<0||rects[i*4+1]<0||rects[i*4+2]<1||rects[i*4+3]<1||rects[i*4+2]>s->width||rects[i*4+3]>s->height||rects[i*4]>s->width-rects[i*4+2]||rects[i*4+1]>s->height-rects[i*4+3])return 0;
 if(count){memcpy(s->damage_rects,rects,(size_t)count*4*sizeof(int));}
 s->damage_count=count;s->damage_set=1;return 1;
}
/* Explicit entry points keep libMali out of DT_NEEDED. Loading the vendor
 * through deep binding prevents GL4ES's GL names interposing driver internals. */
EGLBoolean eglGetConfigs(EGLDisplay d,EGLConfig *c,EGLint n,EGLint *out){return R(eglGetConfigs)(d,c,n,out);}
EGLBoolean eglBindAPI(EGLenum api){return R(eglBindAPI)(api);}
EGLenum eglQueryAPI(void){return R(eglQueryAPI)();}
EGLSurface eglCreatePbufferSurface(EGLDisplay d,EGLConfig c,const EGLint *a){return R(eglCreatePbufferSurface)(d,c,a);}
EGLSurface eglCreatePbufferFromClientBuffer(EGLDisplay d,EGLenum t,EGLClientBuffer b,EGLConfig c,const EGLint *a){return R(eglCreatePbufferFromClientBuffer)(d,t,b,c,a);}
EGLContext eglCreateContext(EGLDisplay d,EGLConfig c,EGLContext share,const EGLint *a){return R(eglCreateContext)(d,c,share,a);}
EGLBoolean eglDestroyContext(EGLDisplay d,EGLContext c){return R(eglDestroyContext)(d,c);}
EGLBoolean eglQueryContext(EGLDisplay d,EGLContext c,EGLint a,EGLint *v){return R(eglQueryContext)(d,c,a,v);}
EGLContext eglGetCurrentContext(void){return R(eglGetCurrentContext)();}
EGLDisplay eglGetCurrentDisplay(void){return R(eglGetCurrentDisplay)();}
const char *eglQueryString(EGLDisplay d,EGLint name){
 if(d==EGL_NO_DISPLAY && name==EGL_EXTENSIONS && getenv("DOVE_EGL_X11_PLATFORM_COMPAT"))
  return "EGL_EXT_client_extensions EGL_EXT_platform_base EGL_EXT_platform_x11";
 return R(eglQueryString)(d,name);
}
EGLBoolean eglWaitGL(void){return R(eglWaitGL)();}
EGLBoolean eglWaitClient(void){return R(eglWaitClient)();}
EGLBoolean eglReleaseThread(void){
 EGLBoolean result=R(eglReleaseThread)();
 if(result){
  pthread_once(&binding_once,create_binding_key);
  Binding *b=binding_key_valid?pthread_getspecific(binding_key):NULL;
  if(b){b->draw=b->read=EGL_NO_SURFACE;b->context=EGL_NO_CONTEXT;b->display=EGL_NO_DISPLAY;}
  reap_pending();
 }
 return result;
}
static void binding_destructor(void *opaque){
 Binding *b=opaque;
 pthread_mutex_lock(&registry_lock);
 /* Vendor release also clears this thread's context when an app omits its
  * explicit eglReleaseThread at pthread exit. On failure retain resources. */
 if(R(eglReleaseThread)()){
  Binding **p=&bindings;while(*p&&*p!=b)p=&(*p)->next;if(*p)*p=b->next;
  free(b);reap_pending();
 }
 pthread_mutex_unlock(&registry_lock);
}

#undef eglGetDisplay
#undef eglInitialize
#undef eglGetError
#undef eglChooseConfig
#undef eglGetConfigAttrib
#undef eglCreateWindowSurface
#undef eglCreatePixmapSurface
#undef eglCopyBuffers
#undef eglMakeCurrent
#undef eglGetCurrentSurface
#undef eglDestroySurface
#undef eglQuerySurface
#undef eglSwapInterval
#undef eglSwapBuffers
#undef eglSurfaceAttrib
#undef eglBindTexImage
#undef eglReleaseTexImage
#undef eglWaitNative
#undef eglTerminate
#undef eglGetProcAddress
#undef dove_egl_set_damage
#undef eglGetConfigs
#undef eglBindAPI
#undef eglQueryAPI
#undef eglCreatePbufferSurface
#undef eglCreatePbufferFromClientBuffer
#undef eglCreateContext
#undef eglDestroyContext
#undef eglQueryContext
#undef eglGetCurrentContext
#undef eglGetCurrentDisplay
#undef eglQueryString
#undef eglWaitGL
#undef eglWaitClient
#undef eglReleaseThread

EGLDisplay eglGetDisplay(EGLNativeDisplayType native){pthread_mutex_lock(&registry_lock);EGLDisplay result=impl_eglGetDisplay(native);pthread_mutex_unlock(&registry_lock);return result;}
EGLBoolean eglInitialize(EGLDisplay d,EGLint *major,EGLint *minor){pthread_mutex_lock(&registry_lock);EGLBoolean result=impl_eglInitialize(d,major,minor);pthread_mutex_unlock(&registry_lock);return result;}
EGLint eglGetError(void){pthread_mutex_lock(&registry_lock);EGLint result=impl_eglGetError();pthread_mutex_unlock(&registry_lock);return result;}
EGLBoolean eglChooseConfig(EGLDisplay d,const EGLint *attrs,EGLConfig *configs,EGLint max,EGLint *count){pthread_mutex_lock(&registry_lock);EGLBoolean result=impl_eglChooseConfig(d,attrs,configs,max,count);pthread_mutex_unlock(&registry_lock);return result;}
EGLBoolean eglGetConfigAttrib(EGLDisplay d,EGLConfig cfg,EGLint attr,EGLint *out){pthread_mutex_lock(&registry_lock);EGLBoolean result=impl_eglGetConfigAttrib(d,cfg,attr,out);pthread_mutex_unlock(&registry_lock);return result;}
EGLSurface eglCreateWindowSurface(EGLDisplay d,EGLConfig cfg,EGLNativeWindowType win,const EGLint *attrs){pthread_mutex_lock(&registry_lock);EGLSurface result=impl_eglCreateWindowSurface(d,cfg,win,attrs);pthread_mutex_unlock(&registry_lock);return result;}
EGLSurface eglCreatePixmapSurface(EGLDisplay d,EGLConfig cfg,EGLNativePixmapType pix,const EGLint *attrs){pthread_mutex_lock(&registry_lock);EGLSurface result=impl_eglCreatePixmapSurface(d,cfg,pix,attrs);pthread_mutex_unlock(&registry_lock);return result;}
EGLBoolean eglCopyBuffers(EGLDisplay d,EGLSurface surface,EGLNativePixmapType pix){pthread_mutex_lock(&registry_lock);EGLBoolean result=impl_eglCopyBuffers(d,surface,pix);pthread_mutex_unlock(&registry_lock);return result;}
EGLBoolean eglMakeCurrent(EGLDisplay d,EGLSurface draw,EGLSurface read,EGLContext context){pthread_mutex_lock(&registry_lock);EGLBoolean result=impl_eglMakeCurrent(d,draw,read,context);pthread_mutex_unlock(&registry_lock);return result;}
EGLSurface eglGetCurrentSurface(EGLint type){pthread_mutex_lock(&registry_lock);EGLSurface result=impl_eglGetCurrentSurface(type);pthread_mutex_unlock(&registry_lock);return result;}
EGLBoolean eglDestroySurface(EGLDisplay d,EGLSurface surface){pthread_mutex_lock(&registry_lock);EGLBoolean result=impl_eglDestroySurface(d,surface);pthread_mutex_unlock(&registry_lock);return result;}
EGLBoolean eglQuerySurface(EGLDisplay d,EGLSurface surface,EGLint attr,EGLint *out){pthread_mutex_lock(&registry_lock);EGLBoolean result=impl_eglQuerySurface(d,surface,attr,out);pthread_mutex_unlock(&registry_lock);return result;}
EGLBoolean eglSwapInterval(EGLDisplay d,EGLint interval){pthread_mutex_lock(&registry_lock);EGLBoolean result=impl_eglSwapInterval(d,interval);pthread_mutex_unlock(&registry_lock);return result;}
EGLBoolean eglSwapBuffers(EGLDisplay d,EGLSurface surface){pthread_mutex_lock(&registry_lock);EGLBoolean result=impl_eglSwapBuffers(d,surface);pthread_mutex_unlock(&registry_lock);return result;}
EGLBoolean eglSurfaceAttrib(EGLDisplay d,EGLSurface surface,EGLint attr,EGLint value){pthread_mutex_lock(&registry_lock);EGLBoolean result=impl_eglSurfaceAttrib(d,surface,attr,value);pthread_mutex_unlock(&registry_lock);return result;}
EGLBoolean eglBindTexImage(EGLDisplay d,EGLSurface surface,EGLint buffer){pthread_mutex_lock(&registry_lock);EGLBoolean result=impl_eglBindTexImage(d,surface,buffer);pthread_mutex_unlock(&registry_lock);return result;}
EGLBoolean eglReleaseTexImage(EGLDisplay d,EGLSurface surface,EGLint buffer){pthread_mutex_lock(&registry_lock);EGLBoolean result=impl_eglReleaseTexImage(d,surface,buffer);pthread_mutex_unlock(&registry_lock);return result;}
EGLBoolean eglWaitNative(EGLint engine){pthread_mutex_lock(&registry_lock);EGLBoolean result=impl_eglWaitNative(engine);pthread_mutex_unlock(&registry_lock);return result;}
EGLBoolean eglTerminate(EGLDisplay d){pthread_mutex_lock(&registry_lock);EGLBoolean result=impl_eglTerminate(d);pthread_mutex_unlock(&registry_lock);return result;}
int dove_egl_set_damage(const int *rects,int count){pthread_mutex_lock(&registry_lock);int result=impl_dove_egl_set_damage(rects,count);pthread_mutex_unlock(&registry_lock);return result;}
EGLBoolean eglGetConfigs(EGLDisplay d,EGLConfig *c,EGLint n,EGLint *out){pthread_mutex_lock(&registry_lock);EGLBoolean result=impl_eglGetConfigs(d,c,n,out);pthread_mutex_unlock(&registry_lock);return result;}
EGLBoolean eglBindAPI(EGLenum api){pthread_mutex_lock(&registry_lock);EGLBoolean result=impl_eglBindAPI(api);pthread_mutex_unlock(&registry_lock);return result;}
EGLenum eglQueryAPI(void){pthread_mutex_lock(&registry_lock);EGLenum result=impl_eglQueryAPI();pthread_mutex_unlock(&registry_lock);return result;}
EGLSurface eglCreatePbufferSurface(EGLDisplay d,EGLConfig c,const EGLint *a){pthread_mutex_lock(&registry_lock);EGLSurface result=impl_eglCreatePbufferSurface(d,c,a);pthread_mutex_unlock(&registry_lock);return result;}
EGLSurface eglCreatePbufferFromClientBuffer(EGLDisplay d,EGLenum t,EGLClientBuffer b,EGLConfig c,const EGLint *a){pthread_mutex_lock(&registry_lock);EGLSurface result=impl_eglCreatePbufferFromClientBuffer(d,t,b,c,a);pthread_mutex_unlock(&registry_lock);return result;}
EGLContext eglCreateContext(EGLDisplay d,EGLConfig c,EGLContext share,const EGLint *a){pthread_mutex_lock(&registry_lock);EGLContext result=impl_eglCreateContext(d,c,share,a);pthread_mutex_unlock(&registry_lock);return result;}
EGLBoolean eglDestroyContext(EGLDisplay d,EGLContext c){pthread_mutex_lock(&registry_lock);EGLBoolean result=impl_eglDestroyContext(d,c);pthread_mutex_unlock(&registry_lock);return result;}
EGLBoolean eglQueryContext(EGLDisplay d,EGLContext c,EGLint a,EGLint *v){pthread_mutex_lock(&registry_lock);EGLBoolean result=impl_eglQueryContext(d,c,a,v);pthread_mutex_unlock(&registry_lock);return result;}
EGLContext eglGetCurrentContext(void){pthread_mutex_lock(&registry_lock);EGLContext result=impl_eglGetCurrentContext();pthread_mutex_unlock(&registry_lock);return result;}
EGLDisplay eglGetCurrentDisplay(void){pthread_mutex_lock(&registry_lock);EGLDisplay result=impl_eglGetCurrentDisplay();pthread_mutex_unlock(&registry_lock);return result;}
const char *eglQueryString(EGLDisplay d,EGLint name){pthread_mutex_lock(&registry_lock);const char * result=impl_eglQueryString(d,name);pthread_mutex_unlock(&registry_lock);return result;}
EGLBoolean eglWaitGL(void){pthread_mutex_lock(&registry_lock);EGLBoolean result=impl_eglWaitGL();pthread_mutex_unlock(&registry_lock);return result;}
EGLBoolean eglWaitClient(void){pthread_mutex_lock(&registry_lock);EGLBoolean result=impl_eglWaitClient();pthread_mutex_unlock(&registry_lock);return result;}
EGLBoolean eglReleaseThread(void){pthread_mutex_lock(&registry_lock);EGLBoolean result=impl_eglReleaseThread();pthread_mutex_unlock(&registry_lock);return result;}

/* Opt-in single-screen X11 compatibility for clients requiring the EXT
 * platform discovery API. Does not claim EGL 1.5 or native pixmap support. */
EGLDisplay eglGetPlatformDisplayEXT(EGLenum platform,void *native,const EGLint *attrs){
 pthread_mutex_lock(&registry_lock);
 EGLDisplay result=EGL_NO_DISPLAY;int screen=-1;
 if(platform!=0x31D5){fail(EGL_BAD_PARAMETER);goto done;}
 if(attrs)for(int i=0;attrs[i]!=EGL_NONE;i+=2){
  if(i>=64||attrs[i]!=0x31D6){fail(EGL_BAD_ATTRIBUTE);goto done;}
  screen=attrs[i+1];if(screen<0){fail(EGL_BAD_ATTRIBUTE);goto done;}
 }
 if(native && screen>=ScreenCount((Display*)native)){fail(EGL_BAD_ATTRIBUTE);goto done;}
 /* The production server has one screen. Refuse other screens rather than
  * silently returning the default screen's visual/configuration. */
 if(native && screen>=0 && screen!=DefaultScreen((Display*)native))goto done;
 if(!native && screen>0)goto done;
 result=eglGetDisplay((EGLNativeDisplayType)native);
 done:pthread_mutex_unlock(&registry_lock);return result;
}
EGLSurface eglCreatePlatformWindowSurfaceEXT(EGLDisplay d,EGLConfig c,void *native,const EGLint *attrs){
 pthread_mutex_lock(&registry_lock);EGLSurface result=EGL_NO_SURFACE;
 if(!native)fail(EGL_BAD_NATIVE_WINDOW);
 else result=eglCreateWindowSurface(d,c,*(Window*)native,attrs);
 pthread_mutex_unlock(&registry_lock);return result;
}
EGLSurface eglCreatePlatformPixmapSurfaceEXT(EGLDisplay d,EGLConfig c,void *native,const EGLint *attrs){
 (void)d;(void)c;(void)native;(void)attrs;
 pthread_mutex_lock(&registry_lock);fail(EGL_BAD_MATCH);pthread_mutex_unlock(&registry_lock);return EGL_NO_SURFACE;
}
__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *name){
 if(!name)return NULL;
#define PROC(fn) if(!strcmp(name,#fn))return (__eglMustCastToProperFunctionPointerType)fn
 PROC(eglGetPlatformDisplayEXT);PROC(eglCreatePlatformWindowSurfaceEXT);PROC(eglCreatePlatformPixmapSurfaceEXT);
 PROC(eglGetDisplay);PROC(eglInitialize);PROC(eglGetError);PROC(eglChooseConfig);PROC(eglGetConfigAttrib);PROC(eglCreateWindowSurface);PROC(eglCreatePixmapSurface);PROC(eglCopyBuffers);PROC(eglMakeCurrent);PROC(eglGetCurrentSurface);PROC(eglDestroySurface);PROC(eglQuerySurface);PROC(eglSwapInterval);PROC(eglSwapBuffers);PROC(eglSurfaceAttrib);PROC(eglBindTexImage);PROC(eglReleaseTexImage);PROC(eglWaitNative);PROC(eglTerminate);PROC(eglReleaseThread);
#undef PROC
 pthread_mutex_lock(&registry_lock);
 __eglMustCastToProperFunctionPointerType result=R(eglGetProcAddress)(name);
 pthread_mutex_unlock(&registry_lock);return result;
}

