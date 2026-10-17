/* Integration test: click within the visible DovePlayer window only. */
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
static Window find(Display *d,Window root){
 char *name=NULL;if(XFetchName(d,root,&name)&&name){int yes=!strcmp(name,"DovePlayer");XFree(name);if(yes)return root;}
 Window r,parent,*children=NULL;unsigned count=0;Window found=0;
 if(XQueryTree(d,root,&r,&parent,&children,&count)){for(unsigned i=0;i<count&&!found;i++)found=find(d,children[i]);if(children)XFree(children);}return found;
}
int main(int argc,char **argv){
 if(argc!=3)return 2;
 Display *d=XOpenDisplay(":7");if(!d)return 3;
 Window w=find(d,DefaultRootWindow(d));if(!w)return 4;
 XWindowAttributes attr;if(!XGetWindowAttributes(d,w,&attr)||attr.map_state!=IsViewable)return 5;
 int x=atoi(argv[1]),y=atoi(argv[2]);if(x<0||y<0||x>=attr.width||y>=attr.height)return 6;
 void *lib=dlopen("libXtst.so.6",RTLD_NOW);if(!lib)return 7;
 int (*button)(Display*,unsigned,int,unsigned long)=dlsym(lib,"XTestFakeButtonEvent");if(!button)return 8;
 XRaiseWindow(d,w);XWarpPointer(d,None,w,0,0,0,0,x,y);XSync(d,False);
 button(d,1,True,0);button(d,1,False,0);XSync(d,False);XCloseDisplay(d);return 0;
}
