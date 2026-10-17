/* Private Xephyr RGB presenter. GPU texture copy -> parent-owned ARGB pool.
 * No RPC ownership or allocation release in this library: guardian owns both. */
#define _GNU_SOURCE
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <dlfcn.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <time.h>
#include <fcntl.h>
#include "rgb-session.h"
typedef struct {uint32_t h,w,bytespp;uint8_t bits,r,g,b,a,l,pad[2];uint32_t flags;void *memory;uint32_t yuv;} Native;
static struct {
 RgbSession *session;void *cpu[5],*mali,*base;
 EGLDisplay display;EGLContext context;EGLSurface surface;
 struct {void *memory;Native native;EGLImageKHR image;GLuint texture,fbo;int attempted;uint32_t previous;int dirty[4];} slot[3];
 GLuint program,verify_fbo,cursor_program,cursor_texture; GLint cursor_rect; RgbCursor cursor; uint32_t cursor_seq,uploaded_shape;uint32_t write;unsigned source;int pending,initialized,failed;
 PFNEGLCREATEIMAGEKHRPROC create_image;PFNEGLDESTROYIMAGEKHRPROC destroy_image;
 PFNEGLCREATESYNCKHRPROC create_sync;PFNEGLCLIENTWAITSYNCKHRPROC wait_sync;PFNEGLDESTROYSYNCKHRPROC destroy_sync;
 void (*destroy_base)(void*),(*free_memory)(void*);
 double rendered_ms,fence_ms,waiting_ms,started,next_present;
 unsigned long long copied_pixels;
} p;
static int (*output_rects)(int*,int),(*output_draw)(int,int,int,int);
static int output_ready;
/* Subtract opaque app clips from base copy regions. Overflow falls back to
 * copying the entire base before the app pass; it never drops pixels. */
