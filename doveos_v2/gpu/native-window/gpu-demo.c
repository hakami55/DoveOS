#define _DEFAULT_SOURCE
#include "dove_gpu_window.h"
#include <EGL/egl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static int thermal_ok(void){FILE*f=fopen("/sys/class/thermal/thermal_zone0/temp","r");int t;if(!f)return 0;int ok=fscanf(f,"%d",&t)==1&&t<86000;fclose(f);return ok;}
static GLuint shader(GLenum type,const char*source){GLuint s=glCreateShader(type);glShaderSource(s,1,&source,0);glCompileShader(s);GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){glDeleteShader(s);return 0;}return s;}
int main(int argc,char**argv){
 setvbuf(stdout,0,_IOLBF,0);int frames=argc>1?atoi(argv[1]):1800;if(frames<1||frames>36000)return 2;
 Display*x=XOpenDisplay(0);if(!x)return 2;int full=getenv("DOVE_DEMO_FULLSCREEN")!=NULL;
 Window win=XCreateSimpleWindow(x,DefaultRootWindow(x),full?0:180,full?0:100,full?1280:800,full?720:450,0,0,0x203040);
 if(full){XSetWindowAttributes a={.override_redirect=True};XChangeWindowAttributes(x,win,CWOverrideRedirect,&a);}
 XStoreName(x,win,"DoveOS native GPU demo");XSelectInput(x,win,StructureNotifyMask|KeyPressMask);Atom close=XInternAtom(x,"WM_DELETE_WINDOW",False);XSetWMProtocols(x,win,&close,1);XMapRaised(x,win);XSync(x,False);
 EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);EGLConfig cfg;EGLint n;
 EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
 EGLint pa[]={EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE},co[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
 if(!eglInitialize(d,0,0)||!eglChooseConfig(d,ca,&cfg,1,&n)||!n)return 3;
 EGLSurface surface=eglCreatePbufferSurface(d,cfg,pa);EGLContext ctx=eglCreateContext(d,cfg,0,co);if(!surface||!ctx||!eglMakeCurrent(d,surface,surface,ctx))return 3;
 GLuint v=shader(GL_VERTEX_SHADER,"attribute vec2 position;void main(){gl_Position=vec4(position,0.,1.);}");
 GLuint f=shader(GL_FRAGMENT_SHADER,"precision mediump float;uniform vec2 size;uniform float phase;void main(){vec2 uv=gl_FragCoord.xy/size;vec3 c=vec3(uv.x*.5+.15,uv.y*.5+.2,.55);if(abs(uv.x-phase)<.025&&abs(uv.y-.5)<.06)c=vec3(1.);gl_FragColor=vec4(c,1.);}");
 GLuint program=glCreateProgram();glAttachShader(program,v);glAttachShader(program,f);glBindAttribLocation(program,0,"position");glLinkProgram(program);GLint linked;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked)return 4;
 glDeleteShader(v);glDeleteShader(f);GLint size=glGetUniformLocation(program,"size"),phase=glGetUniformLocation(program,"phase");GLfloat quad[]={-1,-1,1,-1,-1,1,1,1};
 DoveGpuWindow*w=dove_gpu_window_attach(win);if(!w)return 5;int errors=0,rendered=0,late=0;double max=0,steady_max=0,setup_work=0,start=now();int steady_late=0;
 double previous=0,max_interval=0;unsigned intervals[128]={0},gap25=0,gap33=0;
 for(int i=0;i<frames;i++){
  if(i%60==0&&!thermal_ok()){fprintf(stderr,"GPU_DEMO_THERMAL_STOP\n");errors++;break;}
  int done=0;while(XPending(x)){XEvent e;XNextEvent(x,&e);if(e.type==DestroyNotify||(e.type==ClientMessage&&(Atom)e.xclient.data.l[0]==close)||e.type==KeyPress)done=1;}if(done)break;
  double wait=start+i/60.-now();if(wait>0){struct timespec t={(time_t)wait,(long)((wait-(time_t)wait)*1e9)};nanosleep(&t,0);}
  double b=now();int width,height,ok=dove_gpu_window_begin(w,&width,&height);if(ok<0){errors++;break;}if(!ok){usleep(16000);continue;}
  glUseProgram(program);glVertexAttribPointer(0,2,GL_FLOAT,False,0,quad);glEnableVertexAttribArray(0);glUniform2f(size,width,height);glUniform1f(phase,(i%240)/240.f);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
  if(!dove_gpu_window_present(w)){errors++;break;}rendered++;double work=now()-b;if(work>max)max=work;if(work>1./60.)late++;if(rendered==1)setup_work=work;else {if(work>steady_max)steady_max=work;if(work>1./60.)steady_late++;}
  double complete=now();if(previous){double dt=complete-previous;if(dt>max_interval)max_interval=dt;unsigned bin=(unsigned)(dt*1000);if(bin>127)bin=127;intervals[bin]++;if(dt>.025)gap25++;if(dt>1./30.)gap33++;}previous=complete;
 }
 if(!dove_gpu_window_flush(w))errors++;if(!dove_gpu_window_detach(w))errors++;
 double seconds=now()-start;printf("GPU_DEMO rendered=%d seconds=%.6f fps=%.3f max_work_ms=%.3f over_budget=%d setup_ms=%.3f steady_max_work_ms=%.3f steady_over_budget=%d errors=%d\n",rendered,seconds,rendered/seconds,max*1000,late,setup_work*1000,steady_max*1000,steady_late,errors);
 unsigned sum=0,p95=0,target=rendered>1?(unsigned)((rendered-1)*.95+.999):0;
 for(unsigned i=0;i<128;i++){sum+=intervals[i];if(sum>=target){p95=i;break;}}
 printf("GPU_DEMO_CADENCE intervals=%d p95_ms_upper=%u max_interval_ms=%.3f gaps_over_25ms=%u gaps_over_33ms=%u\n",rendered>0?rendered-1:0,p95+1,max_interval*1000,gap25,gap33);
 glDeleteProgram(program);eglMakeCurrent(d,0,0,0);eglDestroyContext(d,ctx);eglDestroySurface(d,surface);eglTerminate(d);XDestroyWindow(x,win);XSync(x,False);XCloseDisplay(x);return errors?1:0;
}
