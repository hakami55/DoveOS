/* Experimental X11 window presentation for the stock API-600 Mali driver.
 * GPU drawing targets a native ION pixmap; XPutImage presents its CPU mapping.
 * This supplies GLES windows, not a GPU-accelerated X server or GLX driver. */
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
typedef struct {uint32_t h,w,bytespp;uint8_t bits,r,g,b,a,l,pad[2];uint32_t flags;void *memory;uint32_t yuv;} Native;
typedef struct Surface {
 struct Surface *next;Display *x11;EGLSurface real;EGLDisplay display;EGLConfig config;Window window;
 Native native;int ion,dma,width,height,pending;size_t bytes;uint32_t handle,physical;
 void *cpu,*base,*memory;XImage *image;GC gc;
 XShmSegmentInfo shm;int shm_attached;
 double wait_ms,present_ms,measured;unsigned frames;
 int interval;double next_swap;
} Surface;
static void *mali,*ionlib;static Display *xdisplay;static Display *owned_display;
static Surface *surfaces;static size_t owned_bytes;
static __thread EGLint error=EGL_SUCCESS;
static void *symbol(const char *name){
 if(!mali)mali=dlopen("libMali-r4p1-r1p1-trial.so",RTLD_NOW|RTLD_LOCAL|RTLD_DEEPBIND);
 if(!mali){error=EGL_NOT_INITIALIZED;return NULL;}return dlsym(mali,name);
}
#define R(name) ((__typeof__(&name))symbol(#name))
static EGLBoolean fail(EGLint e){error=e;return EGL_FALSE;}
static Surface *lookup(EGLSurface surface){for(Surface *s=surfaces;s;s=s->next)if((void*)s==surface)return s;return NULL;}
static EGLSurface underlying(EGLSurface surface){Surface *s=lookup(surface);return s?s->real:surface;}
static double seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static void free_buffer(Surface *s){
 if(s->real){R(eglDestroySurface)(s->display,s->real);s->real=EGL_NO_SURFACE;}
 if(s->shm_attached){XShmDetach(s->x11,&s->shm);XSync(s->x11,False);s->shm_attached=0;}
 if(s->image){s->image->data=NULL;XDestroyImage(s->image);s->image=NULL;}
 if(s->shm.shmaddr&&(void*)s->shm.shmaddr!=MAP_FAILED){shmdt(s->shm.shmaddr);s->shm.shmaddr=NULL;}
 if(s->memory){void(*f)(void*)=symbol("_mali_base_common_mem_free");f(s->memory);s->memory=NULL;}
 if(s->base){void(*f)(void*)=symbol("_mali_base_common_context_destroy");f(s->base);s->base=NULL;}
 if(s->cpu!=MAP_FAILED){munmap(s->cpu,s->bytes);s->cpu=MAP_FAILED;}
 if(s->physical){void(*forget)(uint32_t)=dlsym(RTLD_DEFAULT,"gpu_forget_buffer");if(forget)forget(s->physical);s->physical=0;}
 if(s->dma>=0){close(s->dma);s->dma=-1;}
 if(s->handle){int(*f)(int,uint32_t)=dlsym(ionlib,"ion_free");f(s->ion,s->handle);s->handle=0;}
 if(s->ion>=0){close(s->ion);s->ion=-1;}
 if(s->bytes){owned_bytes-=s->bytes;s->bytes=0;}
}
static int allocate_buffer(Surface *s,int w,int h){
 if(w<1||h<1||w>1920||h>1080)return fail(EGL_BAD_ALLOC);
 size_t bytes=((size_t)w*h*4+4095)&~4095;
 if(owned_bytes+bytes>32u*1024*1024)return fail(EGL_BAD_ALLOC);
 if(!ionlib)ionlib=dlopen("/usr/lib/libVOutWrapper.so",RTLD_LAZY|RTLD_LOCAL|RTLD_DEEPBIND);
 if(!ionlib)return fail(EGL_BAD_ALLOC);
 int(*alloc)(int,size_t,size_t,uint32_t,uint32_t,uint32_t*)=dlsym(ionlib,"ion_alloc");
 int(*phys)(int,uint32_t,uint32_t*,uint32_t*)=dlsym(ionlib,"ion_phys"),(*share)(int,uint32_t,int*)=dlsym(ionlib,"ion_share");
 void(*register_buffer)(int,uint32_t,uint32_t)=dlsym(RTLD_DEFAULT,"gpu_share_buffer");
 void*(*create)(void)=symbol("_mali_base_common_context_create");
 void*(*map)(void*,uint32_t,uint32_t,void*,uint32_t)=symbol("_mali_base_common_mem_add_phys_mem");
 if(!alloc||!phys||!share||!register_buffer||!create||!map)return fail(EGL_BAD_ALLOC);
 s->bytes=bytes;owned_bytes+=bytes;s->ion=open("/dev/ion",O_RDWR|O_CLOEXEC);uint32_t actual=0;
 if(s->ion<0||alloc(s->ion,bytes,4096,0x80,0x80,&s->handle)||phys(s->ion,s->handle,&s->physical,&actual)||actual!=bytes||share(s->ion,s->handle,&s->dma))goto bad;
 s->cpu=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,s->dma,0);if(s->cpu==MAP_FAILED)goto bad;
 memset(s->cpu,0,bytes);register_buffer(s->dma,s->physical,actual);
 s->base=create();if(!s->base)goto bad;s->memory=map(s->base,s->physical,actual,s->cpu,0x32);if(!s->memory)goto bad;
 s->native=(Native){.h=h,.w=w,.bytespp=4,.bits=32,.r=8,.g=8,.b=8,.a=8,.flags=1,.memory=s->memory};
 s->real=R(eglCreatePixmapSurface)(s->display,s->config,(EGLNativePixmapType)(uintptr_t)&s->native,NULL);if(!s->real)goto bad;
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
 s->width=w;s->height=h;return 1;
