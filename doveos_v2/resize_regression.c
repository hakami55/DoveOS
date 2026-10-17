#define _DEFAULT_SOURCE
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <unistd.h>

static void motion(Display *d,int x,int y){XTestFakeMotionEvent(d,-1,x,y,CurrentTime);XFlush(d);usleep(30000);}
static void button(Display *d,int down){XTestFakeButtonEvent(d,1,down,CurrentTime);XFlush(d);usleep(50000);}
static Window frame(Display *d,Window w){Window root,parent,*children=NULL;unsigned n;XQueryTree(d,w,&root,&parent,&children,&n);if(children)XFree(children);return parent;}
int main(void){
    Display *d=XOpenDisplay(":7");if(!d)return 2;
    XTestGrabControl(d,True);
    Window w=XCreateSimpleWindow(d,DefaultRootWindow(d),300,220,320,180,0,0,0xeeeeee);
    XStoreName(d,w,"DoveOS drag / resize check");XMapWindow(d,w);XFlush(d);usleep(400000);
    unsigned resized=0,moved=0;
    for(int i=0;i<8;i++){
        Window f=frame(d,w);XWindowAttributes a,b;XGetWindowAttributes(d,f,&a);
        motion(d,a.x+a.width-1,a.y+a.height-1);button(d,1);
        int dx=i%2?-32:32,dy=i%2?-16:16;
        for(int k=1;k<=8;k++)motion(d,a.x+a.width-1+dx*k/8,a.y+a.height-1+dy*k/8);
        button(d,0);usleep(100000);XGetWindowAttributes(d,f,&b);
        if(a.width!=b.width||a.height!=b.height)resized++;
        a=b;motion(d,a.x+90,a.y+12);button(d,1);
        for(int k=1;k<=8;k++)motion(d,a.x+90+dx*k/8,a.y+12+dy*k/8);
        button(d,0);usleep(100000);XGetWindowAttributes(d,f,&b);
        if(a.x!=b.x||a.y!=b.y)moved++;
    }
    XDestroyWindow(d,w);XSync(d,False);XCloseDisplay(d);
    printf("DRAG_RESIZE_RESULT resized=%u/8 moved=%u/8\n",resized,moved);
    return resized==8&&moved==8?0:1;
}
