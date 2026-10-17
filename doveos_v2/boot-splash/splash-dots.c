#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#define WIDTH 1280
#define HEIGHT 720
#define LEFT 592
#define TOP 652
#define RW 96
#define RH 24
#define HANDOFF "/tmp/doveos-splash-handoff"
#define PIDFILE "/tmp/doveos-splash-dots.pid"
static volatile sig_atomic_t stopping;
static uint32_t saved[RW*RH],last[RW*RH];
static void interrupted(int sig){(void)sig;stopping=1;}
static double monotonic(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void pause_tick(void){struct timespec t={0,50000000};nanosleep(&t,0);}
static int format_ok(int fd,struct fb_var_screeninfo *v,struct fb_fix_screeninfo *f){
 return !ioctl(fd,FBIOGET_VSCREENINFO,v)&&!ioctl(fd,FBIOGET_FSCREENINFO,f)&&
  v->xres==WIDTH&&v->yres==HEIGHT&&v->bits_per_pixel==32&&f->line_length==WIDTH*4&&f->smem_len>=WIDTH*HEIGHT*4&&
  v->xoffset==0&&v->yoffset==0&&v->red.offset==16&&v->green.offset==8&&v->blue.offset==0&&v->transp.offset==24&&
  v->red.length==8&&v->green.length==8&&v->blue.length==8&&v->transp.length==8;
}
static void compose(unsigned dots,int slow){
 memcpy(last,saved,sizeof last);
 for(unsigned d=0;d<dots;d++)for(int y=-5;y<=5;y++)for(int x=-5;x<=5;x++){
  int r=x*x+y*y;if(r>25)continue;
  unsigned px=21+d*18+x,py=12+y;
  uint32_t color=slow?0xffffbf52U:0xffeeeeeeU;
  if(r>16)color=slow?0xff9a7331U:0xff777777U;
  last[py*RW+px]=color;
 }
}
static void draw(volatile uint32_t *fb){for(int y=0;y<RH;y++)for(int x=0;x<RW;x++)fb[(TOP+y)*WIDTH+LEFT+x]=last[y*RW+x];}
static void restore(volatile uint32_t *fb){for(int y=0;y<RH;y++)for(int x=0;x<RW;x++){
 unsigned at=(TOP+y)*WIDTH+LEFT+x;if(fb[at]==last[y*RW+x])fb[at]=saved[y*RW+x];
}}
static int dark(uint32_t p){return ((p>>16)&255)<12&&((p>>8)&255)<12&&(p&255)<12;}
static int logo_ready(volatile uint32_t *fb){
 return dark(fb[0])&&dark(fb[WIDTH-1])&&dark(fb[TOP*WIDTH+LEFT])&&
  !dark(fb[300*WIDTH+WIDTH/2])&&!dark(fb[400*WIDTH+WIDTH/2]);
}
static int alive(pid_t p){char path[80],buf[256];snprintf(path,sizeof path,"/proc/%d/stat",p);FILE *f=fopen(path,"r");if(!f)return 0;
 char *ok=fgets(buf,sizeof buf,f);fclose(f);if(!ok)return 0;char *end=strrchr(buf,')');return end&&end[1]==' '&&end[2]!='Z'&&end[2]!='T'&&end[2]!='X';}
static int startup_alive(void){FILE *f=fopen("/tmp/doveos-v2.pid","r");if(!f)return 0;int p=0;int n=fscanf(f,"%d",&p);fclose(f);return n==1&&p>1&&alive(p);}
static int owned(pid_t p){char path[80],exe[256];snprintf(path,sizeof path,"/proc/%d/exe",p);ssize_t n=readlink(path,exe,sizeof exe-1);if(n<0)return 0;exe[n]=0;return !strcmp(exe,"/usr/local/etc/doveos-foreground-trial-20261003/splash-dots");}
static int handoff(void){
 FILE *f=fopen(HANDOFF,"w");if(!f)return 1;fclose(f);
 f=fopen(PIDFILE,"r");if(!f)return 0;int p=0;int count=fscanf(f,"%d",&p);fclose(f);
 if(count!=1||p<=1||!owned(p))return 0;
 kill(p,SIGTERM);for(int i=0;i<10&&alive(p);i++)pause_tick();
 if(alive(p)&&owned(p)){kill(p,SIGKILL);puts("DOTS_HANDOFF_FORCED");}else puts("DOTS_HANDOFF_COMPLETE");return 0;
}
int main(int argc,char **argv){
 setvbuf(stdout,0,_IONBF,0);
 if(argc==2&&!strcmp(argv[1],"--stop"))return handoff();
 if(argc==2&&!strcmp(argv[1],"--self-test")){
  for(unsigned i=0;i<RW*RH;i++)saved[i]=0xff010203U;
  for(unsigned n=1;n<=4;n++){compose(n,0);unsigned count=0;for(unsigned i=0;i<RW*RH;i++)count+=last[i]!=saved[i];if(count!=n*81)return 10;
   for(int x=0;x<RW;x++)if(last[x]!=saved[x]||last[(RH-1)*RW+x]!=saved[(RH-1)*RW+x])return 11;
   for(int y=0;y<RH;y++)if(last[y*RW]!=saved[y*RW]||last[y*RW+RW-1]!=saved[y*RW+RW-1])return 12;
  }puts("DOTS_SELF_TEST_PASSED bounds=96x24 counts=81,162,243,324");return 0;
 }
 int probe=argc==2&&!strcmp(argv[1],"--probe");
 if(!probe&&(argc!=3||strcmp(argv[1],"--run")))return 2;
 char *end=0;long hp=probe?0:strtol(argv[2],&end,10);if(!probe&&(*end||hp<=1||hp>2147483647))return 2;
 int fd=open("/dev/fb0",(probe?O_RDONLY:O_RDWR)|O_CLOEXEC);struct fb_var_screeninfo v={0};struct fb_fix_screeninfo f={0};
 if(fd<0||!format_ok(fd,&v,&f)){puts("DOTS_FRAMEBUFFER_UNSUPPORTED");if(fd>=0)close(fd);return 3;}
 if(probe){printf("DOTS_FORMAT_OK %ux%u bpp=%u stride=%u offset=%u,%u\n",v.xres,v.yres,v.bits_per_pixel,f.line_length,v.xoffset,v.yoffset);close(fd);return 0;}
 if(access(HANDOFF,F_OK)==0){close(fd);puts("DOTS_SKIP_DESKTOP_HANDOFF");return 0;}
 uint32_t *map=mmap(0,WIDTH*HEIGHT*4,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);if(map==MAP_FAILED){close(fd);return 4;}
 signal(SIGINT,interrupted);signal(SIGTERM,interrupted);signal(SIGHUP,interrupted);
 FILE *pf=fopen(PIDFILE,"w");if(!pf){munmap(map,WIDTH*HEIGHT*4);close(fd);return 5;}fprintf(pf,"%ld\n",(long)getpid());fclose(pf);
 double begin=monotonic(),next=0;unsigned frames=0;int started=0,leave_warning=0;const char *reason="timeout";
 while(!stopping){
  double now=monotonic(),elapsed=now-begin;
  if(access(HANDOFF,F_OK)==0){reason="desktop-handoff";break;}
  if(elapsed>=60){reason="startup-timeout";leave_warning=started;break;}
  if(now<next){pause_tick();continue;}next=now+1.0;
  if(!alive((pid_t)hp)){reason="hp-service-stopped";break;}
  if(elapsed>30&&!startup_alive()){reason="desktop-launch-not-running";leave_warning=started;break;}
  if(!format_ok(fd,&v,&f)){reason="display-mode-changed";break;}
  if(!started){if(!logo_ready(map))continue;
   for(int y=0;y<RH;y++)for(int x=0;x<RW;x++)saved[y*RW+x]=map[(TOP+y)*WIDTH+LEFT+x];
   started=1;puts("DOTS_ANIMATION_STARTED interval_ms=1000 region=96x24");
  }
  compose(frames%4+1,0);draw(map);frames++;
 }
 if(stopping)reason="handoff-signal";
 if(started&&format_ok(fd,&v,&f)){if(leave_warning){compose(4,1);draw(map);}else restore(map);}
 printf("DOTS_STOP reason=%s frames=%u elapsed=%.2f\n",reason,frames,monotonic()-begin);
 munmap(map,WIDTH*HEIGHT*4);close(fd);unlink(PIDFILE);
 FILE *done=fopen("/tmp/doveos-splash-dots.stopped","w");if(done){fprintf(done,"%s\n",reason);fclose(done);}return 0;
}