bad:free_buffer(s);return fail(EGL_BAD_ALLOC);
}
static void remove_surface(Surface *s){
 Surface **p=&surfaces;while(*p&&*p!=s)p=&(*p)->next;if(*p)*p=s->next;
 free_buffer(s);if(s->gc)XFreeGC(s->x11,s->gc);free(s);
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
  if(key==EGL_SURFACE_TYPE&&(v&EGL_WINDOW_BIT)){window=1;v=(v&~EGL_WINDOW_BIT)|EGL_PIXMAP_BIT;}
  copy[n++]=key;copy[n++]=v;
 }
 if(!has_surface){window=1;copy[n++]=EGL_SURFACE_TYPE;copy[n++]=EGL_PIXMAP_BIT;}
 if(window){
  const EGLint channels[]={EGL_RED_SIZE,EGL_GREEN_SIZE,EGL_BLUE_SIZE,EGL_ALPHA_SIZE};
  for(unsigned k=0;k<4;k++){int found=0;for(int i=0;i<n;i+=2)if(copy[i]==channels[k]){if(copy[i+1]<8)copy[i+1]=8;found=1;}if(!found){copy[n++]=channels[k];copy[n++]=8;}}
 }
 copy[n]=EGL_NONE;return R(eglChooseConfig)(d,copy,configs,max,count);
}
EGLBoolean eglGetConfigAttrib(EGLDisplay d,EGLConfig cfg,EGLint attr,EGLint *out){
 if(!out)return fail(EGL_BAD_PARAMETER);
 if(attr==EGL_NATIVE_VISUAL_ID&&xdisplay){*out=(EGLint)XVisualIDFromVisual(DefaultVisual(xdisplay,DefaultScreen(xdisplay)));return EGL_TRUE;}
 EGLBoolean ok=R(eglGetConfigAttrib)(d,cfg,attr,out);if(ok&&attr==EGL_SURFACE_TYPE&&(*out&EGL_PIXMAP_BIT))*out|=EGL_WINDOW_BIT;return ok;
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
 s->next=surfaces;surfaces=s;fprintf(stderr,"DOVE_EGL_WINDOW_READY %dx%d hardware=Mali-400\n",s->width,s->height);return (EGLSurface)s;
}
/* An X11 Pixmap is an XID, not the private descriptor expected by this blob.
 * Refuse this unsupported path instead of passing an integer as a pointer. */
