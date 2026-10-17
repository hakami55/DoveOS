#include <X11/Xlib.h>
extern int XTestFakeMotionEvent(Display*,int,int,int,unsigned long);
extern int XTestFakeButtonEvent(Display*,unsigned int,Bool,unsigned long);
#include <stdlib.h>
#include <unistd.h>
extern int XTestFakeKeyEvent(Display*,unsigned int,Bool,unsigned long);
int main(void){Display*d=XOpenDisplay(":7");if(!d)return 1;XTestFakeKeyEvent(d,XKeysymToKeycode(d,0xffe3),True,0);XTestFakeKeyEvent(d,XKeysymToKeycode(d,0xff1b),True,0);XTestFakeKeyEvent(d,XKeysymToKeycode(d,0xff1b),False,0);XTestFakeKeyEvent(d,XKeysymToKeycode(d,0xffe3),False,0);XSync(d,False);usleep(500000);XCloseDisplay(d);return 0;}
