#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
static int same(const char *id,XClassHint *c){return(c->res_name&&!strcasecmp(id,c->res_name))||(c->res_class&&!strcasecmp(id,c->res_class));}
static Window find(Display *d,const char *id){
 Atom actual;int format;unsigned long n,left;unsigned char *value=NULL;Window root=DefaultRootWindow(d),found=0;
 if(XGetWindowProperty(d,root,XInternAtom(d,"_NET_CLIENT_LIST",False),0,4096,False,XA_WINDOW,&actual,&format,&n,&left,&value)==Success&&format==32&&value){
  Window *list=(Window*)value;for(unsigned long i=0;i<n;i++){
   Atom type;int fmt;unsigned long count,remaining;unsigned char *types=NULL;int desktop=0;
   XGetWindowProperty(d,list[i],XInternAtom(d,"_NET_WM_WINDOW_TYPE",False),0,32,False,XA_ATOM,&type,&fmt,&count,&remaining,&types);
   if(types&&fmt==32){Atom *atoms=(Atom*)types;for(unsigned long j=0;j<count;j++)if(atoms[j]==XInternAtom(d,"_NET_WM_WINDOW_TYPE_DESKTOP",False))desktop=1;}
   if(types)XFree(types);
   if(desktop)continue;
   if(!strcasecmp(id,"pcmanfm")){char *name=NULL;if(XFetchName(d,list[i],&name)&&name){int background=!strcmp(name,"pcmanfm");XFree(name);if(background)continue;}}
   XClassHint c={0};if(XGetClassHint(d,list[i],&c)){if(same(id,&c))found=list[i];if(c.res_name)XFree(c.res_name);if(c.res_class)XFree(c.res_class);if(found)break;}
  }
 }if(value)XFree(value);return found;
}
static int focus(const char *id,int page){
 Display *d=XOpenDisplay(NULL);if(!d)return 0;Window w=find(d,id);
 if(w){
  if(page>=0){XEvent p={0};p.xclient.type=ClientMessage;p.xclient.window=w;p.xclient.message_type=XInternAtom(d,"_DOVEOS_SETTINGS_PAGE",False);p.xclient.format=32;p.xclient.data.l[0]=page;XSendEvent(d,w,False,NoEventMask,&p);}
  XEvent e={0};e.xclient.type=ClientMessage;e.xclient.window=w;e.xclient.message_type=XInternAtom(d,"_NET_ACTIVE_WINDOW",False);e.xclient.format=32;e.xclient.data.l[0]=2;e.xclient.data.l[1]=CurrentTime;
  XSendEvent(d,DefaultRootWindow(d),False,SubstructureRedirectMask|SubstructureNotifyMask,&e);XMapRaised(d,w);XFlush(d);
 }XCloseDisplay(d);return w!=0;
}
int main(int argc,char **argv){
 if(argc<3)return 2;
 const char *id=argv[1];for(const char *p=id;*p;p++)if(!isalnum((unsigned char)*p)&&*p!='-'&&*p!='_')return 2;
 int page=-1,index=2;if(argc>4&&!strcmp(argv[2],"--page")){page=atoi(argv[3]);index=4;}if(index>=argc)return 2;
 if(focus(id,page)){puts("FOCUSED_EXISTING_WINDOW");return 0;}
 char path[160];snprintf(path,sizeof path,"/tmp/doveos-app-%s.lock",id);int fd=open(path,O_CREAT|O_RDWR,0600);if(fd<0)return 2;
 if(flock(fd,LOCK_EX|LOCK_NB)){for(int i=0;i<25;i++){if(focus(id,page))return 0;usleep(200000);}return 0;}
 if(focus(id,page)){close(fd);return 0;}
 pid_t child=fork();if(child==0){close(fd);execvp(argv[index],argv+index);_exit(127);}if(child<0){close(fd);return 2;}
 char busy_path[160];snprintf(busy_path,sizeof busy_path,"/tmp/doveos-cursor-busy/%ld",(long)child);
 mkdir("/tmp/doveos-cursor-busy",0700);
 int status=0,finished=0;
 for(int n=0;n<75;n++){
  pid_t r=waitpid(child,&status,WNOHANG);if(r==child){finished=1;break;}
  Display *d=XOpenDisplay(NULL);int ready=d&&find(d,id);if(d)XCloseDisplay(d);if(ready)break;
  if(n==1){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);unsigned deadline=(unsigned)(t.tv_sec*1000u+t.tv_nsec/1000000u)+15000;
   FILE *b=fopen(busy_path,"w");if(b){fprintf(b,"%ld %u\n",(long)child,deadline);fclose(b);}
  }
  usleep(200000);
 }
 unlink(busy_path);
 if(finished){close(fd);return WIFEXITED(status)?WEXITSTATUS(status):1;}
 while(waitpid(child,&status,0)<0){if(errno!=EINTR){close(fd);return 1;}}close(fd);return WIFEXITED(status)?WEXITSTATUS(status):1;
}
