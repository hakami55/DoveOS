#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct _XftDraw XftDraw;
typedef struct _XftFont XftFont;
typedef struct {unsigned short red,green,blue,alpha;} RenderColor;
typedef struct {unsigned long pixel;RenderColor color;} XftColor;
extern XftDraw *XftDrawCreate(Display *,Drawable,Visual *,Colormap);
extern void XftDrawDestroy(XftDraw *);
extern XftFont *XftFontOpenName(Display *,int,const char *);
extern void XftDrawStringUtf8(XftDraw *,const XftColor *,XftFont *,int,int,const unsigned char *,int);
static Display *d;static Window w;static GC gc;static XftDraw *draw;
static XftFont *font,*title;static XftColor ink,white,muted;
static unsigned long pixel(const char *s){XColor c,e;if(!XAllocNamedColor(d,DefaultColormap(d,0),s,&c,&e))return 0;return c.pixel;}
static void paint_color(XftColor *out,const char *s){XColor c,e;memset(out,0,sizeof *out);if(XAllocNamedColor(d,DefaultColormap(d,0),s,&c,&e)){out->pixel=c.pixel;out->color=(RenderColor){c.red,c.green,c.blue,65535};}}
static void box(int x,int y,int width,int height,const char *s){XSetForeground(d,gc,pixel(s));XFillRectangle(d,w,gc,x,y,(unsigned)width,(unsigned)height);}
static void text(int x,int y,const char *s,XftFont *f,XftColor *c){XftDrawStringUtf8(draw,c,f,x,y,(const unsigned char *)s,(int)strlen(s));}
static void render(void){
 box(0,0,820,470,"#f3f6fa");box(0,0,820,76,"#284f94");
 text(28,49,"Wireless DeX",title,&white);
 text(28,120,"Use your Samsung phone as a desktop on the Lap Dock.",font,&ink);
 text(28,167,"1. Choose Start below.",font,&ink);
 text(28,202,"2. On your phone, open DeX > DeX on TV or monitor.",font,&ink);
 text(28,237,"3. Select HP Lap Dock. The phone connects directly.",font,&ink);
 text(28,291,"Return to DoveOS: Ctrl + Alt + Shift + Q",font,&ink);
 text(28,327,"DoveOS also returns after DeX disconnects or waiting times out.",font,&muted);
 text(28,359,"Input compatibility is experimental; no firmware is flashed.",font,&muted);
 box(28,392,244,48,"#284f94");text(52,424,"Start Wireless DeX",font,&white);
 box(294,392,150,48,"#dce4ef");text(325,424,"Cancel",font,&ink);
 XFlush(d);
}
static int launch(void){FILE *f=fopen("/tmp/doveos-v2-action","w");if(!f)return 1;if(fputs("dex\n",f)<0){fclose(f);return 1;}if(fclose(f))return 1;return system("jwm -exit");}
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--check")){puts("DEX_UI_CHECK_OK");return 0;}
 d=XOpenDisplay(NULL);if(!d)return 1;
 w=XCreateSimpleWindow(d,DefaultRootWindow(d),100,90,820,470,0,0,pixel("#f3f6fa"));
 XStoreName(d,w,"Wireless DeX - DoveOS");
 XSizeHints hints={.flags=PMinSize|PMaxSize,.min_width=820,.min_height=470,.max_width=820,.max_height=470};XSetWMNormalHints(d,w,&hints);
 XSelectInput(d,w,ExposureMask|KeyPressMask|ButtonPressMask);Atom close=XInternAtom(d,"WM_DELETE_WINDOW",False);XSetWMProtocols(d,w,&close,1);
 gc=XCreateGC(d,w,0,NULL);draw=XftDrawCreate(d,w,DefaultVisual(d,0),DefaultColormap(d,0));
 font=XftFontOpenName(d,0,"DejaVu Sans-13");title=XftFontOpenName(d,0,"DejaVu Sans-22");if(!font||!title||!draw)return 2;
 paint_color(&ink,"#182b45");paint_color(&white,"#ffffff");paint_color(&muted,"#566780");XMapWindow(d,w);
 int done=0;while(!done){XEvent e;XNextEvent(d,&e);if(e.type==Expose&&!e.xexpose.count)render();
  if(e.type==ClientMessage&&(Atom)e.xclient.data.l[0]==close)done=1;
  if(e.type==KeyPress){KeySym key=XLookupKeysym(&e.xkey,0);if(key==XK_Escape)done=1;else if(key==XK_Return){if(!launch())done=1;}}
  if(e.type==ButtonPress&&e.xbutton.button==1&&e.xbutton.y>=392&&e.xbutton.y<440){if(e.xbutton.x>=28&&e.xbutton.x<272){if(!launch())done=1;}else if(e.xbutton.x>=294&&e.xbutton.x<444)done=1;}
 }
 XftDrawDestroy(draw);XFreeGC(d,gc);XDestroyWindow(d,w);XCloseDisplay(d);return 0;
}
