#ifndef DOVE_TERMINAL_H
#define DOVE_TERMINAL_H
/* Optional GTK2/X11 terminal add-on. Uses the installed xterm emulator and its
 * real PTY; no emulation, output polling or separate library is introduced. */
#include <gtk/gtksocket.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <signal.h>
typedef void (*DoveTerminalExit)(GtkWidget*,gint,gpointer);
typedef struct {GtkWidget *widget;GPid pid;gchar *cwd,**command,*error,*font;gboolean destroyed;guint ready,attempts;DoveTerminalExit exited;gpointer data;} DoveTerminal;
static inline void dove_terminal_free(gpointer data){DoveTerminal *t=data;g_free(t->cwd);g_strfreev(t->command);g_free(t->error);g_free(t->font);g_free(t);}
static inline void dove_terminal_destroy(GtkWidget *w,gpointer data){(void)w;DoveTerminal *t=data;t->destroyed=TRUE;if(t->ready){g_source_remove(t->ready);t->ready=0;}if(t->pid)kill(t->pid,SIGTERM);}
static inline void dove_terminal_child(GPid pid,gint status,gpointer data){GtkWidget *w=data;DoveTerminal *t=g_object_get_data(G_OBJECT(w),"dove-terminal");t->pid=0;g_spawn_close_pid(pid);if(!t->destroyed&&t->exited)t->exited(w,status,t->data);g_object_unref(w);}
static inline Window dove_terminal_target(GtkWidget *widget){
 if(!gtk_widget_get_realized(widget)){return None;}
 Display *display=GDK_DISPLAY_XDISPLAY(gtk_widget_get_display(widget));Window socket=gtk_socket_get_id(GTK_SOCKET(widget)),root,parent,*children=NULL,best=None;unsigned int count=0;
 gdk_error_trap_push();
 if(XQueryTree(display,socket,&root,&parent,&children,&count)){
  Window shell=count?children[0]:None;if(children)XFree(children);children=NULL;
  XWindowAttributes a;
  if(shell&&XGetWindowAttributes(display,shell,&a)&&a.map_state==IsViewable){
   best=shell;
   if(XQueryTree(display,shell,&root,&parent,&children,&count)){unsigned long largest=0;for(unsigned int i=0;i<count;i++){if(XGetWindowAttributes(display,children[i],&a)&&a.map_state==IsViewable){unsigned long area=(unsigned long)a.width*a.height;if(area>largest){largest=area;best=children[i];}}}if(children)XFree(children);}
  }
 }
 XSync(display,False);if(gdk_error_trap_pop())return None;return best;
}
static inline gboolean dove_terminal_key(GtkWidget *widget,KeySym symbol,unsigned int modifiers){
 Window target=dove_terminal_target(widget);if(!target)return FALSE;Display *display=GDK_DISPLAY_XDISPLAY(gtk_widget_get_display(widget));gdk_error_trap_push();XKeyEvent event={0};event.display=display;event.window=target;event.root=DefaultRootWindow(display);event.same_screen=True;event.keycode=XKeysymToKeycode(display,symbol);event.state=modifiers;event.type=KeyPress;XSendEvent(display,target,False,KeyPressMask,(XEvent*)&event);event.type=KeyRelease;XSendEvent(display,target,False,KeyReleaseMask,(XEvent*)&event);XSync(display,False);return gdk_error_trap_pop()==0;
}
static inline void dove_terminal_fit(GtkWidget *widget){
 if(!gtk_widget_get_realized(widget)){return;}Display *display=GDK_DISPLAY_XDISPLAY(gtk_widget_get_display(widget));Window socket=gtk_socket_get_id(GTK_SOCKET(widget)),root,parent,*children=NULL;unsigned int count=0;GtkAllocation a;gtk_widget_get_allocation(widget,&a);gdk_error_trap_push();if(XQueryTree(display,socket,&root,&parent,&children,&count)){if(count)XResizeWindow(display,children[0],MAX(a.width,1),MAX(a.height,1));if(children)XFree(children);}XSync(display,False);gdk_error_trap_pop();
}
static inline void dove_terminal_allocated(GtkWidget *widget,GtkAllocation *a,gpointer data){(void)a;(void)data;dove_terminal_fit(widget);}
static inline void dove_terminal_focus(GtkWidget *widget){Window target=dove_terminal_target(widget);if(target){gtk_widget_grab_focus(widget);Display *display=GDK_DISPLAY_XDISPLAY(gtk_widget_get_display(widget));gdk_error_trap_push();XSetInputFocus(display,target,RevertToParent,CurrentTime);XSync(display,False);gdk_error_trap_pop();}}
static inline gboolean dove_terminal_focused(GtkWidget *w,GdkEventFocus *event,gpointer data){(void)event;(void)data;dove_terminal_focus(w);return FALSE;}
static inline gboolean dove_terminal_removed(GtkSocket *socket,gpointer data){(void)socket;(void)data;return TRUE;}
static inline void dove_terminal_added(GtkSocket *socket,gpointer data){(void)data;dove_terminal_focus(GTK_WIDGET(socket));}
static inline gboolean dove_terminal_ready(gpointer data){GtkWidget *widget=data;DoveTerminal *t=g_object_get_data(G_OBJECT(widget),"dove-terminal");if(dove_terminal_target(widget)){dove_terminal_fit(widget);dove_terminal_focus(widget);t->ready=0;return FALSE;}if(++t->attempts>=60||t->destroyed){t->ready=0;return FALSE;}return TRUE;}
static inline void dove_terminal_realize(GtkWidget *widget,gpointer data){
 DoveTerminal *t=data;gchar id[32];g_snprintf(id,sizeof id,"%lu",(unsigned long)gtk_socket_get_id(GTK_SOCKET(widget)));
 const char *translations="*VT100.translations: #override\\nCtrl Shift <Key>C: copy-selection(CLIPBOARD)\\nCtrl Shift <Key>V: insert-selection(CLIPBOARD)\\nCtrl Alt <Key>F1: set-vt-font(d)\\nCtrl Alt <Key>F2: set-vt-font(2)\\nCtrl Alt <Key>F3: set-vt-font(4)\\nCtrl Alt <Key>F4: set-vt-font(5)";
 GPtrArray *args=g_ptr_array_new();const char *base[]={"/usr/bin/xterm","-into",id,"-name","DoveTerminal","-T","Terminal","-bg","black","-fg","#c0c0c0","-cr","white","-fn","6x13","-fa",t->font?t->font:"Monospace","-fs",t->font?"12":"9","-b","3","-bw","0","-sl","1000","+sb","-ut","-xrm","*VT100.allowSendEvents: true","-xrm","*VT100.allowWindowOps: false","-xrm","*VT100.allowFontOps: false","-xrm",t->font?"*VT100.faceSize2: 9":"*VT100.faceSize2: 8","-xrm",t->font?"*VT100.faceSize4: 12":"*VT100.faceSize4: 10","-xrm",t->font?"*VT100.faceSize5: 18":"*VT100.faceSize5: 12","-xrm",translations,"-e",NULL};
 for(int i=0;base[i];i++){g_ptr_array_add(args,(gpointer)base[i]);}for(int i=0;t->command[i];i++){g_ptr_array_add(args,t->command[i]);}g_ptr_array_add(args,NULL);GError *error=NULL;
 if(g_spawn_async(t->cwd,(gchar**)args->pdata,NULL,G_SPAWN_DO_NOT_REAP_CHILD,NULL,NULL,&t->pid,&error)){g_child_watch_add(t->pid,dove_terminal_child,g_object_ref(widget));t->ready=g_timeout_add_full(G_PRIORITY_DEFAULT,50,dove_terminal_ready,g_object_ref(widget),g_object_unref);}else{t->error=g_strdup(error->message);g_error_free(error);if(t->exited)t->exited(widget,-1,t->data);}g_ptr_array_free(args,TRUE);
}
static inline GtkWidget *dove_terminal_new_with_font(const char *directory,const char *const *command,const char *font){
 GtkWidget *widget=gtk_socket_new();DoveTerminal *t=g_new0(DoveTerminal,1);t->widget=widget;t->font=g_strdup(font);t->cwd=g_strdup(directory?directory:g_get_home_dir());const char *shell[]={"/bin/sh","-i",NULL};t->command=g_strdupv((gchar**)(command?command:shell));g_object_set_data_full(G_OBJECT(widget),"dove-terminal",t,dove_terminal_free);gtk_widget_set_size_request(widget,300,180);g_signal_connect(widget,"realize",G_CALLBACK(dove_terminal_realize),t);g_signal_connect(widget,"size-allocate",G_CALLBACK(dove_terminal_allocated),NULL);g_signal_connect(widget,"destroy",G_CALLBACK(dove_terminal_destroy),t);g_signal_connect(widget,"plug-added",G_CALLBACK(dove_terminal_added),t);g_signal_connect(widget,"plug-removed",G_CALLBACK(dove_terminal_removed),NULL);g_signal_connect(widget,"focus-in-event",G_CALLBACK(dove_terminal_focused),NULL);return widget;
}
/* Optional installed Fontconfig family/pattern; NULL preserves the SDK default. */
static inline GtkWidget *dove_terminal_new(const char *directory,const char *const *command){return dove_terminal_new_with_font(directory,command,NULL);}
static inline void dove_terminal_on_exit(GtkWidget *widget,DoveTerminalExit callback,gpointer data){DoveTerminal *t=g_object_get_data(G_OBJECT(widget),"dove-terminal");t->exited=callback;t->data=data;}
static inline const char *dove_terminal_error(GtkWidget *widget){DoveTerminal *t=g_object_get_data(G_OBJECT(widget),"dove-terminal");return t->error;}
#endif
