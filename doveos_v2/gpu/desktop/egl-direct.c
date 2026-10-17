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
typedef struct {uint32_t h,w,bytespp;uint8_t bits,r,g,b,a,l,pad[2];uint32_t flags;void *memory;uint32_t yuv;} Native;
typedef struct Surface {
 struct Surface *next;Display *x11;EGLSurface real;EGLDisplay display;EGLConfig config;Window window;
 Native native;int ion,dma,width,height,pending;size_t bytes;uint32_t handle,physical;
 void *cpu,*base,*memory,*readback;XImage *image;GC gc;
 XShmSegmentInfo shm;int shm_attached;
 double wait_ms,present_ms,measured;unsigned frames;
 int interval;double next_swap;int initialized,damage_set,damage_count,damage_rects[64];unsigned long long read_pixels;
} Surface;
static void *present_library;
static int (*present_frame)(int,int);
static void (*present_texture)(unsigned);
static unsigned source_texture;
static void (*present_damage)(const int*,int);
static void (*present_close)(void);
static void *mali;static Display *xdisplay;static Display *owned_display;
static Surface *surfaces;static size_t owned_bytes;
static __thread EGLint error=EGL_SUCCESS;
static void *symbol(const char *name){
 if(!mali)mali=dlopen(getenv("DOVE_EGL_MALI_LIBRARY")?getenv("DOVE_EGL_MALI_LIBRARY"):"libMali-r4p1-r1p1-trial.so",RTLD_NOW|RTLD_LOCAL);
 if(!mali){error=EGL_NOT_INITIALIZED;return NULL;}return dlsym(mali,name);
}
#define R(name) ((__typeof__(&name))symbol(#name))
static EGLBoolean fail(EGLint e){error=e;return EGL_FALSE;}
static Surface *lookup(EGLSurface surface){for(Surface *s=surfaces;s;s=s->next)if((void*)s==surface)return s;return NULL;}
static EGLSurface underlying(EGLSurface surface){Surface *s=lookup(surface);return s?s->real:surface;}
static double seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
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
 if(present_close){present_close();present_close=NULL;present_frame=NULL;}
 if(s->real){R(eglDestroySurface)(s->display,s->real);s->real=EGL_NO_SURFACE;}
 if(s->shm_attached){XShmDetach(s->x11,&s->shm);XSync(s->x11,False);s->shm_attached=0;}
 if(s->image){s->image->data=NULL;XDestroyImage(s->image);s->image=NULL;}
 if(s->shm.shmaddr&&(void*)s->shm.shmaddr!=MAP_FAILED){shmdt(s->shm.shmaddr);s->shm.shmaddr=NULL;}
 if(s->cpu!=MAP_FAILED){free(s->cpu);s->cpu=MAP_FAILED;}
 free(s->readback);s->readback=NULL;
 if(s->bytes){owned_bytes-=s->bytes;s->bytes=0;}
}
static int allocate_buffer(Surface *s,int w,int h){
 if(w<1||h<1||w>1920||h>1080){fprintf(stderr,"DOVE_EGL_ALLOC_REJECT reason=dimensions w=%d h=%d\n",w,h);return fail(EGL_BAD_ALLOC);}
 size_t bytes=(size_t)w*h*4;
 /* Direct: RGBA pbuffer + RGBA Glamor root + two page-rounded NV12 outputs.
  * No CPU staging/readback/XShm image exists in this branch. Keep the same
  * per-process cap and 16 MiB free-memory reserve. Generic windows unchanged. */
 int tiny=getenv("DOVE_DIRECT_PRESENTER")&&getenv("DOVE_DIRECT_TEXTURE")&&getenv("DOVE_SMALL_PBUFFER");
 size_t budget=getenv("DOVE_DIRECT_PRESENTER") ? (tiny?32u*32*4:bytes)+bytes+2*(((size_t)w*h*3/2+4095)&~(size_t)4095) : bytes*4;
 if(owned_bytes+budget>64u*1024*1024){fprintf(stderr,"DOVE_EGL_ALLOC_REJECT reason=process_limit owned=%zu need=%zu\n",owned_bytes,budget);return fail(EGL_BAD_ALLOC);}
 size_t available=available_window_memory(),required=budget+16u*1024*1024;
 fprintf(stderr,"DOVE_EGL_ALLOC_CHECK w=%d h=%d owned=%zu required=%zu available=%zu direct=%d\n",w,h,owned_bytes,required,available,getenv("DOVE_DIRECT_PRESENTER")!=NULL);
 if(available<required){fprintf(stderr,"DOVE_EGL_ALLOC_REJECT reason=ordinary_memory\n");return fail(EGL_BAD_ALLOC);}
 s->bytes=budget;owned_bytes+=s->bytes;
 if(getenv("DOVE_DIRECT_PRESENTER")){
  if(!((w==1280&&h==720)||(w==1920&&h==1080)))goto bad;
  EGLint attrs[]={EGL_WIDTH,tiny?32:w,EGL_HEIGHT,tiny?32:h,EGL_TEXTURE_FORMAT,EGL_TEXTURE_RGBA,EGL_TEXTURE_TARGET,EGL_TEXTURE_2D,EGL_NONE};
  if(!getenv("DOVE_BIND_PBUFFER"))attrs[4]=EGL_NONE;
  s->real=R(eglCreatePbufferSurface)(s->display,s->config,attrs);
  if(!s->real){fprintf(stderr,"DOVE_EGL_ALLOC_REJECT reason=vendor_pbuffer error=%04x\n",R(eglGetError)());goto bad;}
  fprintf(stderr,"DOVE_EGL_DIRECT_SURFACE logical=%dx%d pbuffer=%dx%d budget=%zu\n",w,h,tiny?32:w,tiny?32:h,budget);
  s->width=w;s->height=h;return 1;
 }
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
  if(key==EGL_SURFACE_TYPE&&(v&EGL_WINDOW_BIT)){window=1;v=(v&~EGL_WINDOW_BIT)|EGL_PBUFFER_BIT;}
  copy[n++]=key;copy[n++]=v;
 }
 if(!has_surface){window=1;copy[n++]=EGL_SURFACE_TYPE;copy[n++]=EGL_PBUFFER_BIT;}
 if(window){
  const EGLint channels[]={EGL_RED_SIZE,EGL_GREEN_SIZE,EGL_BLUE_SIZE,EGL_ALPHA_SIZE};
  for(unsigned k=0;k<4;k++){int found=0;for(int i=0;i<n;i+=2)if(copy[i]==channels[k]){if(copy[i+1]<8)copy[i+1]=8;found=1;}if(!found){copy[n++]=channels[k];copy[n++]=8;}}
 }
 if(getenv("DOVE_BIND_PBUFFER")){copy[n++]=EGL_BIND_TO_TEXTURE_RGBA;copy[n++]=EGL_TRUE;}copy[n]=EGL_NONE;return R(eglChooseConfig)(d,copy,configs,max,count);
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
 if(getenv("DOVE_DIRECT_PRESENTER")){
  /* The small pbuffer has no screen image. Never silently present it if
   * the private Xephyr/native-texture contract is unavailable. */
  if(getenv("DOVE_SMALL_PBUFFER")&&!source_texture)return fail(EGL_BAD_ACCESS);
  if(!present_frame){
   present_library=dlopen(getenv("DOVE_DIRECT_PRESENTER"),RTLD_NOW|RTLD_LOCAL|RTLD_DEEPBIND);
   if(!present_library){fprintf(stderr,"PRESENTER_LOAD_FAILED=%s\n",dlerror());return fail(EGL_BAD_ACCESS);}
   present_frame=dlsym(present_library,"dove_present_frame");present_close=dlsym(present_library,"dove_present_close");
   present_damage=dlsym(present_library,"dove_present_damage");present_texture=dlsym(present_library,"dove_present_texture");
   if(!present_frame||!present_close)return fail(EGL_BAD_ACCESS);
  }
  if(present_damage){int full[4]={0,0,s->width,s->height};present_damage(s->initialized&&s->damage_set?s->damage_rects:full,s->initialized&&s->damage_set?s->damage_count:1);}
  s->damage_set=0;
  if(present_texture)present_texture(source_texture);
  if(present_frame(s->width,s->height))return fail(EGL_BAD_ACCESS);
  s->initialized=1;
  return R(eglSwapBuffers)(d,s->real);
 }
 double t=seconds();
 if(R(eglGetCurrentSurface)(EGL_DRAW)!=s->real)return fail(EGL_BAD_ACCESS);
 int count=s->initialized&&s->damage_set?s->damage_count:1;
 int rects[64];
 if(s->initialized&&s->damage_set)memcpy(rects,s->damage_rects,sizeof rects);
 else{rects[0]=0;rects[1]=0;rects[2]=s->width;rects[3]=s->height;}
 s->damage_set=0;
 GLint alignment=4;R(glGetIntegerv)(GL_PACK_ALIGNMENT,&alignment);
 R(glPixelStorei)(GL_PACK_ALIGNMENT,1);
 uint8_t *dest=s->shm_attached?(uint8_t*)s->image->data:s->cpu;
 for(int i=0;i<count;i++){
  int l=rects[i*4],top=rects[i*4+1],w=rects[i*4+2],h=rects[i*4+3];
  R(glReadPixels)(l,s->height-top-h,w,h,GL_RGBA,GL_UNSIGNED_BYTE,s->readback);
  s->read_pixels+=(unsigned long long)w*h;
  /* Standard readback is bottom-up RGBA; X11 is top-down BGRA. */
  for(int y=0;y<h;y++){
   const uint8_t *src=(const uint8_t*)s->readback+(size_t)(h-y-1)*w*4;
   uint8_t *dst=dest+(size_t)(top+y)*s->image->bytes_per_line+l*4;int x=0;
   for(;x+16<=w;x+=16){uint8x16x4_t v=vld4q_u8(src+x*4);uint8x16_t r=v.val[0];v.val[0]=v.val[2];v.val[2]=r;vst4q_u8(dst+x*4,v);}
   for(;x<w;x++){dst[x*4]=src[x*4+2];dst[x*4+1]=src[x*4+1];dst[x*4+2]=src[x*4];dst[x*4+3]=src[x*4+3];}
  }
 }
 R(glPixelStorei)(GL_PACK_ALIGNMENT,alignment);
 GLenum read_error=R(glGetError)();
 if(read_error!=GL_NO_ERROR){fprintf(stderr,"DOVE_EGL_READBACK_ERROR=%x\n",read_error);return fail(EGL_BAD_ACCESS);}
 s->initialized=1;
 s->wait_ms+=(seconds()-t)*1000;t=seconds();
 if(a.map_state==IsViewable){
  if(s->shm_attached){for(int i=0;i<count;i++)XShmPutImage(s->x11,s->window,s->gc,s->image,rects[i*4],rects[i*4+1],rects[i*4],rects[i*4+1],rects[i*4+2],rects[i*4+3],False);if(count)XSync(s->x11,False);}
  else{for(int i=0;i<count;i++)XPutImage(s->x11,s->window,s->gc,s->image,rects[i*4],rects[i*4+1],rects[i*4],rects[i*4+1],rects[i*4+2],rects[i*4+3]);if(count)XFlush(s->x11);}
 }
 /* Keep the vendor driver's frame lifecycle even for pbuffer targets.
  * Presentation has already consumed the pixels; standard pbuffer swap is
  * valid and lets the driver retire its per-frame command resources. */
 if(!R(eglSwapBuffers)(d,s->real))return EGL_FALSE;
 s->present_ms+=(seconds()-t)*1000;s->frames++;
 if(!s->measured)s->measured=seconds();
 if(seconds()-s->measured>5){fprintf(stderr,"DOVE_EGL_TIMING wait_ms=%.3f present_ms=%.3f frames=%u shm=%d read_pixels=%llu\n",s->wait_ms/s->frames,s->present_ms/s->frames,s->frames,s->shm_attached,s->read_pixels);s->wait_ms=s->present_ms=0;s->frames=0;s->read_pixels=0;s->measured=seconds();}
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
EGLBoolean eglDestroyContext(EGLDisplay d,EGLContext c){if(present_close){present_close();present_close=NULL;present_frame=NULL;}return R(eglDestroyContext)(d,c);}
EGLBoolean eglQueryContext(EGLDisplay d,EGLContext c,EGLint a,EGLint *v){return R(eglQueryContext)(d,c,a,v);}
EGLContext eglGetCurrentContext(void){return R(eglGetCurrentContext)();}
EGLDisplay eglGetCurrentDisplay(void){return R(eglGetCurrentDisplay)();}
const char *eglQueryString(EGLDisplay d,EGLint name){return R(eglQueryString)(d,name);}
EGLBoolean eglWaitGL(void){return R(eglWaitGL)();}
EGLBoolean eglWaitClient(void){return R(eglWaitClient)();}
EGLBoolean eglReleaseThread(void){return R(eglReleaseThread)();}

int dove_egl_direct_texture(unsigned texture){source_texture=texture;return texture!=0;}
