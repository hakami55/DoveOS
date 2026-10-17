/* Small click-through label, owned by the existing tray process. */
#include <X11/extensions/shape.h>
static Window cpu_watermark;
static XftDraw *cpu_watermark_draw;
static XftColor cpu_ink, cpu_shadow;
static int cpu_visible, cpu_width, cpu_height, cpu_x=-1, cpu_y=-1;
static void cpu_watermark_paint(void){
 if(!cpu_visible)return;
 const FcChar8 *label=(const FcChar8*)"CPU Mode";
 XftDrawStringUtf8(cpu_watermark_draw,&cpu_shadow,text_font,1,text_font->ascent+1,label,8);
 XftDrawStringUtf8(cpu_watermark_draw,&cpu_ink,text_font,0,text_font->ascent,label,8);
}
static void cpu_watermark_init(void){
 int screen=DefaultScreen(display),event_base,error_base;
 if(!XShapeQueryExtension(display,&event_base,&error_base))return;
 int major,minor;
 if(!XShapeQueryVersion(display,&major,&minor)||major<1||(major==1&&minor<1))return;
 XGlyphInfo size;
 XftTextExtentsUtf8(display,text_font,(const FcChar8*)"CPU Mode",8,&size);
 cpu_width=size.xOff+2;cpu_height=text_font->ascent+text_font->descent+2;
 XSetWindowAttributes a={0};a.override_redirect=True;a.background_pixel=white;
 cpu_watermark=XCreateWindow(display,DefaultRootWindow(display),0,0,(unsigned)cpu_width,(unsigned)cpu_height,0,CopyFromParent,InputOutput,CopyFromParent,CWOverrideRedirect|CWBackPixel,&a);
 XStoreName(display,cpu_watermark,"DoveOS CPU Mode");
 XWMHints hints={0};hints.flags=InputHint;hints.input=False;XSetWMHints(display,cpu_watermark,&hints);
 XSelectInput(display,cpu_watermark,ExposureMask);
 XShapeCombineRectangles(display,cpu_watermark,ShapeInput,0,0,NULL,0,ShapeSet,Unsorted);
 Pixmap mask=XCreatePixmap(display,cpu_watermark,(unsigned)cpu_width,(unsigned)cpu_height,1);
 GC mask_gc=XCreateGC(display,mask,0,NULL);XSetForeground(display,mask_gc,0);
 XFillRectangle(display,mask,mask_gc,0,0,(unsigned)cpu_width,(unsigned)cpu_height);
 XftDraw *mask_draw=XftDrawCreateBitmap(display,mask);
 XftColor bit={1,{65535,65535,65535,65535}};
 XftDrawStringUtf8(mask_draw,&bit,text_font,1,text_font->ascent+1,(const FcChar8*)"CPU Mode",8);
 XftDrawStringUtf8(mask_draw,&bit,text_font,0,text_font->ascent,(const FcChar8*)"CPU Mode",8);
 XShapeCombineMask(display,cpu_watermark,ShapeBounding,0,0,mask,ShapeSet);
 XftDrawDestroy(mask_draw);XFreeGC(display,mask_gc);XFreePixmap(display,mask);
 cpu_watermark_draw=XftDrawCreate(display,cpu_watermark,DefaultVisual(display,screen),DefaultColormap(display,screen));
 XftColorAllocName(display,DefaultVisual(display,screen),DefaultColormap(display,screen),"#ffffff",&cpu_ink);
 XftColorAllocName(display,DefaultVisual(display,screen),DefaultColormap(display,screen),"#243447",&cpu_shadow);
}
static void cpu_watermark_update(void){
 if(!cpu_watermark)return;
 char status[160];const char *status_path=getenv("DOVE_RENDER_STATUS_FILE");read_line(status_path?status_path:"/tmp/doveos-render-status",status,sizeof status);
 int show=!strncmp(status,"CPU",3)&&access("/tmp/doveos-direct-display",F_OK)!=0;
 if(!show){if(cpu_visible)XUnmapWindow(display,cpu_watermark);cpu_visible=0;return;}
 Window child;int tray_x,tray_y;
 XTranslateCoordinates(display,window,DefaultRootWindow(display),0,0,&tray_x,&tray_y,&child);
 XWindowAttributes root;XGetWindowAttributes(display,DefaultRootWindow(display),&root);
 int x=root.width-cpu_width-12,y=tray_y-cpu_height-10;
 if(x<0)x=0;
 if(y<0)y=root.height-cpu_height-40;
 if(x!=cpu_x||y!=cpu_y){XMoveWindow(display,cpu_watermark,x,y);cpu_x=x;cpu_y=y;}
 if(!cpu_visible){cpu_visible=1;XMapRaised(display,cpu_watermark);cpu_watermark_paint();}
}
static void cpu_watermark_destroy(void){
 if(!cpu_watermark)return;
 XftDrawDestroy(cpu_watermark_draw);XDestroyWindow(display,cpu_watermark);
 XftColorFree(display,DefaultVisual(display,DefaultScreen(display)),DefaultColormap(display,DefaultScreen(display)),&cpu_ink);
 XftColorFree(display,DefaultVisual(display,DefaultScreen(display)),DefaultColormap(display,DefaultScreen(display)),&cpu_shadow);
}
