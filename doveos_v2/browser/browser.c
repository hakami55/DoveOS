#define _DEFAULT_SOURCE
#include "doveos.h"
#include <gtk/gtksocket.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
extern int XTestFakeKeyEvent(Display*,unsigned int,int,unsigned long);
static GtkWidget *window,*socket_widget,*address,*status;
static GPid engine;
static gboolean closing;
static Display *display;
static Window plug;
static gchar *current_uri,*home;
static gchar *property(const char *name){
 Atom type;int format;unsigned long count,left;unsigned char *data=NULL;
 if(!plug)return NULL;
 gdk_error_trap_push();
 int result=XGetWindowProperty(display,plug,XInternAtom(display,name,False),0,8192,False,AnyPropertyType,&type,&format,&count,&left,&data);
 XSync(display,False);int error=gdk_error_trap_pop();
 gchar *text=!error&&result==Success&&data&&format==8?g_strndup((char*)data,count):NULL;
 if(data)XFree(data);
 return text;
}
static void engine_exit(GPid pid,gint result,gpointer unused){
 (void)unused;g_spawn_close_pid(pid);engine=0;plug=0;
 if(!closing)gtk_label_set_text(GTK_LABEL(status),result?"Browser stopped. Close other apps, then press Go to reopen.":"Browser closed. Press Go to reopen.");
}
static void start_engine(const char *uri){
 if(engine)return;
 gchar *id=g_strdup_printf("%lu",(unsigned long)gtk_socket_get_id(GTK_SOCKET(socket_widget)));
 gchar *argv[]={"/usr/sbin/chroot","/proc/1/root","/bin/sh","/media/usb0/DoveOS-v2/ModernBrowser/engine.sh",id,(gchar*)uri,NULL};
 GError *error=NULL;
 if(g_spawn_async(NULL,argv,NULL,G_SPAWN_DO_NOT_REAP_CHILD,NULL,NULL,&engine,&error)){
  g_child_watch_add(engine,engine_exit,NULL);gtk_label_set_text(GTK_LABEL(status),"Starting browser...");
 }else{
  GtkWidget *dialog=dove_messagebox(GTK_WINDOW(window),error?error->message:"Cannot start browser.",GTK_MESSAGE_ERROR,GTK_BUTTONS_OK);
  gtk_dialog_run(GTK_DIALOG(dialog));gtk_widget_destroy(dialog);g_clear_error(&error);engine=0;
 }
 g_free(id);
}
static void go(GtkWidget *widget,gpointer unused){
 (void)widget;(void)unused;gchar *text=g_strdup(gtk_entry_get_text(GTK_ENTRY(address)));g_strstrip(text);
 if(!*text){g_free(text);return;}
 gchar *uri=strstr(text,"://")||g_str_has_prefix(text,"about:")?g_strdup(text):g_strconcat("https://",text,NULL);
 if(plug){
  XChangeProperty(display,plug,XInternAtom(display,"_SURF_GO",False),XInternAtom(display,"UTF8_STRING",False),8,PropModeReplace,(unsigned char*)uri,strlen(uri)+1);
  XFlush(display);gtk_widget_grab_focus(socket_widget);gtk_label_set_text(GTK_LABEL(status),"Loading...");
 }else start_engine(uri);
 g_free(text);g_free(uri);
}
static void send_key(GtkWidget *widget,gpointer data){
 (void)widget;if(!plug)return;
 KeySym sym=(KeySym)GPOINTER_TO_UINT(data);unsigned ctrl=XKeysymToKeycode(display,XK_Control_L),key=XKeysymToKeycode(display,sym);
 gtk_widget_grab_focus(socket_widget);XSetInputFocus(display,plug,RevertToParent,CurrentTime);
 XTestFakeKeyEvent(display,ctrl,True,0);XTestFakeKeyEvent(display,key,True,0);XTestFakeKeyEvent(display,key,False,0);XTestFakeKeyEvent(display,ctrl,False,0);XFlush(display);
}
static void go_home(GtkWidget *widget,gpointer unused){(void)widget;(void)unused;gtk_entry_set_text(GTK_ENTRY(address),home);go(NULL,NULL);}
static gboolean poll_engine(gpointer unused){
 (void)unused;if(closing)return FALSE;
 gchar *uri=property("_SURF_URI");
 if(uri&&*uri&&g_strcmp0(uri,current_uri)){
  g_free(current_uri);current_uri=g_strdup(uri);
  if(!gtk_widget_has_focus(address))gtk_entry_set_text(GTK_ENTRY(address),uri);
 }
 g_free(uri);
 gchar *title=property("_NET_WM_NAME");if(!title)title=property("WM_NAME");
 if(title&&*title){
  gchar *page=strrchr(title,'|');if(page)page++;else page=title;
  gchar *label=g_strdup_printf("%s - DoveBrowser",g_strstrip(page));gtk_window_set_title(GTK_WINDOW(window),label);g_free(label);
  gtk_label_set_text(GTK_LABEL(status),title[0]=='['?"Loading...":"Ready");
 }
 g_free(title);return TRUE;
}
static void added(GtkSocket *socket,gpointer unused){(void)unused;GdkWindow *w=gtk_socket_get_plug_window(socket);plug=w?GDK_WINDOW_XID(w):0;gtk_label_set_text(GTK_LABEL(status),"Loading...");}
static gboolean removed(GtkSocket *socket,gpointer unused){(void)socket;(void)unused;plug=0;return TRUE;}
static void destroyed(GtkWidget *widget,gpointer unused){(void)widget;(void)unused;closing=TRUE;if(engine)kill(engine,SIGTERM);gtk_main_quit();}
static gboolean keys(GtkWidget *widget,GdkEventKey *event,gpointer unused){
 (void)widget;(void)unused;
 if((event->state&GDK_CONTROL_MASK)&&(event->keyval==GDK_l||event->keyval==GDK_L)){gtk_widget_grab_focus(address);gtk_editable_select_region(GTK_EDITABLE(address),0,-1);return TRUE;}
 return FALSE;
}
int main(int argc,char **argv){
 gtk_init(&argc,&argv);home=g_strdup("https://www.wikipedia.org/");display=GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
 window=dove_form("DoveBrowser (Preview)",1150,650);gtk_window_set_wmclass(GTK_WINDOW(window),"DoveBrowser","DoveBrowser");
 gtk_window_set_icon_from_file(GTK_WINDOW(window),"/media/usb0/DoveOS-v2/ClassicApps/icons/browser-24.png",NULL);
 GtkWidget *box=dove_layout(FALSE);gtk_box_set_spacing(GTK_BOX(box),2);gtk_container_add(GTK_CONTAINER(window),box);
 GtkWidget *bar=gtk_hbox_new(FALSE,3);dove_pack(box,bar,FALSE);
 const char *names[]={"Back","Forward","Reload"};guint values[]={XK_h,XK_l,XK_r};
 for(int i=0;i<3;i++){GtkWidget *b=dove_button(names[i]);gtk_box_pack_start(GTK_BOX(bar),b,FALSE,FALSE,0);g_signal_connect(b,"clicked",G_CALLBACK(send_key),GUINT_TO_POINTER(values[i]));}
 GtkWidget *b=dove_button("Home");gtk_box_pack_start(GTK_BOX(bar),b,FALSE,FALSE,0);g_signal_connect(b,"clicked",G_CALLBACK(go_home),NULL);
 address=gtk_entry_new();gtk_box_pack_start(GTK_BOX(bar),address,TRUE,TRUE,0);g_signal_connect(address,"activate",G_CALLBACK(go),NULL);
 b=dove_button("Go");gtk_box_pack_start(GTK_BOX(bar),b,FALSE,FALSE,0);g_signal_connect(b,"clicked",G_CALLBACK(go),NULL);
 socket_widget=gtk_socket_new();gtk_widget_set_size_request(socket_widget,400,300);dove_pack(box,socket_widget,TRUE);
 status=dove_label("Starting browser...");dove_pack(box,status,FALSE);
 g_signal_connect(socket_widget,"plug-added",G_CALLBACK(added),NULL);g_signal_connect(socket_widget,"plug-removed",G_CALLBACK(removed),NULL);
 g_signal_connect(window,"destroy",G_CALLBACK(destroyed),NULL);g_signal_connect(window,"key-press-event",G_CALLBACK(keys),NULL);
 gtk_entry_set_text(GTK_ENTRY(address),argc>1?argv[1]:home);gtk_widget_show_all(window);go(NULL,NULL);g_timeout_add(500,poll_engine,NULL);
 gtk_main();g_free(current_uri);g_free(home);return 0;
}
