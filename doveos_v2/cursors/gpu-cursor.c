/* Small display-overlay cursor for the isolated accelerated desktop. */
#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XTest.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include "busy-frames.h"
static volatile sig_atomic_t done;
static void stop(int sig){(void)sig;done=1;}
static long external_owner(void){FILE*f=fopen("/tmp/doveos-direct-display","r");long p=0;if(f){if(fscanf(f,"%ld",&p)!=1)p=0;fclose(f);}return p>1&&p<4194304&&!kill(p,0)?p:0;}
static int enable_backing(Display*d){
 void*lib=dlopen("libXcomposite.so.1",RTLD_NOW|RTLD_LOCAL);if(!lib)return -1;
 Bool(*query)(Display*,int*,int*)=dlsym(lib,"XCompositeQueryExtension");
 Status(*version)(Display*,int*,int*)=dlsym(lib,"XCompositeQueryVersion");
 void(*redirect)(Display*,Window,int)=dlsym(lib,"XCompositeRedirectSubwindows");
 int event,error,major=0,minor=4;
 if(!query||!version||!redirect||!query(d,&event,&error)||!version(d,&major,&minor))return -1;
 redirect(d,DefaultRootWindow(d),0);XSync(d,False);
 printf("GPU_WINDOW_BACKING_READY version=%d.%d\n",major,minor);return 0;
}
static void clear(uint32_t *fb,int x,int y,int w,int h){for(int j=0;j<h;j++)for(int i=0;i<w;i++)if(x+i>=0&&x+i<1280&&y+j>=0&&y+j<720)fb[(y+j)*1280+x+i]=0;}
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
 if(argc!=2)return 2;int owner=atoi(argv[1]);if(owner<=1)return 2;
 Display *d=XOpenDisplay(NULL);if(!d)return 3;
 if(enable_backing(d))return 6;
 int fd=open("/dev/fb0",O_RDWR|O_CLOEXEC);struct fb_var_screeninfo v={0};struct fb_fix_screeninfo f={0};
 if(fd<0||ioctl(fd,FBIOGET_VSCREENINFO,&v)||ioctl(fd,FBIOGET_FSCREENINFO,&f)||v.xres!=1280||v.yres!=720||v.bits_per_pixel!=32||f.line_length!=5120)return 4;
 uint32_t *fb=mmap(NULL,1280*720*4,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);if(fb==MAP_FAILED)return 5;
 signal(SIGTERM,stop);signal(SIGINT,stop);int x=0,y=0,w=0,h=0,paused=0;unsigned long serial=0;unsigned moves=0;int busy=0,last_busy=-1,last_frame=-1;uint32_t busy_check=0;
 XTestGrabControl(d,True);XFixesHideCursor(d,DefaultRootWindow(d));XSync(d,False);
 FILE*pidfile=fopen("/tmp/doveos-gpu-cursor.pid","w");if(pidfile){fprintf(pidfile,"%d\n",getpid());fclose(pidfile);}
 puts("GPU_CURSOR_OVERLAY_READY");fflush(stdout);
 while(!done&&!kill(owner,0)){
  long external=external_owner();
  if(external||!access("/tmp/doveos-gpu-display-paused",F_OK)){
   if(!paused){clear(fb,x,y,w,h);__sync_synchronize();paused=1;}
   if(external){FILE*f=fopen("/tmp/doveos-gpu-cursor-paused","w");if(f){fprintf(f,"%ld\n",external);fclose(f);}}
   usleep(16000);continue;
  }
  if(paused){paused=0;w=h=0;serial=0;last_busy=-1;last_frame=-1;unlink("/tmp/doveos-gpu-cursor-paused");}
  XFixesCursorImage *c=XFixesGetCursorImage(d);if(!c){usleep(16000);continue;}
  uint32_t now=clock_ms();if((uint32_t)(now-busy_check)>=100){busy=launch_busy();busy_check=now;}
  int frame=busy?(now/150)%(sizeof busy_frames/sizeof busy_frames[0]):-1;
  if(busy){const struct BusyFrame *b=&busy_frames[frame];
   {c->width=b->w;c->height=b->h;c->xhot=b->hx;c->yhot=b->hy;
    
   }
  }
  int nx=c->x-c->xhot,ny=c->y-c->yhot;
  if(c->width<=128&&c->height<=128&&(x!=nx||y!=ny||serial!=c->cursor_serial||busy!=last_busy||frame!=last_frame||!moves)){
   clear(fb,x,y,w,h);x=nx;y=ny;w=c->width;h=c->height;serial=c->cursor_serial;last_busy=busy;last_frame=frame;
   for(int j=0;j<h;j++)for(int i=0;i<w;i++)if(x+i>=0&&x+i<1280&&y+j>=0&&y+j<720)fb[(y+j)*1280+x+i]=busy?busy_frames[frame].pixels[j*w+i]:(uint32_t)c->pixels[j*w+i];
   __sync_synchronize();moves++;
  }
  XFree(c);usleep(16000);
 }
 if(!paused)clear(fb,x,y,w,h);__sync_synchronize();
 unlink("/tmp/doveos-gpu-cursor.pid");unlink("/tmp/doveos-gpu-cursor-paused");
 XFixesShowCursor(d,DefaultRootWindow(d));XSync(d,False);
 munmap(fb,1280*720*4);close(fd);XCloseDisplay(d);printf("GPU_CURSOR_CLOSED updates=%u\n",moves);return 0;
}
