#define _POSIX_C_SOURCE 200809L
#include "doveos.h"
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
static GtkWidget *state,*connections,*tvcode,*start,*stop,*pair;
static GtkTextBuffer *details;
static GPid child;
static gboolean stopping;
static GtkWidget *line(const char *text){GtkWidget *label=gtk_label_new(text);gtk_misc_set_alignment(GTK_MISC(label),0,0.5);return label;}
static void pack(GtkWidget *box,GtkWidget *widget){gtk_box_pack_start(GTK_BOX(box),widget,FALSE,TRUE,0);}
static void set_code(const char *code){
 gchar *escaped=g_markup_escape_text(code,-1),*markup=g_strdup_printf("<span size=\"18000\" weight=\"bold\">%s</span>",escaped);
 gtk_label_set_markup(GTK_LABEL(tvcode),markup);g_free(markup);g_free(escaped);
}
static void show_status(const char *text){
 gtk_text_buffer_set_text(details,text,-1);gchar **lines=g_strsplit(text,"\n",-1);gboolean has_code=FALSE;const char *playback=NULL;
 for(gchar **p=lines;*p;p++){
  if(g_str_has_prefix(*p,"TV code: ")){set_code(*p+9);has_code=TRUE;}
  else if(g_str_has_prefix(*p,"Requesting TV code"))set_code("Getting code...");
  else if(g_str_has_prefix(*p,"TV code unavailable"))set_code("No code yet");
  char *count=strstr(*p,"Connected phones: ");
  if(count){guint64 n=g_ascii_strtoull(count+18,NULL,10);gchar *label=g_strdup_printf("Connected devices: %"G_GUINT64_FORMAT,n);gtk_label_set_text(GTK_LABEL(connections),label);g_free(label);}
  if(g_str_has_prefix(*p,"Playing"))playback="Playing your YouTube video.";
  else if(g_str_has_prefix(*p,"Paused"))playback="Video paused. Use YouTube to continue.";
  else if(g_str_has_prefix(*p,"Loading video"))playback="Loading your YouTube video...";
 }
 if(!stopping){
  if(strstr(text,"TV code request failed")||strstr(text,"Cannot connect to YouTube")||strstr(text,"YouTube connection error"))gtk_label_set_text(GTK_LABEL(state),"Connection problem. Check the network and try New code.");
  else if(playback)gtk_label_set_text(GTK_LABEL(state),playback);
  else if(has_code)gtk_label_set_text(GTK_LABEL(state),"Enter this code in YouTube to link your device.");
  else gtk_label_set_text(GTK_LABEL(state),"Connecting to YouTube. Please wait...");
 }
 g_strfreev(lines);
}
static gboolean poll_status(gpointer unused){
 (void)unused;if(!child)return TRUE;gchar *text=NULL;
 if(g_file_get_contents("/tmp/doveos-youtube-live/status.txt",&text,NULL,NULL)){show_status(text);g_free(text);}return TRUE;
}
static void request(const char *name){int f=open(name,O_WRONLY|O_CREAT,0600);if(f>=0)close(f);}
static void stop_receiver(GtkWidget *w,gpointer data){
 (void)w;(void)data;if(!child)return;stopping=TRUE;request("/tmp/doveos-youtube-live/stop");gtk_widget_set_sensitive(stop,FALSE);gtk_widget_set_sensitive(pair,FALSE);gtk_label_set_text(GTK_LABEL(state),"Stopping receiver...");
}
static void new_code(GtkWidget *w,gpointer data){(void)w;(void)data;if(!child)return;request("/tmp/doveos-youtube-live/pair");set_code("Getting code...");gtk_label_set_text(GTK_LABEL(state),"Requesting a new YouTube TV code...");}
static void finished(GPid pid,gint result,gpointer data){
 (void)data;g_spawn_close_pid(pid);child=0;stopping=FALSE;gtk_widget_set_sensitive(start,TRUE);gtk_widget_set_sensitive(stop,FALSE);gtk_widget_set_sensitive(pair,FALSE);
 set_code("--- --- --- ---");gtk_label_set_text(GTK_LABEL(connections),"Connected devices: 0");
 gtk_label_set_text(GTK_LABEL(state),!WIFEXITED(result)||WEXITSTATUS(result)?"Unable to start. Check the network, then try again.":"Stopped. Click Start receiver to connect again.");
}
static void start_receiver(GtkWidget *w,gpointer data){
 (void)w;(void)data;if(child)return;stopping=FALSE;
 gchar *argv[]={"/tmp/doveos-youtube-run","--exec","chroot","/proc/1/root","/bin/sh","/media/usb0/DoveOS-v2/YouTubeTrial/receiver.sh","start",NULL};GError *error=NULL;
 if(!g_file_test("/tmp/doveos-youtube-live/receiver.lock",G_FILE_TEST_IS_DIR))unlink("/tmp/doveos-youtube-live/status.txt");
 if(!g_spawn_async(NULL,argv,NULL,G_SPAWN_SEARCH_PATH|G_SPAWN_DO_NOT_REAP_CHILD|G_SPAWN_STDOUT_TO_DEV_NULL|G_SPAWN_STDERR_TO_DEV_NULL,NULL,NULL,&child,&error)){
  gtk_text_buffer_set_text(details,error->message,-1);g_error_free(error);child=0;gtk_label_set_text(GTK_LABEL(state),"Unable to start. Open Details for more information.");return;
 }
 gtk_widget_set_sensitive(start,FALSE);gtk_widget_set_sensitive(stop,TRUE);gtk_widget_set_sensitive(pair,TRUE);set_code("Getting code...");gtk_label_set_text(GTK_LABEL(state),"Starting DoveCast...");g_child_watch_add(child,finished,NULL);
}
static gboolean close_window(GtkWidget *w,GdkEvent *e,gpointer data){(void)e;stop_receiver(w,data);gtk_main_quit();return FALSE;}
static gboolean auto_start(gpointer data){(void)data;start_receiver(NULL,NULL);return FALSE;}
int main(int argc,char **argv){
 gboolean autostart=argc>1&&!strcmp(argv[1],"--start"),preview=argc>1&&!strcmp(argv[1],"--preview");int width=preview&&argc>2?atoi(argv[2]):520;
 gtk_init(&argc,&argv);GtkWidget *window=dove_form("DoveCast",width,360),*box=gtk_vbox_new(FALSE,8);
 dove_window_fit(window,width,360);gtk_window_set_wmclass(GTK_WINDOW(window),"DoveCast","DoveCast");gtk_window_set_icon_from_file(GTK_WINDOW(window),"/media/usb0/DoveOS-v2/AppIcons/dove-cast-48.png",NULL);
 gtk_container_set_border_width(GTK_CONTAINER(box),12);gtk_container_add(GTK_CONTAINER(window),box);
 GtkWidget *heading=gtk_hbox_new(FALSE,10);pack(heading,dove_image("/media/usb0/DoveOS-v2/AppIcons/dove-cast-48.png"));
 GtkWidget *title=line("");gtk_label_set_markup(GTK_LABEL(title),"<b>DoveCast</b>\nYouTube videos on your dock");gtk_box_pack_start(GTK_BOX(heading),title,TRUE,TRUE,0);pack(box,heading);
 pack(box,line("YouTube only. Other Cast apps are not supported."));
 GtkWidget *steps=gtk_vbox_new(FALSE,5);pack(steps,line("1. Connect both devices to the same network."));pack(steps,line("2. Click Start receiver and open YouTube on your phone."));pack(steps,line("3. Settings > Watch on TV > Link with TV code."));pack(box,steps);
 GtkWidget *frame=dove_groupbox("YouTube TV code"),*codebox=gtk_hbox_new(FALSE,12);gtk_container_set_border_width(GTK_CONTAINER(codebox),10);gtk_container_add(GTK_CONTAINER(frame),codebox);
 tvcode=line("");set_code("--- --- --- ---");gtk_label_set_selectable(GTK_LABEL(tvcode),TRUE);gtk_box_pack_start(GTK_BOX(codebox),tvcode,TRUE,TRUE,0);pair=dove_button("New code");gtk_widget_set_size_request(pair,95,28);pack(codebox,pair);pack(box,frame);
 state=line("Click Start receiver to get your TV code.");connections=line("Connected devices: 0");pack(box,state);pack(box,connections);
 GtkWidget *actions=gtk_hbox_new(FALSE,8);start=dove_button("Start receiver");stop=dove_button("Stop");gtk_widget_set_size_request(start,120,28);gtk_widget_set_size_request(stop,85,28);pack(actions,start);pack(actions,stop);pack(box,actions);gtk_widget_set_sensitive(pair,FALSE);gtk_widget_set_sensitive(stop,FALSE);
 gtk_widget_set_tooltip_text(pair,"Get a fresh YouTube TV code to link your device.");
 GtkWidget *expander=gtk_expander_new("Details"),*scroll=gtk_scrolled_window_new(NULL,NULL),*view=gtk_text_view_new();gtk_text_view_set_editable(GTK_TEXT_VIEW(view),FALSE);gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view),FALSE);gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view),GTK_WRAP_WORD_CHAR);details=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
 gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);gtk_widget_set_size_request(scroll,-1,100);gtk_container_add(GTK_CONTAINER(scroll),view);gtk_container_add(GTK_CONTAINER(expander),scroll);pack(box,expander);pack(box,line("During playback, Escape returns to these controls."));
 g_signal_connect(start,"clicked",G_CALLBACK(start_receiver),NULL);g_signal_connect(pair,"clicked",G_CALLBACK(new_code),NULL);g_signal_connect(stop,"clicked",G_CALLBACK(stop_receiver),NULL);g_signal_connect(window,"delete-event",G_CALLBACK(close_window),NULL);
 dove_timer(500,poll_status,NULL);gtk_widget_show_all(window);
 if(preview)show_status("Listening as DoveCast\nTV code: 123 456 789 012\nWi-Fi: local | Connected phones: 1\nReady\n");
 if(autostart)g_idle_add(auto_start,NULL);
 gtk_main();return 0;
}
