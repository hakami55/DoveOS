#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/wait.h>
static int markers(void){DIR *d=opendir("/tmp/doveos-cursor-busy");if(!d)return 0;int n=0;struct dirent *e;while((e=readdir(d)))if(e->d_name[0]!='.')n++;closedir(d);return n;}
int main(int argc,char **argv){
 Display *d=XOpenDisplay(NULL);if(!d)return 2;
 if(argc==2){
  sleep(2);Window v=XCreateSimpleWindow(d,DefaultRootWindow(d),100,100,200,100,0,0,0xffffff);
  XClassHint hint={"DoveCursorLaunch","DoveCursorLaunch"};XSetClassHint(d,v,&hint);XStoreName(d,v,"Launch feedback verification");XMapRaised(d,v);XSync(d,False);sleep(2);XDestroyWindow(d,v);XCloseDisplay(d);return 0;
 }
 Window root=DefaultRootWindow(d),w=XCreateSimpleWindow(d,root,200,200,200,180,0,0,0xffffff);
 XStoreName(d,w,"Cursor verification");XMapRaised(d,w);XSync(d,False);usleep(500000);
 int fd=open("/dev/fb0",O_RDONLY);uint32_t *fb=mmap(NULL,1280*720*4,PROT_READ,MAP_SHARED,fd,0);
 if(fb==MAP_FAILED)return 3;
 unsigned shapes[]={XC_left_ptr,XC_watch,XC_xterm,XC_hand2,XC_fleur,XC_sb_h_double_arrow,XC_sb_v_double_arrow,XC_top_left_corner,XC_top_right_corner,XC_circle};
 const char *names[]={"arrow","hourglass","text","link","move","horizontal","vertical","diagonal-NW","diagonal-NE","unavailable"};
 int bad=0;
 for(unsigned n=0;n<sizeof shapes/sizeof shapes[0];n++){
  Cursor c=XCreateFontCursor(d,shapes[n]);XDefineCursor(d,w,c);XWarpPointer(d,None,w,0,0,0,0,80,70);XSync(d,False);usleep(350000);
  XFixesCursorImage *im=XFixesGetCursorImage(d);unsigned visible=0,match=0;uint32_t hash=0;
  for(int j=0;j<im->height;j++)for(int i=0;i<im->width;i++){
   uint32_t p=im->pixels[j*im->width+i];hash=hash*33+p;
   if(p>>24){visible++;int x=im->x-im->xhot+i,y=im->y-im->yhot+j;if(x>=0&&x<1280&&y>=0&&y<720&&fb[y*1280+x]==p)match++;}
  }
  printf("CURSOR %s %ux%u hotspot=%u,%u visible=%u overlay_match=%u hash=%08x\n",names[n],im->width,im->height,im->xhot,im->yhot,visible,match,hash);
  if(!visible||match!=visible)bad++;
  XFree(im);XFreeCursor(d,c);
 }
 Cursor arrow=XCreateFontCursor(d,XC_left_ptr);XDefineCursor(d,w,arrow);XSync(d,False);usleep(250000);
 mkdir("/tmp/doveos-cursor-busy",0700);char path[128];snprintf(path,sizeof path,"/tmp/doveos-cursor-busy/%d",getpid());
 struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);unsigned deadline=(unsigned)(t.tv_sec*1000u+t.tv_nsec/1000000u)+2000;
 FILE *f=fopen(path,"w");fprintf(f,"%d %u\n",getpid(),deadline);fclose(f);usleep(500000);
 unsigned a=0,b=0;for(int i=0;i<1280*720;i++)a=a*33+fb[i];usleep(170000);
 for(int i=0;i<1280*720;i++)b=b*33+fb[i];
 printf("LAUNCH_BUSY_ANIMATION %08x %08x\n",a,b);if(a==b)bad++;
 unlink(path);usleep(300000);
 XDestroyWindow(d,w);XFreeCursor(d,arrow);XWarpPointer(d,None,root,0,0,0,0,640,360);XSync(d,False);
 pid_t launch=fork();if(!launch){execl("/tmp/doveos-theme/doveos-open","doveos-open","DoveCursorLaunch","/tmp/doveos-cursor-probe","--launch-window",NULL);_exit(127);}
 usleep(600000);int during=markers();usleep(2300000);int after=markers();
 printf("REAL_LAUNCH_FEEDBACK markers_during=%d markers_after_window=%d\n",during,after);if(during<1||after)bad++;
 int status;waitpid(launch,&status,0);
 munmap(fb,1280*720*4);close(fd);XCloseDisplay(d);printf("CURSOR_CHECK_BAD=%d\n",bad);return bad?1:0;
}
