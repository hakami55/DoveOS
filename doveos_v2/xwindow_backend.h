#ifndef DOVEOS_XWINDOW_BACKEND_H
#define DOVEOS_XWINDOW_BACKEND_H
#define DOVE_X_W 1024
#define DOVE_X_H 576
#include <X11/Xatom.h>
#include "moonlight_icon.h"
typedef struct _XftDraw XftDraw;
typedef struct _XftFont XftFont;
typedef struct {unsigned short red,green,blue,alpha;} DoveXRenderColor;
typedef struct {unsigned long pixel;DoveXRenderColor color;} DoveXftColor;
extern XftDraw *XftDrawCreate(Display *,Drawable,Visual *,Colormap);
extern void XftDrawDestroy(XftDraw *);
extern XftFont *XftFontOpenName(Display *,int,const char *);
extern void XftFontClose(Display *,XftFont *);
extern void XftDrawStringUtf8(XftDraw *,const DoveXftColor *,XftFont *,int,int,const unsigned char *,int);
static Display *dove_display;
static Window dove_window;
static GC dove_gc;
static XImage *dove_image;
static XftDraw *dove_xft_draw;
static XftFont *dove_fonts[4];
static Atom dove_delete;
static int dove_x_open(struct app *a){
    dove_display=XOpenDisplay(NULL);
    if(!dove_display)return -1;
    int screen=DefaultScreen(dove_display);
    dove_window=XCreateSimpleWindow(dove_display,RootWindow(dove_display,screen),
        100,55,DOVE_X_W,DOVE_X_H,1,BlackPixel(dove_display,screen),WhitePixel(dove_display,screen));
    XSizeHints hints={0};
    hints.flags=PMinSize|PMaxSize;
    hints.min_width=hints.max_width=DOVE_X_W;
    hints.min_height=hints.max_height=DOVE_X_H;
    XSetWMNormalHints(dove_display,dove_window,&hints);
    XStoreName(dove_display,dove_window,a->page==MOONLIGHT?"Moonlight":"Settings");
    if(a->page==MOONLIGHT){
        unsigned long icon[2+48*48];icon[0]=48;icon[1]=48;
        for(int i=0;i<48*48;i++){
            const unsigned char *p=moonlight_icon_rgb+i*3;
            icon[i+2]=0xff000000ul|((unsigned long)p[0]<<16)|((unsigned long)p[1]<<8)|p[2];
        }
        Atom property=XInternAtom(dove_display,"_NET_WM_ICON",False);
        XChangeProperty(dove_display,dove_window,property,XA_CARDINAL,32,PropModeReplace,
                        (unsigned char *)icon,2+48*48);
    }
    XSelectInput(dove_display,dove_window,ExposureMask|KeyPressMask|KeyReleaseMask|ButtonPressMask|StructureNotifyMask);
    dove_delete=XInternAtom(dove_display,"WM_DELETE_WINDOW",False);
    XSetWMProtocols(dove_display,dove_window,&dove_delete,1);
    dove_gc=XCreateGC(dove_display,dove_window,0,NULL);
    dove_xft_draw=XftDrawCreate(dove_display,dove_window,DefaultVisual(dove_display,screen),DefaultColormap(dove_display,screen));
    dove_fonts[1]=XftFontOpenName(dove_display,screen,"Roboto-12");
    dove_fonts[2]=XftFontOpenName(dove_display,screen,"Roboto-22");
    dove_fonts[3]=XftFontOpenName(dove_display,screen,"Roboto-32");
    char *data=calloc((size_t)DOVE_X_W*DOVE_X_H,4);
    a->pixels=calloc((size_t)WIDTH*HEIGHT,4);
    if(!data||!a->pixels)return -1;
    a->stride=WIDTH;a->fb_size=(size_t)WIDTH*HEIGHT*4;
    dove_image=XCreateImage(dove_display,DefaultVisual(dove_display,screen),
        DefaultDepth(dove_display,screen),ZPixmap,0,data,DOVE_X_W,DOVE_X_H,32,0);
    if(!dove_image)return -1;
    XMapRaised(dove_display,dove_window);
    XFlush(dove_display);
    return 0;
}
static void dove_x_present(struct app *a){
    if(!dove_display||!dove_image)return;
    uint32_t *dest=(uint32_t *)dove_image->data;
    for(int y=0;y<DOVE_X_H;y++){
        const uint32_t *source=a->pixels+(size_t)(y*HEIGHT/DOVE_X_H)*WIDTH;
        for(int x=0;x<DOVE_X_W;x++)dest[(size_t)y*DOVE_X_W+x]=source[x*WIDTH/DOVE_X_W];
    }
    XPutImage(dove_display,dove_window,dove_gc,dove_image,0,0,0,0,DOVE_X_W,DOVE_X_H);
    if(dove_xft_draw){
        for(int i=0;i<dove_text_count;i++){
            const struct dove_text_command *cmd=&dove_text[i];
            int scale=cmd->scale;if(scale<1||scale>3)scale=1;
            if(!dove_fonts[scale])continue;
            DoveXftColor color={0};
            color.pixel=cmd->color;
            color.color.red=(unsigned short)(((cmd->color>>16)&255)*257);
            color.color.green=(unsigned short)(((cmd->color>>8)&255)*257);
            color.color.blue=(unsigned short)((cmd->color&255)*257);
            color.color.alpha=65535;
            int x=cmd->x*DOVE_X_W/WIDTH;
            int y=cmd->y*DOVE_X_H/HEIGHT+(scale==1?12:scale==2?22:32);
            XftDrawStringUtf8(dove_xft_draw,&color,dove_fonts[scale],x,y,
                              (const unsigned char *)cmd->value,(int)strlen(cmd->value));
        }
    }
    XFlush(dove_display);
}
static void dove_x_close(struct app *a){
    if(dove_display){
        for(int i=1;i<=3;i++)if(dove_fonts[i])XftFontClose(dove_display,dove_fonts[i]);
        if(dove_xft_draw)XftDrawDestroy(dove_xft_draw);
    }
    if(dove_image){XDestroyImage(dove_image);dove_image=NULL;}
    free(a->pixels);a->pixels=NULL;
    if(dove_display){XFreeGC(dove_display,dove_gc);XDestroyWindow(dove_display,dove_window);XCloseDisplay(dove_display);dove_display=NULL;}
}
#endif
