#include <X11/Xlib.h>
extern int XTestFakeMotionEvent(Display*,int,int,int,unsigned long);
extern int XTestFakeButtonEvent(Display*,unsigned int,Bool,unsigned long);
#include <stdlib.h>
#include <unistd.h>
int main(void){setenv("DISPLAY",":7",1);Display *d=XOpenDisplay(0);if(!d)return 1;XTestFakeMotionEvent(d,-1,450,359,0);XTestFakeButtonEvent(d,1,True,0);XTestFakeButtonEvent(d,1,False,0);XFlush(d);return 0;}