EGLSurface eglCreatePixmapSurface(EGLDisplay d,EGLConfig cfg,EGLNativePixmapType pix,const EGLint *attrs){
 (void)d;(void)cfg;(void)pix;(void)attrs;fail(EGL_BAD_NATIVE_PIXMAP);return EGL_NO_SURFACE;
}
EGLBoolean eglCopyBuffers(EGLDisplay d,EGLSurface surface,EGLNativePixmapType pix){
 (void)d;(void)surface;(void)pix;return fail(EGL_BAD_NATIVE_PIXMAP);
}
EGLBoolean eglMakeCurrent(EGLDisplay d,EGLSurface draw,EGLSurface read,EGLContext context){
 EGLBoolean ok=R(eglMakeCurrent)(d,underlying(draw),underlying(read),context);
 if(ok){EGLSurface actual_draw=R(eglGetCurrentSurface)(EGL_DRAW),actual_read=R(eglGetCurrentSurface)(EGL_READ);Surface *s=surfaces,*next;
  while(s){next=s->next;if(s->pending&&s->real!=actual_draw&&s->real!=actual_read)remove_surface(s);s=next;}
 }return ok;
}
EGLSurface eglGetCurrentSurface(EGLint type){EGLSurface real=R(eglGetCurrentSurface)(type);for(Surface *s=surfaces;s;s=s->next)if(s->real==real)return (EGLSurface)s;return real;}
EGLBoolean eglDestroySurface(EGLDisplay d,EGLSurface surface){
 Surface *s=lookup(surface);if(!s)return R(eglDestroySurface)(d,surface);
 if(s->pending)return fail(EGL_BAD_SURFACE);
 if(R(eglGetCurrentSurface)(EGL_DRAW)==s->real||R(eglGetCurrentSurface)(EGL_READ)==s->real){s->pending=1;return EGL_TRUE;}
 remove_surface(s);return EGL_TRUE;
}
EGLBoolean eglQuerySurface(EGLDisplay d,EGLSurface surface,EGLint attr,EGLint *out){
 Surface *s=lookup(surface);if(s){if(!out)return fail(EGL_BAD_PARAMETER);
  if(attr==EGL_WIDTH||attr==EGL_HEIGHT){XWindowAttributes a;if(!XGetWindowAttributes(s->x11,s->window,&a))return fail(EGL_BAD_NATIVE_WINDOW);*out=attr==EGL_WIDTH?a.width:a.height;return EGL_TRUE;}
  if(attr==EGL_RENDER_BUFFER){*out=EGL_BACK_BUFFER;return EGL_TRUE;}
 }return R(eglQuerySurface)(d,underlying(surface),attr,out);
}
EGLBoolean eglSwapInterval(EGLDisplay d,EGLint interval){
 Surface *s=lookup(eglGetCurrentSurface(EGL_DRAW));if(s){if(interval<0||interval>4)return fail(EGL_BAD_PARAMETER);s->interval=interval;s->next_swap=0;return EGL_TRUE;}
 return R(eglSwapInterval)(d,interval);
}
EGLBoolean eglSwapBuffers(EGLDisplay d,EGLSurface surface){
 Surface *s=lookup(surface);if(!s)return R(eglSwapBuffers)(d,surface);
 XWindowAttributes a;if(!XGetWindowAttributes(s->x11,s->window,&a))return fail(EGL_BAD_NATIVE_WINDOW);
 if(a.width!=s->width||a.height!=s->height){
  EGLContext ctx=R(eglGetCurrentContext)();EGLSurface draw=R(eglGetCurrentSurface)(EGL_DRAW),read=R(eglGetCurrentSurface)(EGL_READ);int was_draw=draw==s->real,was_read=read==s->real;
  if(!was_draw||!was_read)return fail(EGL_BAD_ACCESS);
  if(!R(eglMakeCurrent)(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT))return EGL_FALSE;
  free_buffer(s);if(!allocate_buffer(s,a.width,a.height))return EGL_FALSE;
  if(!R(eglMakeCurrent)(d,s->real,s->real,ctx))return EGL_FALSE;
  fprintf(stderr,"DOVE_EGL_WINDOW_RESIZED %dx%d\n",s->width,s->height);return EGL_TRUE;
 }
 double t=seconds();
 if(getenv("DOVE_EGL_USE_GL_FINISH")){
  void(*finish)(void)=symbol("glFinish");if(!finish)return fail(EGL_NOT_INITIALIZED);finish();
 }else if(!R(eglWaitClient)())return EGL_FALSE;
 s->wait_ms+=(seconds()-t)*1000;t=seconds();
 if(a.map_state==IsViewable){
  if(s->shm_attached){memcpy(s->image->data,s->cpu,(size_t)s->width*s->height*4);XShmPutImage(s->x11,s->window,s->gc,s->image,0,0,0,0,s->width,s->height,False);XSync(s->x11,False);}
  else{XPutImage(s->x11,s->window,s->gc,s->image,0,0,0,0,s->width,s->height);XFlush(s->x11);}
 }
 s->present_ms+=(seconds()-t)*1000;s->frames++;
 if(!s->measured)s->measured=seconds();
 if(seconds()-s->measured>5){fprintf(stderr,"DOVE_EGL_TIMING wait_ms=%.3f present_ms=%.3f frames=%u shm=%d\n",s->wait_ms/s->frames,s->present_ms/s->frames,s->frames,s->shm_attached);s->wait_ms=s->present_ms=0;s->frames=0;s->measured=seconds();}
 if(s->interval){double now=seconds();if(s->next_swap>now&&s->next_swap-now<.1){double delay=s->next_swap-now;struct timespec t={(time_t)delay,(long)((delay-(time_t)delay)*1e9)};while(nanosleep(&t,&t)&&errno==EINTR){}}
  now=seconds();s->next_swap=(s->next_swap>now-.1?s->next_swap:now)+s->interval/60.0;
 }return EGL_TRUE;
}
EGLBoolean eglSurfaceAttrib(EGLDisplay d,EGLSurface surface,EGLint attr,EGLint value){return R(eglSurfaceAttrib)(d,underlying(surface),attr,value);}
EGLBoolean eglBindTexImage(EGLDisplay d,EGLSurface surface,EGLint buffer){return R(eglBindTexImage)(d,underlying(surface),buffer);}
EGLBoolean eglReleaseTexImage(EGLDisplay d,EGLSurface surface,EGLint buffer){return R(eglReleaseTexImage)(d,underlying(surface),buffer);}
EGLBoolean eglWaitNative(EGLint engine){if(xdisplay&&engine==EGL_CORE_NATIVE_ENGINE){XSync(xdisplay,False);return EGL_TRUE;}return R(eglWaitNative)(engine);}
EGLBoolean eglTerminate(EGLDisplay d){
 R(eglMakeCurrent)(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);while(surfaces)remove_surface(surfaces);
 EGLBoolean result=R(eglTerminate)(d);if(owned_display)XCloseDisplay(owned_display);xdisplay=owned_display=NULL;return result;
}
__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *name){
 if(!name)return NULL;
#define PROC(fn) if(!strcmp(name,#fn))return (__eglMustCastToProperFunctionPointerType)fn
 PROC(eglGetDisplay);PROC(eglInitialize);PROC(eglGetError);PROC(eglChooseConfig);PROC(eglGetConfigAttrib);PROC(eglCreateWindowSurface);PROC(eglCreatePixmapSurface);PROC(eglCopyBuffers);PROC(eglMakeCurrent);PROC(eglGetCurrentSurface);PROC(eglDestroySurface);PROC(eglQuerySurface);PROC(eglSwapInterval);PROC(eglSwapBuffers);PROC(eglSurfaceAttrib);PROC(eglBindTexImage);PROC(eglReleaseTexImage);PROC(eglWaitNative);PROC(eglTerminate);
#undef PROC
 return R(eglGetProcAddress)(name);
}
