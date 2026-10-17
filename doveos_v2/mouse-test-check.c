#include <X11/Xlib.h>
extern int XTestFakeMotionEvent(Display*,int,int,int,unsigned long);
extern int XTestFakeButtonEvent(Display*,unsigned int,Bool,unsigned long);
#include <stdlib.h>
#include <unistd.h>
extern int XTestFakeKeyEvent(Display*,unsigned int,Bool,unsigned long);
int main(void){Display*d=XOpenDisplay(":7");if(!d)return 1;XTestFakeMotionEvent(d,-1,250,250,0);XSync(d,False);usleep(150000);XTestFakeMotionEvent(d,-1,320,300,0);for(unsigned b=1;b<=4;b++){XTestFakeButtonEvent(d,b,True,0);XTestFakeButtonEvent(d,b,False,0);}XSync(d,False);usleep(500000);XCloseDisplay(d);return 0;}