static int base_regions(const int*dirty,int*regions){
 int count=1;regions[0]=dirty[0];regions[1]=dirty[1];regions[2]=dirty[2]-dirty[0];regions[3]=dirty[3]-dirty[1];
 if(!output_ready)return count;
 int clips[4096],next[2048],n=output_rects(clips,1024);if(n<0)return 1;
 for(int j=0;j<n;j++){
  int*c=clips+j*4,total=0;
  for(int i=0;i<count;i++){
   int*r=regions+i*4,l=r[0]>c[0]?r[0]:c[0],t=r[1]>c[1]?r[1]:c[1];
   int right=r[0]+r[2]<c[0]+c[2]?r[0]+r[2]:c[0]+c[2],bottom=r[1]+r[3]<c[1]+c[3]?r[1]+r[3]:c[1]+c[3];
   int pieces[16],parts=0;
   if(l>=right||t>=bottom){memcpy(pieces,r,4*sizeof(int));parts=1;}
   else{
    if(t>r[1]){int a[]={r[0],r[1],r[2],t-r[1]};memcpy(pieces+parts++*4,a,sizeof a);}
    if(bottom<r[1]+r[3]){int a[]={r[0],bottom,r[2],r[1]+r[3]-bottom};memcpy(pieces+parts++*4,a,sizeof a);}
    if(l>r[0]){int a[]={r[0],t,l-r[0],bottom-t};memcpy(pieces+parts++*4,a,sizeof a);}
    if(right<r[0]+r[2]){int a[]={right,t,r[0]+r[2]-right,bottom-t};memcpy(pieces+parts++*4,a,sizeof a);}
   }
   if(total+parts>512){regions[0]=dirty[0];regions[1]=dirty[1];regions[2]=dirty[2]-dirty[0];regions[3]=dirty[3]-dirty[1];return 1;}
   memcpy(next+total*4,pieces,parts*4*sizeof(int));total+=parts;
  }
  memcpy(regions,next,total*4*sizeof(int));count=total;
 }
 return count;
}
static unsigned seen_epoch;
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static uint32_t load(volatile uint32_t *v){__sync_synchronize();return ntohl(*v);}
static void store(volatile uint32_t *v,uint32_t x){*v=htonl(x);__sync_synchronize();}
static int fence(void){
 EGLSyncKHR sync=p.create_sync(p.display,EGL_SYNC_FENCE_KHR,NULL);if(!sync)return 0;
 double start=now();EGLint result=p.wait_sync(p.display,sync,EGL_SYNC_FLUSH_COMMANDS_BIT_KHR,1000000000ULL);p.fence_ms+=(now()-start)*1000;
 p.destroy_sync(p.display,sync);return result==EGL_CONDITION_SATISFIED_KHR;
}
static GLuint shader(GLenum type,const char *text){GLuint id=glCreateShader(type);glShaderSource(id,1,&text,NULL);glCompileShader(id);GLint ok=0;glGetShaderiv(id,GL_COMPILE_STATUS,&ok);if(!ok){char log[1024];glGetShaderInfoLog(id,sizeof log,NULL,log);fprintf(stderr,"RGB_SHADER_FAIL %s\n",log);glDeleteShader(id);return 0;}return id;}
#define GET(v,n) do{*(void**)(&v)=dlsym(p.mali,n);if(!v)return 0;}while(0)
static int initialize(unsigned w,unsigned h,EGLContext shared){
 setvbuf(stdout,NULL,_IOLBF,0);
 const char *fdtext=getenv("DOVE_RGB_SESSION_FD");if(!fdtext)return 0;
 char *end;long fd=strtol(fdtext,&end,10);struct stat st;if(*end||fd<3||fstat(fd,&st)||st.st_size!=RGB_SESSION_BYTES)return 0;
 p.session=mmap(NULL,RGB_SESSION_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);if(p.session==MAP_FAILED){p.session=NULL;return 0;}
 if(p.session->magic!=RGB_SESSION_MAGIC||p.session->width!=w||p.session->height!=h||kill(p.session->parent,0)||!p.session->state.created||p.session->state.phase!=5)return 0;
 p.session->state.gpu_safe=0;
 for(int i=0;i<5;i++){WireBuffer *b=&p.session->buffers[i];if(b->fd<3||!b->phys||b->bytes<4096)return 0;p.cpu[i]=mmap(NULL,b->bytes,PROT_READ|PROT_WRITE,MAP_SHARED,b->fd,0);if(p.cpu[i]==MAP_FAILED){p.cpu[i]=NULL;return 0;}}
 const char *lib=getenv("LIBGL_GLES");if(!lib)return 0;p.mali=dlopen(lib,RTLD_NOW|RTLD_LOCAL);if(!p.mali)return 0;
 void *(*create_base)(void),*(*map_memory)(void*,uint32_t,uint32_t,void*,uint32_t);void (*share_gpu)(int,uint32_t,uint32_t);
 GET(create_base,"_mali_base_common_context_create");GET(p.destroy_base,"_mali_base_common_context_destroy");GET(map_memory,"_mali_base_common_mem_add_phys_mem");GET(p.free_memory,"_mali_base_common_mem_free");
 *(void**)(&share_gpu)=dlsym(RTLD_DEFAULT,"gpu_share_buffer");if(!share_gpu)return 0;
 p.create_image=(void*)eglGetProcAddress("eglCreateImageKHR");p.destroy_image=(void*)eglGetProcAddress("eglDestroyImageKHR");
 p.create_sync=(void*)eglGetProcAddress("eglCreateSyncKHR");p.wait_sync=(void*)eglGetProcAddress("eglClientWaitSyncKHR");p.destroy_sync=(void*)eglGetProcAddress("eglDestroySyncKHR");
 PFNGLEGLIMAGETARGETTEXTURE2DOESPROC attach=(void*)eglGetProcAddress("glEGLImageTargetTexture2DOES");
 if(!p.create_image||!p.destroy_image||!p.create_sync||!p.wait_sync||!p.destroy_sync||!attach)return 0;
 p.display=eglGetCurrentDisplay();EGLConfig config;EGLint count;
 EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
 if(!eglChooseConfig(p.display,ca,&config,1,&count)||!count)return 0;
 EGLint pa[]={EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE},ctx[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
 p.surface=eglCreatePbufferSurface(p.display,config,pa);p.context=eglCreateContext(p.display,config,shared,ctx);
 if(!p.surface||!p.context||!eglMakeCurrent(p.display,p.surface,p.surface,p.context))return 0;
 p.base=create_base();if(!p.base)return 0;
 for(unsigned s=0;s<3;s++){
  p.slot[s].dirty[0]=p.slot[s].dirty[1]=0;p.slot[s].dirty[2]=w;p.slot[s].dirty[3]=h;
  WireBuffer *b=&p.session->buffers[s+2];if(b->bytes<((size_t)w*h*4))return 0;share_gpu(b->fd,b->phys,b->bytes);
  p.slot[s].memory=map_memory(p.base,b->phys,b->bytes,p.cpu[s+2],0x32);if(!p.slot[s].memory)return 0;
  p.slot[s].native=(Native){.w=w,.h=h,.bytespp=4,.bits=32,.r=8,.g=8,.b=8,.a=8,.flags=1,.memory=p.slot[s].memory};
  EGLint ia[]={EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_NONE};p.slot[s].attempted=1;p.slot[s].image=p.create_image(p.display,EGL_NO_CONTEXT,EGL_NATIVE_PIXMAP_KHR,&p.slot[s].native,ia);if(!p.slot[s].image)return 0;
  glGenTextures(1,&p.slot[s].texture);glBindTexture(GL_TEXTURE_2D,p.slot[s].texture);attach(GL_TEXTURE_2D,p.slot[s].image);if(glGetError())return 0;
  glGenFramebuffers(1,&p.slot[s].fbo);glBindFramebuffer(GL_FRAMEBUFFER,p.slot[s].fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,p.slot[s].texture,0);if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return 0;
 }
 GLuint vs=shader(GL_VERTEX_SHADER,"attribute vec2 pos;varying vec2 uv;void main(){uv=pos*.5+.5;gl_Position=vec4(pos,0.,1.);}");
 GLuint fs=shader(GL_FRAGMENT_SHADER,"precision mediump float;varying vec2 uv;uniform sampler2D image;void main(){gl_FragColor=vec4(texture2D(image,uv).rgb,1.);}");
 if(!vs||!fs)return 0;p.program=glCreateProgram();glAttachShader(p.program,vs);glAttachShader(p.program,fs);glBindAttribLocation(p.program,0,"pos");glLinkProgram(p.program);glDeleteShader(vs);glDeleteShader(fs);GLint ok;glGetProgramiv(p.program,GL_LINK_STATUS,&ok);if(!ok)return 0;
 vs=shader(GL_VERTEX_SHADER,"attribute vec2 pos;varying vec2 uv;uniform vec4 rect;void main(){uv=pos*.5+.5;gl_Position=vec4(rect.xy+uv*rect.zw,0.,1.);}");
 fs=shader(GL_FRAGMENT_SHADER,"precision mediump float;varying vec2 uv;uniform sampler2D image;void main(){gl_FragColor=texture2D(image,uv);}");
 if(!vs||!fs)return 0;p.cursor_program=glCreateProgram();glAttachShader(p.cursor_program,vs);glAttachShader(p.cursor_program,fs);glBindAttribLocation(p.cursor_program,0,"pos");glLinkProgram(p.cursor_program);glDeleteShader(vs);glDeleteShader(fs);glGetProgramiv(p.cursor_program,GL_LINK_STATUS,&ok);if(!ok)return 0;
 p.cursor_rect=glGetUniformLocation(p.cursor_program,"rect");if(p.cursor_rect<0)return 0;
 glGenTextures(1,&p.cursor_texture);glBindTexture(GL_TEXTURE_2D,p.cursor_texture);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 glGenFramebuffers(1,&p.verify_fbo);p.initialized=1;
 if(getenv("DOVE_COMPOSITOR_OUTPUT")){
  void*bridge=dlopen(getenv("DOVE_COMPOSITOR_LIBRARY"),RTLD_NOW|RTLD_NOLOAD);
  if(!bridge)return 0;*(void**)&output_rects=dlsym(bridge,"dove_compositor_output_rects");*(void**)&output_draw=dlsym(bridge,"dove_compositor_output_draw");
  if(!output_rects||!output_draw)return 0;output_ready=1;
 }
 p.started=now();p.session->ready=1;seen_epoch=p.session->epoch;
 puts("GPU_PRESENTER_READY renderer=rgb720 cpu_pixels=0");
 printf("RGB_X_PRESENTER_READY size=%ux%u parent=%u renderer=%s\n",w,h,p.session->parent,glGetString(GL_RENDERER));return 1;
}
#undef GET
void dove_present_texture(unsigned texture){p.source=texture;}
void dove_present_damage(const int *rects,int count){
 if(count)p.pending=1;if(!p.initialized)return;
 for(int r=0;r<count;r++)for(int s=0;s<3;s++){
  int left=rects[r*4],top=rects[r*4+1],right=left+rects[r*4+2],bottom=top+rects[r*4+3];
  if(left<0)left=0;if(top<0)top=0;if(right>(int)p.session->width)right=p.session->width;if(bottom>(int)p.session->height)bottom=p.session->height;
  if(right<=left||bottom<=top)continue;int *dirty=p.slot[s].dirty;
  if(left<dirty[0])dirty[0]=left;if(top<dirty[1])dirty[1]=top;if(right>dirty[2])dirty[2]=right;if(bottom>dirty[3])dirty[3]=bottom;
 }
}


static long lease_owner(void){
 long owner=0;FILE *f=fopen("/tmp/doveos-direct-display","r");
 if(f){if(fscanf(f,"%ld",&owner)!=1)owner=0;fclose(f);}
 return owner>1&&owner<4194304&&!kill(owner,0)?owner:0;
}
int dove_present_poll(void){
 if(!p.initialized)return 0;
 p.session->heartbeat=(uint32_t)(now()*1000);
 /* Same X thread as submission: publishing this request guarantees that
    no further frame can be queued while the RPC owner retires the plane. */
 p.session->pause_request=lease_owner();__sync_synchronize();
 if(p.session->pause_request||p.session->paused)return 0;
 unsigned epoch=p.session->epoch;
 if(epoch!=seen_epoch){
  seen_epoch=epoch;p.write=0;p.pending=1;
  for(int i=0;i<3;i++){
   p.slot[i].previous=0;p.slot[i].dirty[0]=p.slot[i].dirty[1]=0;
   p.slot[i].dirty[2]=p.session->width;p.slot[i].dirty[3]=p.session->height;
  }
  puts("RGB_DESKTOP_DISPLAY_RESUMED full_redraw=1");return 1;
 }
 return 0;
}
/* Coalesce damage until the next 60 Hz output deadline. A pending update
 * keeps its per-slot dirty bounds even after X11 has consumed its damage. */
int dove_present_delay_ms(void){
 if(!p.initialized||!getenv("DOVE_RGB_PACE60"))return 16;
 double delay=(p.next_present-now())*1000;
 if(delay<=0)return p.pending?0:16;int milliseconds=(int)delay+1;return milliseconds<16?milliseconds:16;
}
static int pending_bounds(int*bounds){
 if(!p.initialized||!p.pending||p.failed||p.session->paused||p.session->pause_request)return 0;
 if(getenv("DOVE_RGB_PACE60")&&now()<p.next_present)return 0;
 int*r=p.slot[p.session->frames%3].dirty;
 if(r[2]<=r[0]||r[3]<=r[1])return 0;
 memcpy(bounds,r,4*sizeof(int));return 1;
}
/* Single writer in a separate process: X requests never block the X server's own thread. */
int dove_present_cursor_poll(int *bounds){
 if(!p.initialized||p.failed)return 0;
 RgbCursor next;uint32_t seq=__atomic_load_n(&p.session->cursor_seq,__ATOMIC_ACQUIRE);
 if((seq&1)||seq==p.cursor_seq)return pending_bounds(bounds);
 memcpy(&next,&p.session->cursor,sizeof next);__sync_synchronize();
 if(seq!=__atomic_load_n(&p.session->cursor_seq,__ATOMIC_ACQUIRE))return 0;
 if(next.w>RGB_CURSOR_SIDE||next.h>RGB_CURSOR_SIDE)return 0;
 int rects[8]={p.cursor.x,p.cursor.y,p.cursor.visible?p.cursor.w:0,p.cursor.visible?p.cursor.h:0,next.x,next.y,next.visible?next.w:0,next.visible?next.h:0};
 bounds[0]=p.session->width;bounds[1]=p.session->height;bounds[2]=bounds[3]=0;
 for(int i=0;i<2;i++){
  int *r=rects+i*4,l=r[0],t=r[1],right=l+r[2],b=t+r[3];if(!r[2]||!r[3])continue;
  if(l<0)l=0;if(t<0)t=0;if(right>(int)p.session->width)right=p.session->width;if(b>(int)p.session->height)b=p.session->height;
  if(l>=right||t>=b)continue;
  if(l<bounds[0])bounds[0]=l;if(t<bounds[1])bounds[1]=t;if(right>bounds[2])bounds[2]=right;if(b>bounds[3])bounds[3]=b;
 }
 p.cursor=next;p.cursor_seq=seq;
 if(bounds[2]<=bounds[0]||bounds[3]<=bounds[1])return 0;
 dove_present_damage(rects,2);p.session->cursor_frames++;return 1;
}
static void draw_cursor(void){
 RgbCursor *c=&p.cursor;if(!c->visible||!c->w||!c->h)return;
 glBindTexture(GL_TEXTURE_2D,p.cursor_texture);
 if(p.uploaded_shape!=c->shape){
  unsigned char rgba[RGB_CURSOR_SIDE*RGB_CURSOR_SIDE*4];
  for(unsigned i=0;i<c->w*c->h;i++){uint32_t a=c->pixels[i];rgba[i*4]=a>>16;rgba[i*4+1]=a>>8;rgba[i*4+2]=a;rgba[i*4+3]=a>>24;}
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,c->w,c->h,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);p.uploaded_shape=c->shape;
 }
 /* On this Mali native-pixmap path, a cursor-sized viewport erased retained
  * scene pixels outside the scissor (reproduced at Start). Keep the render
  * target viewport fixed and position the cursor through clip coordinates. */
 glUseProgram(p.cursor_program);glViewport(0,0,p.session->width,p.session->height);
 glUniform4f(p.cursor_rect,2.f*c->x/p.session->width-1.f,2.f*c->y/p.session->height-1.f,2.f*c->w/p.session->width,2.f*c->h/p.session->height);
 glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glDrawArrays(GL_TRIANGLE_STRIP,0,4);glDisable(GL_BLEND);
}
int dove_present_frame(int width,int height){
 if(p.initialized){dove_present_poll();if(p.session->pause_request||p.session->paused)return 0;}
 if(p.failed||!p.source)return -1;if(p.initialized&&!p.pending)return 0;
 if(p.initialized&&getenv("DOVE_RGB_PACE60")&&now()<p.next_present)return 0;
 EGLDisplay saved_display=eglGetCurrentDisplay();EGLContext saved_context=eglGetCurrentContext();EGLSurface saved_draw=eglGetCurrentSurface(EGL_DRAW),saved_read=eglGetCurrentSurface(EGL_READ);int result=-1;
 double start=now();glFinish(); /* Root shader writes must precede sampling in the shared context. */
 double profile_root=now();
 if(!p.initialized&&!initialize(width,height,saved_context))goto failed;
 if(p.session->width!=(unsigned)width||p.session->height!=(unsigned)height)goto failed;
 if(!eglMakeCurrent(p.display,p.surface,p.surface,p.context))goto failed;
 unsigned gen=p.session->frames+1,slot=(gen-1)%3;uint32_t previous=p.slot[slot].previous;volatile uint32_t *ref=p.cpu[0],*head=p.cpu[1];
 double wait=now();uint32_t current=load(ref+14);
 while(previous&&(current==0xffffffff||current<=previous)&&now()-wait<1){usleep(1000);current=load(ref+14);}
 p.waiting_ms+=(now()-wait)*1000;if(previous&&(current==0xffffffff||current<=previous))goto failed;if(previous)p.session->reused++;
 const int points[6][2]={{28,28},{60,28},{92,44},{24,56},{width/2,height/2},{width-80,height-80}};
 unsigned char expected[6][4];int verify=getenv("DOVE_RGB_VERIFY")&&gen<=24;
 unsigned char root_roi[100*30*4],old_roi[100*30*4];int trace=getenv("DOVE_RGB_TRACE_START")&&gen<80;
 if(trace){
  glBindFramebuffer(GL_FRAMEBUFFER,p.verify_fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,p.source,0);
  glReadPixels(0,690,100,30,GL_RGBA,GL_UNSIGNED_BYTE,root_roi);
  for(int y=0;y<30;y++)memcpy(old_roi+y*400,(char*)p.cpu[slot+2]+((690+y)*width)*4,400);
 }
 if(getenv("DOVE_RGB_CAPTURE_SOURCE")&&!access("/tmp/dove-shared-root.capture",F_OK)){
  /* Diagnostic only: compare the actual texture with XGetImage and scanout. */
  glBindFramebuffer(GL_FRAMEBUFFER,p.verify_fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,p.source,0);
  if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)goto failed;
  size_t bytes=(size_t)width*height*4;unsigned char *pixels=malloc(bytes);if(!pixels)goto failed;
  glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
  FILE *fp=fopen("/tmp/rgb-source-texture.rgba","wb");if(!fp){free(pixels);goto failed;}
  size_t n=fwrite(pixels,1,bytes,fp);fclose(fp);free(pixels);if(n!=bytes)goto failed;
 }
 if(verify){glBindFramebuffer(GL_FRAMEBUFFER,p.verify_fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,p.source,0);if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)goto failed;for(int i=0;i<6;i++)glReadPixels(points[i][0],points[i][1],1,1,GL_RGBA,GL_UNSIGNED_BYTE,expected[i]);}
 p.session->state.gpu_safe=0;
 glBindFramebuffer(GL_FRAMEBUFFER,p.slot[slot].fbo);glUseProgram(p.program);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,p.source);
 GLint min,mag,wraps,wrapt;glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&min);glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,&mag);glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,&wraps);glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,&wrapt);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 int *dirty=p.slot[slot].dirty;if(dirty[2]<=dirty[0]||dirty[3]<=dirty[1])goto failed;
 if(getenv("DOVE_RGB_FULL_COPY")){dirty[0]=dirty[1]=0;dirty[2]=width;dirty[3]=height;}
 int regions[2048],region_count=base_regions(dirty,regions);
 if(getenv("DOVE_RGB_NATIVE_COPY")&&access("/tmp/dove-rgb-shader-copy",F_OK)){
  /* Exact integer copy between owned root and output textures. */
  glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  glBindFramebuffer(GL_FRAMEBUFFER,p.verify_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,p.source,0);
  if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)goto failed;
  glBindTexture(GL_TEXTURE_2D,p.slot[slot].texture);
  for(int i=0;i<region_count;i++){int*r=regions+i*4;glCopyTexSubImage2D(GL_TEXTURE_2D,0,r[0],r[1],r[0],r[1],r[2],r[3]);}
  glBindTexture(GL_TEXTURE_2D,p.source);
  glBindFramebuffer(GL_FRAMEBUFFER,p.slot[slot].fbo);
  GLfloat quad[]={-1,-1,1,-1,-1,1,1,1};glViewport(0,0,width,height);
  glEnable(GL_SCISSOR_TEST);glScissor(dirty[0],dirty[1],dirty[2]-dirty[0],dirty[3]-dirty[1]);
  glVertexAttribPointer(0,2,GL_FLOAT,0,0,quad);glEnableVertexAttribArray(0);
 }else{
  GLfloat quad[]={-1,-1,1,-1,-1,1,1,1};glViewport(0,0,width,height);glEnable(GL_SCISSOR_TEST);glScissor(dirty[0],dirty[1],dirty[2]-dirty[0],dirty[3]-dirty[1]);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glVertexAttribPointer(0,2,GL_FLOAT,0,0,quad);glEnableVertexAttribArray(0);for(int i=0;i<region_count;i++){int*r=regions+i*4;glScissor(r[0],r[1],r[2],r[3]);glDrawArrays(GL_TRIANGLE_STRIP,0,4);}
 }
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,min);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,mag);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,wraps);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,wrapt);
 if(output_ready&&!output_draw(dirty[0],dirty[1],dirty[2],dirty[3]))goto failed;
 glScissor(dirty[0],dirty[1],dirty[2]-dirty[0],dirty[3]-dirty[1]);
 draw_cursor();if(!fence())goto failed;p.session->state.gpu_safe=1;
 if(trace){
  unsigned before_bad=0,after_bad=0,outside_changed=0;
  for(int y=690;y<720;y++)for(int x=0;x<100;x++){
   unsigned char *a=old_roi+((y-690)*100+x)*4,*b=(unsigned char*)p.cpu[slot+2]+(y*width+x)*4,*want=root_roi+((y-690)*100+x)*4;
   int under=p.cursor.visible&&x>=p.cursor.x&&x<p.cursor.x+(int)p.cursor.w&&y>=p.cursor.y&&y<p.cursor.y+(int)p.cursor.h;
   int inside=x>=dirty[0]&&x<dirty[2]&&y>=dirty[1]&&y<dirty[3];
   if(!under){before_bad+=a[0]!=want[2]||a[1]!=want[1]||a[2]!=want[0];after_bad+=b[0]!=want[2]||b[1]!=want[1]||b[2]!=want[0];}
   if(!inside&&memcmp(a,b,4))outside_changed++;
  }
  printf("START_TRACE frame=%u slot=%u dirty=%d,%d-%d,%d mismatch=%u->%u changed_outside=%u\n",gen,slot,dirty[0],dirty[1],dirty[2],dirty[3],before_bad,after_bad,outside_changed);
 }
 if(glGetError())goto failed;
 if(!access("/tmp/dove-shared-root.capture",F_OK)){
  int fd=open("/tmp/dove-shared-root.bgra",O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);size_t total=(size_t)width*height*4,off=0;
  if(fd<0)goto failed;
  while(off<total){ssize_t n=write(fd,(char*)p.cpu[slot+2]+off,total-off);if(n<=0){close(fd);goto failed;}off+=n;}
  close(fd);unlink("/tmp/dove-shared-root.capture");printf("RGB_X_CAPTURE bytes=%zu frame=%u\n",total,gen);
 }
 if(verify){unsigned char *pixels=p.cpu[slot+2];for(int i=0;i<6;i++)for(unsigned c=0;c<4;c++){if(p.cursor.visible&&points[i][0]>=p.cursor.x&&points[i][0]<p.cursor.x+(int)p.cursor.w&&points[i][1]>=p.cursor.y&&points[i][1]<p.cursor.y+(int)p.cursor.h)continue;unsigned want=c==3?255:expected[i][c==0?2:c==2?0:c];unsigned got=pixels[((size_t)points[i][1]*width+points[i][0])*4+c];if(abs((int)got-(int)want)>1)p.session->bad++;}if(p.session->bad){printf("RGB_X_PIXEL_FAILURE frame=%u bad=%u\n",gen,p.session->bad);goto failed;}}
 uint32_t base=p.session->buffers[1].phys+4096,size=p.session->buffers[1].bytes-4096,rp=load(head+8);
 if(rp<base||rp>=base+size||size-(p.write+size-(rp-base))%size<84)goto failed;
 uint32_t cmd[20]={32,80,39,0,0,gen,0xffffffff,0,0,0,width,height,p.session->buffers[slot+2].phys,width*4,0,0,0,0,0,0};
 for(unsigned i=0;i<20;i++)((uint32_t*)((char*)p.cpu[1]+4096))[((p.write+i*4)%size)/4]=htonl(cmd[i]);
 if(p.write+80>=size)p.session->wraps++;p.write=(p.write+80)%size;store(head+4,base+p.write);
 p.copied_pixels+=(unsigned long long)(dirty[2]-dirty[0])*(dirty[3]-dirty[1]);dirty[0]=width;dirty[1]=height;dirty[2]=dirty[3]=0;
 p.slot[slot].previous=gen;p.session->frames=gen;p.session->copies++;p.pending=0;
 /* Keep a fixed cadence: millisecond wake rounding must not accumulate into
    a slower refresh rate. Skip missed deadlines instead of queuing catch-up. */
 const double period=1./60.;
 if(!p.next_present)p.next_present=start+period;
 else {p.next_present+=period;if(p.next_present<=start)p.next_present+=((unsigned)((start-p.next_present)/period)+1)*period;}
 p.rendered_ms+=(now()-start)*1000;
 if(now()-start>.05)fprintf(stderr,"RGB_X_SLOW n=%u total=%.3f root_finish=%.3f ms\n",gen,(now()-start)*1000,(profile_root-start)*1000);
 if(gen==1||gen%60==0)printf("RGB_X_FRAME n=%u bad=%u mean_ms=%.3f mean_fence_ms=%.3f mean_reuse_ms=%.3f copied_ratio=%.5f full_cpu_readback=0\n",gen,p.session->bad,p.rendered_ms/gen,p.fence_ms/gen,p.waiting_ms/gen,(double)p.copied_pixels/((double)gen*width*height));
 result=0;goto restore;
