#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>
int main(int argc,char **argv){
 if(argc!=3)return 2;
 Display *d=XOpenDisplay(":7");if(!d)return 3;
 int x=atoi(argv[1]),y=atoi(argv[2]);
 XWarpPointer(d,None,DefaultRootWindow(d),0,0,0,0,x,y);
 XSync(d,False);XCloseDisplay(d);
 printf("POINTER_AT %d %d\n",x,y);return 0;
}
