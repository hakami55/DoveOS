#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
int main(int argc,char**argv){sigset_t m;sigemptyset(&m);sigprocmask(SIG_SETMASK,&m,0);alarm(15);setvbuf(stdout,0,_IOLBF,0);int fd=open("/dev/mali",O_RDWR);struct{uint32_t ctx,padding,ver,compatible;}v={fd,0,0x02580258,0};if(fd<0)return 1;int r=ioctl(fd,0xc0108203,&v);printf("MALI_API result=%d version=%u compatible=%u\n",r,v.ver&65535,v.compatible);close(fd);if(r||!v.compatible||(v.ver&65535)!=600)return 1;if(argc<2)return 0;void*l=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!l){printf("LOAD_ERROR=%s\n",dlerror());return 2;}

EGLDisplay (*eglGetDisplay)(EGLNativeDisplayType)=dlsym(l,"eglGetDisplay");EGLBoolean (*eglInitialize)(EGLDisplay,EGLint*,EGLint*)=dlsym(l,"eglInitialize");EGLint (*eglGetError)(void)=dlsym(l,"eglGetError");const char* (*eglQueryString)(EGLDisplay,EGLint)=dlsym(l,"eglQueryString");EGLBoolean (*eglTerminate)(EGLDisplay)=dlsym(l,"eglTerminate");
EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);printf("EGL_DISPLAY=%p error=%x\n",d,eglGetError());EGLint ma=0,mi=0;EGLBoolean ok=eglInitialize(d,&ma,&mi);printf("EGL_INIT=%u version=%d.%d error=%x\n",ok,ma,mi,eglGetError());if(!ok){void(*dump)(void)=dlsym(RTLD_DEFAULT,"trace_dump");if(dump)dump();return 3;}printf("EGL_VENDOR=%s\nEGL_EXTENSIONS=%s\n",eglQueryString(d,EGL_VENDOR),eglQueryString(d,EGL_EXTENSIONS));
EGLBoolean (*eglChooseConfig)(EGLDisplay,const EGLint*,EGLConfig*,EGLint,EGLint*)=dlsym(l,"eglChooseConfig");EGLBoolean (*eglBindAPI)(EGLenum)=dlsym(l,"eglBindAPI");EGLSurface (*eglCreatePbufferSurface)(EGLDisplay,EGLConfig,const EGLint*)=dlsym(l,"eglCreatePbufferSurface");EGLContext (*eglCreateContext)(EGLDisplay,EGLConfig,EGLContext,const EGLint*)=dlsym(l,"eglCreateContext");EGLBoolean (*eglMakeCurrent)(EGLDisplay,EGLSurface,EGLSurface,EGLContext)=dlsym(l,"eglMakeCurrent");const GLubyte* (*glGetString)(GLenum)=dlsym(l,"glGetString");void (*glClearColor)(GLfloat,GLfloat,GLfloat,GLfloat)=dlsym(l,"glClearColor");void (*glClear)(GLbitfield)=dlsym(l,"glClear");void (*glFinish)(void)=dlsym(l,"glFinish");void (*glReadPixels)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*)=dlsym(l,"glReadPixels");
EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};EGLConfig c;EGLint count=0;eglChooseConfig(d,ca,&c,1,&count);printf("PBUFFER_CONFIGS=%d\n",count);if(!count)return 4;eglBindAPI(EGL_OPENGL_ES_API);EGLint sa[]={EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE},xa[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};EGLSurface s=eglCreatePbufferSurface(d,c,sa);printf("PBUFFER=%p error=%x\n",s,eglGetError());EGLContext x=eglCreateContext(d,c,EGL_NO_CONTEXT,xa);printf("CONTEXT=%p error=%x\n",x,eglGetError());if(!eglMakeCurrent(d,s,s,x)){printf("CONTEXT_ERROR=%x\n",eglGetError());return 5;}printf("GL_VENDOR=%s GL_RENDERER=%s GL_VERSION=%s\n",glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION));glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glFinish();unsigned char px[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);printf("GPU_RED_PIXEL=%u,%u,%u,%u\n",px[0],px[1],px[2],px[3]);eglTerminate(d);dlclose(l);return px[0]>250&&px[1]<5?0:6;}