failed:
 p.failed=1;if(p.session)p.session->errors++;fprintf(stderr,"RGB_X_PRESENTER_FAILED egl=%x gl=%x\n",eglGetError(),glGetError());
restore:
 if(!eglMakeCurrent(saved_display,saved_draw,saved_read,saved_context))return -1;return result;
}
void dove_present_close(void){
 EGLDisplay saved_display=eglGetCurrentDisplay();EGLContext saved_context=eglGetCurrentContext();EGLSurface saved_draw=eglGetCurrentSurface(EGL_DRAW),saved_read=eglGetCurrentSurface(EGL_READ);
 int current=p.context&&eglMakeCurrent(p.display,p.surface,p.surface,p.context);
 if(current){glFinish();if(p.session)p.session->state.gpu_safe=1;if(p.program)glDeleteProgram(p.program);if(p.cursor_program)glDeleteProgram(p.cursor_program);if(p.cursor_texture)glDeleteTextures(1,&p.cursor_texture);if(p.verify_fbo)glDeleteFramebuffers(1,&p.verify_fbo);}
 for(unsigned i=0;i<3;i++){if(current){if(p.slot[i].fbo)glDeleteFramebuffers(1,&p.slot[i].fbo);if(p.slot[i].texture)glDeleteTextures(1,&p.slot[i].texture);}if(p.slot[i].image&&p.destroy_image)p.destroy_image(p.display,p.slot[i].image);else if(p.slot[i].memory&&!p.slot[i].attempted&&p.free_memory)p.free_memory(p.slot[i].memory);}
 if(p.base&&p.destroy_base)p.destroy_base(p.base);
 if(p.context){eglMakeCurrent(p.display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(p.display,p.context);}if(p.surface)eglDestroySurface(p.display,p.surface);
 for(int i=0;i<5;i++)if(p.cpu[i]&&p.session)munmap(p.cpu[i],p.session->buffers[i].bytes);
 if(p.session){p.session->closed=1;printf("RGB_X_CLOSED frames=%u bad=%u mean_ms=%.3f fence_ms=%.3f reuse_wait_ms=%.3f copied_ratio=%.5f\n",p.session->frames,p.session->bad,p.session->frames?p.rendered_ms/p.session->frames:0,p.fence_ms,p.waiting_ms,p.session->frames?(double)p.copied_pixels/((double)p.session->frames*p.session->width*p.session->height):0);munmap(p.session,RGB_SESSION_BYTES);}
 if(p.mali)dlclose(p.mali);memset(&p,0,sizeof p);
 eglMakeCurrent(saved_display,saved_draw,saved_read,saved_context);
}
