/* Owned EGLImage target; no native window surface, VOUT or display ownership. */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/ioctl.h>
#include <arm_neon.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
typedef struct {uint32_t h,w,bytespp;uint8_t bits,r,g,b,a,l,pad[2];uint32_t flags;void *memory;uint32_t yuv;} Native;
_Static_assert(sizeof(Native)==32,"native image ABI");
typedef struct {
 int ion,dma,w,h,attempted,attached,cached,pending,damage_count,damage[64];uint32_t handle,physical;size_t bytes;void *cpu,*base,*memory,*allocator;
 EGLDisplay d;EGLImageKHR image;GLuint texture,fbo;Native native;
 int(*free_ion)(int,uint32_t);void(*free_memory)(void*),(*destroy_base)(void*),(*forget)(uint32_t);
 PFNEGLDESTROYIMAGEKHRPROC destroy_image;
 Display*x;Window window;GC gc;XImage*xi;XShmSegmentInfo shm;
 void(*event)(void*,XEvent*);void*event_data;
} Target;
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static int thermal_ok(void){int temp=0;FILE*f=fopen("/sys/class/thermal/thermal_zone0/temp","r");if(f){if(fscanf(f,"%d",&temp)!=1)temp=0;fclose(f);}if(temp>=86000){fprintf(stderr,"THERMAL_TEST_STOP temp=%d\n",temp);return 0;}return 1;}
static int failures;
static int x_error(Display*d,XErrorEvent*e){(void)d;char text[128];XGetErrorText(d,e->error_code,text,sizeof text);fprintf(stderr,"X_ERROR code=%d request=%d %s\n",e->error_code,e->request_code,text);failures++;return 0;}
static double fence_ms,copy_ms,submit_ms,sync_ms;
/* Only the isolated SCM_RIGHTS bridge test sets this to an owned dma-buf. */
static int import_dma=-1;
static int gpu_only;
static int reserve_memory(size_t bytes){
 unsigned long long f=0,c=0,sh=0,cma=0,value;char line[160],key[64];
 FILE*p=fopen("/proc/meminfo","r");if(!p)return 0;
 while(fgets(line,sizeof line,p))if(sscanf(line,"%63s %llu",key,&value)==2){
  if(!strcmp(key,"MemFree:"))f=value;else if(!strcmp(key,"Cached:"))c=value;else if(!strcmp(key,"Shmem:"))sh=value;
 }fclose(p);p=fopen("/proc/vmstat","r");if(p){while(fgets(line,sizeof line,p))if(sscanf(line,"%63s %llu",key,&value)==2&&!strcmp(key,"nr_free_cma"))cma=value;fclose(p);}
 unsigned long long available=(f+(c>sh?(c-sh)/2:0))*1024,reserved=cma*(unsigned long long)sysconf(_SC_PAGESIZE);
 available=available>reserved?available-reserved:0;
 size_t required=bytes*4+16u*1024*1024;
 printf("MEMORY_GUARD required=%zu available=%llu\n",required,available);
 return required<=64u*1024*1024&&available>=required;
}
static int sync_target(Target*t){if(!t->cached)return 1;struct{uint32_t handle;int fd;}arg={0,t->dma};double start=now();int good=!ioctl(t->ion,0xc0084907,&arg);sync_ms+=(now()-start)*1000;return good;}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"FAILED %s EGL=%x GL=%x\n",#v,eglGetError(),glGetError());failures++;goto done;}}while(0)
#define LOAD(lib,v,n) do{*(void**)(&v)=dlsym(lib,n);CHECK(v);}while(0)
static int close_target(Target*t){
 glBindFramebuffer(GL_FRAMEBUFFER,0);glFinish();
 if(t->fbo)glDeleteFramebuffers(1,&t->fbo);
 if(t->texture)glDeleteTextures(1,&t->texture);
 int good=!t->image||t->destroy_image(t->d,t->image);
 /* Import consumes the descriptor's caller reference; do not free twice. */
 if(t->memory&&!t->attempted&&t->free_memory)t->free_memory(t->memory);
 if(t->base&&t->destroy_base)t->destroy_base(t->base);
 if(t->forget&&t->physical)t->forget(t->physical);
 if(t->cpu!=MAP_FAILED)munmap(t->cpu,t->bytes);
 if(t->dma>=0)close(t->dma);
 if(t->handle&&t->free_ion)t->free_ion(t->ion,t->handle);
 if(t->ion>=0)close(t->ion);
 if(t->attached){XShmDetach(t->x,&t->shm);XSync(t->x,False);}
 if(t->xi){t->xi->data=NULL;XDestroyImage(t->xi);}
 if(t->shm.shmaddr&&(void*)t->shm.shmaddr!=MAP_FAILED)shmdt(t->shm.shmaddr);
 if(t->gc)XFreeGC(t->x,t->gc);
 if(t->window){XDestroyWindow(t->x,t->window);XSync(t->x,False);}
 if(t->allocator)dlclose(t->allocator);
 return good;
}
static int create_target(Target*t,EGLDisplay d,Display*x,int w,int h){
 memset(t,0,sizeof*t);t->d=d;t->x=x;t->w=w;t->h=h;t->ion=t->dma=-1;t->cpu=MAP_FAILED;t->shm.shmid=-1;
 t->bytes=((size_t)w*h*4+4095)&~(size_t)4095;
 CHECK(reserve_memory(t->bytes));
 t->damage_count=1;t->damage[0]=t->damage[1]=0;t->damage[2]=w;t->damage[3]=h;
 t->cached=getenv("DOVE_SHARED_CACHED")!=NULL;
 void*mali=dlopen(getenv("DOVE_NATIVE_MALI"),RTLD_NOW|RTLD_LOCAL);CHECK(mali);
 t->allocator=dlopen(getenv("DOVE_ION_LIBRARY"),RTLD_LAZY|RTLD_LOCAL|RTLD_DEEPBIND);CHECK(t->allocator);
 void*(*create_base)(void),*(*map)(void*,uint32_t,uint32_t,void*,uint32_t);void(*share_gpu)(int,uint32_t,uint32_t);
 int(*alloc)(int,size_t,size_t,uint32_t,uint32_t,uint32_t*),(*physical)(int,uint32_t,uint32_t*,uint32_t*),(*share)(int,uint32_t,int*);
 LOAD(mali,create_base,"_mali_base_common_context_create");LOAD(mali,map,"_mali_base_common_mem_add_phys_mem");LOAD(mali,t->destroy_base,"_mali_base_common_context_destroy");LOAD(mali,t->free_memory,"_mali_base_common_mem_free");
 LOAD(RTLD_DEFAULT,share_gpu,"gpu_share_buffer");LOAD(RTLD_DEFAULT,t->forget,"gpu_forget_buffer");
 LOAD(t->allocator,alloc,"ion_alloc");LOAD(t->allocator,physical,"ion_phys");LOAD(t->allocator,share,"ion_share");LOAD(t->allocator,t->free_ion,"ion_free");
 PFNEGLCREATEIMAGEKHRPROC create_image=(void*)eglGetProcAddress("eglCreateImageKHR");t->destroy_image=(void*)eglGetProcAddress("eglDestroyImageKHR");
 PFNGLEGLIMAGETARGETTEXTURE2DOESPROC attach=(void*)eglGetProcAddress("glEGLImageTargetTexture2DOES");CHECK(create_image&&t->destroy_image&&attach);
 uint32_t actual=0;t->ion=open("/dev/ion",O_RDWR|O_CLOEXEC);CHECK(t->ion>=0);
 uint32_t flags=t->cached?(getenv("DOVE_SHARED_FAULT_CACHE")?0x81:0x83):0x80;
 printf("ION_FLAGS value=%x\n",flags);
 if(import_dma>=0){int(*import)(int,int,uint32_t*);LOAD(t->allocator,import,"ion_import");CHECK(!import(t->ion,import_dma,&t->handle));}
 else CHECK(!alloc(t->ion,t->bytes,4096,0x80,flags,&t->handle));
 CHECK(!physical(t->ion,t->handle,&t->physical,&actual)&&actual==t->bytes);CHECK(!share(t->ion,t->handle,&t->dma));
 t->cpu=mmap(NULL,t->bytes,PROT_READ|PROT_WRITE,MAP_SHARED,t->dma,0);CHECK(t->cpu!=MAP_FAILED);if(import_dma<0)memset(t->cpu,0,t->bytes);
 CHECK(sync_target(t));
 share_gpu(t->dma,t->physical,actual);t->base=create_base();CHECK(t->base);t->memory=map(t->base,t->physical,actual,t->cpu,0x32);CHECK(t->memory);
 t->native=(Native){.w=w,.h=h,.bytespp=4,.bits=32,.r=8,.g=8,.b=8,.a=8,.flags=1,.memory=t->memory};
 EGLint ia[]={EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_NONE};t->attempted=1;t->image=create_image(d,EGL_NO_CONTEXT,EGL_NATIVE_PIXMAP_KHR,&t->native,ia);CHECK(t->image);
 glGenTextures(1,&t->texture);glBindTexture(GL_TEXTURE_2D,t->texture);attach(GL_TEXTURE_2D,t->image);CHECK(glGetError()==GL_NO_ERROR);
 glGenFramebuffers(1,&t->fbo);glBindFramebuffer(GL_FRAMEBUFFER,t->fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t->texture,0);CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
 if(gpu_only){dlclose(mali);return 1;}
 t->window=XCreateSimpleWindow(x,DefaultRootWindow(x),0,0,w,h,0,0,0);XSetWindowAttributes wa={.override_redirect=True};XChangeWindowAttributes(x,t->window,CWOverrideRedirect,&wa);XStoreName(x,t->window,"DoveOS shared GPU window trial");XMapRaised(x,t->window);XSync(x,False);
 XWindowAttributes a;CHECK(XGetWindowAttributes(x,t->window,&a)&&a.depth==24);t->gc=XCreateGC(x,t->window,0,NULL);
 t->xi=XShmCreateImage(x,a.visual,a.depth,ZPixmap,NULL,&t->shm,w,h);CHECK(t->xi);
 t->shm.shmid=shmget(IPC_PRIVATE,(size_t)t->xi->bytes_per_line*h,IPC_CREAT|0600);CHECK(t->shm.shmid>=0);
 t->shm.shmaddr=shmat(t->shm.shmid,NULL,0);shmctl(t->shm.shmid,IPC_RMID,NULL);CHECK((void*)t->shm.shmaddr!=MAP_FAILED);t->shm.readOnly=False;t->xi->data=t->shm.shmaddr;
 t->attached=XShmAttach(x,&t->shm);XSync(x,False);CHECK(t->attached);dlclose(mali);return 1;
 done:return 0;
}
static int compare(Target*t){
 if(!sync_target(t))return 0;
 size_t bytes=(size_t)t->w*t->h*4;unsigned char*r=malloc(bytes);if(!r)return 0;
 unsigned char*before=NULL;if(getenv("DOVE_SHARED_COMPARE_BEFORE")){before=malloc(bytes);if(!before){free(r);return 0;}memcpy(before,t->cpu,bytes);}
 glReadPixels(0,0,t->w,t->h,GL_RGBA,GL_UNSIGNED_BYTE,r);glFinish();if(!sync_target(t)){free(r);return 0;}unsigned bad=0;
 for(size_t i=0;i<bytes;i++){int k=i%4,b=k==0?2:k==2?0:k;if(r[i]!=((unsigned char*)t->cpu)[(i&~(size_t)3)+b]){if(bad<20)printf("MAP_DIFF x=%zu y=%zu k=%d read=%d mapped=%d\n",(i/4)%t->w,(i/4)/t->w,k,r[i],((unsigned char*)t->cpu)[(i&~(size_t)3)+b]);bad++;}}
 if(before){unsigned pre=0;for(size_t i=0;i<bytes;i++){int k=i%4,b=k==0?2:k==2?0:k;if(r[i]!=before[(i&~(size_t)3)+b])pre++;}printf("BEFORE_READBACK_COMPARE bad_bytes=%u\n",pre);if(pre)bad+=pre;free(before);}
 free(r);printf("MAPPING_COMPARE size=%dx%d bad_bytes=%u\n",t->w,t->h,bad);return !bad;
}
static int present(Target*t){
 /* Fence the preceding XShm read before overwriting its single staging image. */
 double start=now();
 if(getenv("DOVE_SHARED_EVENTS")){
  double deadline=now()+2.;
  while(t->pending){
   if(!XPending(t->x)){
    int ms=(int)((deadline-now())*1000);if(ms<=0){fprintf(stderr,"XSHM_TIMEOUT\n");return 0;}
    struct pollfd p={.fd=ConnectionNumber(t->x),.events=POLLIN};int rc=poll(&p,1,ms);
    if(rc<0&&errno==EINTR)continue;if(rc<=0)return 0;
   }
   XEvent ev;XNextEvent(t->x,&ev);if(ev.type==XShmGetEventBase(t->x)+ShmCompletion&&((XShmCompletionEvent*)&ev)->shmseg==t->shm.shmseg)t->pending--;else if(t->event)t->event(t->event_data,&ev);
  }
 }else XSync(t->x,False);
 fence_ms+=(now()-start)*1000;start=now();
 for(int i=0;i<t->damage_count;i++){
  int*r=t->damage+i*4;
  for(int y=r[1];y<r[1]+r[3];y++){
   uint8_t*dst=(uint8_t*)t->xi->data+(size_t)y*t->xi->bytes_per_line+r[0]*4,*src=(uint8_t*)t->cpu+((size_t)(t->h-y-1)*t->w+r[0])*4;int z=0;
   if(getenv("DOVE_SHARED_NEON")){for(;z+64<=r[2]*4;z+=64){uint8x16_t a=vld1q_u8(src+z),b=vld1q_u8(src+z+16),c=vld1q_u8(src+z+32),d=vld1q_u8(src+z+48);vst1q_u8(dst+z,a);vst1q_u8(dst+z+16,b);vst1q_u8(dst+z+32,c);vst1q_u8(dst+z+48,d);}}
   if(z<r[2]*4)memcpy(dst+z,src+z,(size_t)r[2]*4-z);
  }
 }
 copy_ms+=(now()-start)*1000;start=now();t->pending=0;for(int i=0;i<t->damage_count;i++){int*r=t->damage+i*4;XShmPutImage(t->x,t->window,t->gc,t->xi,r[0],r[1],r[0],r[1],r[2],r[3],getenv("DOVE_SHARED_EVENTS")!=NULL);t->pending++;}XFlush(t->x);submit_ms+=(now()-start)*1000;
 return 1;
}
static int compare_x(Target*t){
 XSync(t->x,False);XImage*im=XGetImage(t->x,t->window,0,0,t->w,t->h,AllPlanes,ZPixmap);if(!im)return 0;unsigned bad=0;
 for(int y=0;y<t->h;y++)for(int x=0;x<t->w;x++){uint32_t want=((uint32_t*)t->cpu)[(t->h-y-1)*t->w+x]&0xffffff;unsigned long got=XGetPixel(im,x,y)&0xffffff;if(got!=want){if(bad<20)printf("X_DIFF x=%d y=%d got=%06lx want=%06x\n",x,y,got,want);bad++;}}
 XDestroyImage(im);printf("X11_COMPARE size=%dx%d bad_pixels=%u\n",t->w,t->h,bad);return !bad;
}
static GLuint shader(GLenum type,const char*s){GLuint id=glCreateShader(type);glShaderSource(id,1,&s,NULL);glCompileShader(id);GLint good;glGetShaderiv(id,GL_COMPILE_STATUS,&good);if(!good){glDeleteShader(id);return 0;}return id;}
#ifndef DOVE_SHARED_LIBRARY
int main(int argc,char**argv){
 setvbuf(stdout,NULL,_IOLBF,0);int frames=argc>1?atoi(argv[1]):120;int w=argc>2?atoi(argv[2]):640,h=w*9/16;
 if(frames<1||frames>36000||(w!=320&&w!=640&&w!=1280))return 2;
 Display*x=XOpenDisplay(NULL);if(!x)return 2;XSetErrorHandler(x_error);EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);EGLContext c=EGL_NO_CONTEXT;EGLSurface s=EGL_NO_SURFACE;Target t={.ion=-1,.dma=-1,.cpu=MAP_FAILED};int made=0;
 CHECK(eglInitialize(d,NULL,NULL));EGLConfig cfg;EGLint n;EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};CHECK(eglChooseConfig(d,ca,&cfg,1,&n)&&n);
 EGLint pa[]={EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE},ctx[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};s=eglCreatePbufferSurface(d,cfg,pa);c=eglCreateContext(d,cfg,EGL_NO_CONTEXT,ctx);CHECK(c&&s);CHECK(eglMakeCurrent(d,s,s,c));
 CHECK(create_target(&t,d,x,w,h));made=1;
 GLuint vs=shader(GL_VERTEX_SHADER,"attribute vec2 p;void main(){gl_Position=vec4(p,0.,1.);}"),fs=shader(GL_FRAGMENT_SHADER,"precision mediump float;uniform vec2 sz;uniform float phase;void main(){gl_FragColor=vec4(gl_FragCoord.x/sz.x,gl_FragCoord.y/sz.y,mod(floor(gl_FragCoord.x)+floor(gl_FragCoord.y)+phase,17.)/16.,1.);}");CHECK(vs&&fs);
 GLuint pr=glCreateProgram();glAttachShader(pr,vs);glAttachShader(pr,fs);glBindAttribLocation(pr,0,"p");glLinkProgram(pr);GLint linked;glGetProgramiv(pr,GL_LINK_STATUS,&linked);CHECK(linked);glDeleteShader(vs);glDeleteShader(fs);glUseProgram(pr);
 glUniform2f(glGetUniformLocation(pr,"sz"),w,h);GLint phase=glGetUniformLocation(pr,"phase");GLfloat quad[]={-1,-1,1,-1,-1,1,1,1};glVertexAttribPointer(0,2,GL_FLOAT,0,0,quad);glEnableVertexAttribArray(0);glViewport(0,0,w,h);
 glDrawArrays(GL_TRIANGLE_STRIP,0,4);glFinish();CHECK(compare(&t));CHECK(present(&t));CHECK(compare_x(&t));
 fence_ms=copy_ms=submit_ms=sync_ms=0;
 double begin=now(),draw=0,copy=0,max_work=0;unsigned late=0;int region=getenv("DOVE_SHARED_REGIONS")!=NULL,oldx=0;
 for(int f=0;f<frames;f++){
  if(f%120==0)CHECK(thermal_ok());
  if(getenv("DOVE_PACE_60")){double due=begin+(double)f/60.,delay=due-now();if(delay>0){struct timespec ts={(time_t)delay,(long)((delay-(time_t)delay)*1e9)};nanosleep(&ts,NULL);}}
  double start=now(),frame_begin=start;
  if(region){
   int nx=(f*7)%(w-64);glEnable(GL_SCISSOR_TEST);glUniform1f(phase,0);glScissor(oldx,80,64,64);glDrawArrays(GL_TRIANGLE_STRIP,0,4);glUniform1f(phase,(f%16)+1);glScissor(nx,80,64,64);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
   t.damage_count=2;int r[]={oldx,h-144,64,64,nx,h-144,64,64};memcpy(t.damage,r,sizeof r);oldx=nx;
  }else{glUniform1f(phase,f%17);glDrawArrays(GL_TRIANGLE_STRIP,0,4);}
  if(getenv("DOVE_SHARED_REBIND")){glBindFramebuffer(GL_FRAMEBUFFER,0);glFlush();glBindFramebuffer(GL_FRAMEBUFFER,t.fbo);}
  glFinish();CHECK(sync_target(&t));draw+=now()-start;start=now();CHECK(present(&t));copy+=now()-start;CHECK(glGetError()==GL_NO_ERROR);
  double work=now()-frame_begin;if(work>max_work)max_work=work;if(work>1./60.)late++;
  if(getenv("DOVE_VERIFY_ALL")){CHECK(compare(&t));CHECK(compare_x(&t));}
 }
 XSync(x,False);double duration=now()-begin;printf("SHARED_BENCH frames=%d size=%dx%d wall=%.6f fps=%.3f draw_ms=%.3f present_ms=%.3f readback_frames=0\n",frames,w,h,duration,frames/duration,draw*1000/frames,copy*1000/frames);
 printf("PRESENT_PARTS fence_ms=%.3f copy_ms=%.3f submit_ms=%.3f ion_sync_ms=%.3f cached=%d\n",fence_ms/frames,copy_ms/frames,submit_ms/frames,sync_ms/frames,t.cached);
 printf("FRAME_BUDGET over_16_667ms=%u max_work_ms=%.3f\n",late,max_work*1000);
 CHECK(compare(&t));CHECK(compare_x(&t));glDisableVertexAttribArray(0);glUseProgram(0);glDeleteProgram(pr);
 done:
 if(made){if(!close_target(&t))failures++;}else if(t.x)close_target(&t);
 if(c&&s)eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);if(c)eglDestroyContext(d,c);if(s)eglDestroySurface(d,s);if(d)eglTerminate(d);XCloseDisplay(x);
 printf("SHARED_WINDOW_DONE failures=%d\n",failures);return failures?1:0;
}
#endif
