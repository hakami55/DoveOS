#ifndef DOVE_GPU_CANVAS_H
#define DOVE_GPU_CANVAS_H
/* Optional GTK2 canvas. Compile with native Mali and libdove-gpu-window.
 * GTK widgets/layout remain GTK; only this owned opaque child uses GPU images.
 * Event-driven rendering: no idle animation timer and no CPU pixel readback. */
#include <gdk/gdkx.h>
#include <EGL/egl.h>
#include "dove_gpu_window.h"
typedef struct {
 GtkWidget *area;EGLDisplay display;EGLContext context;EGLSurface surface;
 DoveGpuWindow *window;guint idle;gboolean failed;double x,y;unsigned buttons;int allocation_width,allocation_height;
} DoveGpuCanvas;
static void dove_gpu_canvas_close(DoveGpuCanvas*c){
 if(c->idle){g_source_remove(c->idle);c->idle=0;}
 if(c->context&&eglMakeCurrent(c->display,c->surface,c->surface,c->context)){
  if(c->window){dove_gpu_window_flush(c->window);dove_gpu_window_detach(c->window);c->window=NULL;}
  eglMakeCurrent(c->display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
 }
 if(c->context)eglDestroyContext(c->display,c->context);
 if(c->surface)eglDestroySurface(c->display,c->surface);
 if(c->display)eglTerminate(c->display);
 c->context=EGL_NO_CONTEXT;c->surface=EGL_NO_SURFACE;c->display=EGL_NO_DISPLAY;
}
static void dove_gpu_canvas_rect(int x,int y,int w,int h,float r,float g,float b){
 glScissor(x,y,w,h);glClearColor(r,g,b,1);glClear(GL_COLOR_BUFFER_BIT);
}
static gboolean dove_gpu_canvas_render(gpointer data){
 DoveGpuCanvas*c=data;c->idle=0;
 if(!GTK_WIDGET_REALIZED(c->area)||!GTK_WIDGET_MAPPED(c->area)||c->failed)return FALSE;
 int width=c->area->allocation.width,height=c->area->allocation.height;
 if(width<32||height<32)return FALSE;
 if(width!=c->allocation_width||height!=c->allocation_height){
  /* GTK's allocation can precede its X window resize. Synchronize only on a
   * size change, never on ordinary pointer frames. */
  XSync(GDK_WINDOW_XDISPLAY(c->area->window),False);
  c->allocation_width=width;c->allocation_height=height;
 }
 if(!c->context){
  EGLConfig cfg;EGLint n=0;
  EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
  EGLint pa[]={EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE},co[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
  c->display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if(!c->display||!eglInitialize(c->display,NULL,NULL)||!eglChooseConfig(c->display,ca,&cfg,1,&n)||!n)goto failed;
  c->surface=eglCreatePbufferSurface(c->display,cfg,pa);c->context=eglCreateContext(c->display,cfg,EGL_NO_CONTEXT,co);
  if(!c->surface||!c->context||!eglMakeCurrent(c->display,c->surface,c->surface,c->context))goto failed;
  /* 7.5 MiB reserved once, bounded to the canvas's maximum supported height. */
  c->window=dove_gpu_window_attach_capacity(GDK_WINDOW_XID(c->area->window),1280,512);
  if(!c->window)goto failed;
 }
 if(!eglMakeCurrent(c->display,c->surface,c->surface,c->context))goto failed;
 int result=dove_gpu_window_begin(c->window,&width,&height);
 if(result==0)return FALSE;if(result<0)goto failed;
 glClearColor(0,.38f,.4f,1);glClear(GL_COLOR_BUFFER_BIT);glEnable(GL_SCISSOR_TEST);
 for(int x=0;x<width;x+=32)dove_gpu_canvas_rect(x,0,1,height,0,.45f,.47f);
 for(int y=0;y<height;y+=32)dove_gpu_canvas_rect(0,y,width,1,0,.45f,.47f);
 int x=(int)c->x,y=height-1-(int)c->y;
 if(x<0)x=0;if(x>=width)x=width-1;if(y<0)y=0;if(y>=height)y=height-1;
 dove_gpu_canvas_rect(x,0,1,height,.6f,.85f,.85f);dove_gpu_canvas_rect(0,y,width,1,.6f,.85f,.85f);
 dove_gpu_canvas_rect(x>6?x-6:0,y>6?y-6:0,13,13,c->buttons?1:.9f,c->buttons?.7f:.9f,c->buttons?0:.9f);
 glDisable(GL_SCISSOR_TEST);
 if(!dove_gpu_window_present(c->window))goto failed;
 if(g_getenv("DOVE_GPU_CANVAS_TRACE")&&!dove_gpu_window_flush(c->window))goto failed;
 if(width!=c->area->allocation.width||height!=c->area->allocation.height)
  c->idle=g_timeout_add(16,dove_gpu_canvas_render,c);
 if(g_getenv("DOVE_GPU_CANVAS_TRACE"))g_printerr("GPU_CANVAS size=%dx%d gtk=%dx%d marker=%d,%d\n",width,height,c->area->allocation.width,c->area->allocation.height,x,height-1-y);
 return FALSE;
failed:
 g_printerr("DOVE_GPU_CANVAS_STOP\n");c->failed=TRUE;dove_gpu_canvas_close(c);return FALSE;
}
static void dove_gpu_canvas_queue(DoveGpuCanvas*c){if(!c->idle&&!c->failed)c->idle=g_idle_add(dove_gpu_canvas_render,c);}
static gboolean dove_gpu_canvas_expose(GtkWidget*w,GdkEventExpose*e,gpointer data){(void)w;(void)e;dove_gpu_canvas_queue(data);return FALSE;}
static gboolean dove_gpu_canvas_configure(GtkWidget*w,GdkEventConfigure*e,gpointer data){(void)w;(void)e;dove_gpu_canvas_queue(data);return FALSE;}
static void dove_gpu_canvas_allocate(GtkWidget*w,GtkAllocation*a,gpointer data){(void)w;(void)a;dove_gpu_canvas_queue(data);}
static void dove_gpu_canvas_unrealize(GtkWidget*w,gpointer data){(void)w;dove_gpu_canvas_close(data);}
static void dove_gpu_canvas_free(gpointer data){DoveGpuCanvas*c=data;dove_gpu_canvas_close(c);g_free(c);}
static DoveGpuCanvas*dove_gpu_canvas_new(void){
 DoveGpuCanvas*c=g_new0(DoveGpuCanvas,1);c->area=gtk_drawing_area_new();
 g_object_set_data_full(G_OBJECT(c->area),"dove-gpu-canvas",c,dove_gpu_canvas_free);
 g_signal_connect(c->area,"expose-event",G_CALLBACK(dove_gpu_canvas_expose),c);
 g_signal_connect(c->area,"configure-event",G_CALLBACK(dove_gpu_canvas_configure),c);
 g_signal_connect(c->area,"size-allocate",G_CALLBACK(dove_gpu_canvas_allocate),c);
 g_signal_connect(c->area,"unrealize",G_CALLBACK(dove_gpu_canvas_unrealize),c);
 return c;
}
static void dove_gpu_canvas_pointer(DoveGpuCanvas*c,double x,double y,unsigned buttons){c->x=x;c->y=y;c->buttons=buttons;dove_gpu_canvas_queue(c);}
#endif
