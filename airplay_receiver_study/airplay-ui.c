#define _POSIX_C_SOURCE 200809L
#include "doveos.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
static GtkWidget *start,*stop,*state,*hint,*audio;
static GtkTextBuffer *details;
static GPid child;
static guint output_timer;
static int pipe_fd=-1;
static GString *output;
static gboolean stopping;
static GtkWidget *line(const char *text){GtkWidget *label=gtk_label_new(text);gtk_misc_set_alignment(GTK_MISC(label),0,0.5);return label;}
static void pack(GtkWidget *box,GtkWidget *widget){gtk_box_pack_start(GTK_BOX(box),widget,FALSE,TRUE,0);}
static void set_state(const char *title,const char *message){gtk_label_set_text(GTK_LABEL(state),title);gtk_label_set_text(GTK_LABEL(hint),message);}
static void show_output(void){
 gtk_text_buffer_set_text(details,output->str,-1);gchar **lines=g_strsplit(output->str,"\n",-1);
 for(gchar **p=lines;*p&&!stopping;p++){
  if(g_str_has_prefix(*p,"Listening as "))set_state("Ready to connect","Choose DoveAir in your device's Screen Mirroring menu.");
  else if(g_str_has_prefix(*p,"Mirroring "))set_state("Connected","Your screen is playing on the dock.");
 }
 g_strfreev(lines);
}
static gboolean poll_output(gpointer unused){
 (void)unused;if(pipe_fd<0)return FALSE;char text[1024];ssize_t size;
 while((size=read(pipe_fd,text,sizeof(text)))>0)g_string_append_len(output,text,size);
 if(output->len>8192){char *next=strchr(output->str+output->len-8192,'\n');if(next)g_string_erase(output,0,next-output->str+1);}
 show_output();return TRUE;
}
static void finished(GPid pid,gint result,gpointer unused){
 (void)unused;poll_output(NULL);g_spawn_close_pid(pid);child=0;stopping=FALSE;
 if(output_timer){g_source_remove(output_timer);output_timer=0;}if(pipe_fd>=0){close(pipe_fd);pipe_fd=-1;}
 gtk_widget_set_sensitive(start,TRUE);gtk_widget_set_sensitive(stop,FALSE);gtk_widget_set_sensitive(audio,TRUE);
 if(WIFEXITED(result)&&WEXITSTATUS(result)==0)set_state("Stopped","Click Start receiver when you want to connect again.");
 else set_state("Unable to continue","Check the network, or open Details for more information.");
}
static void stop_receiver(GtkWidget *widget,gpointer unused){
 (void)widget;(void)unused;if(!child)return;
 int fd=open("/tmp/doveos-airplay.stop",O_WRONLY|O_CREAT,0600);if(fd>=0)close(fd);
 stopping=TRUE;gtk_widget_set_sensitive(stop,FALSE);set_state("Stopping...","The dock desktop will return automatically.");
}
static void start_receiver(GtkWidget *widget,gpointer unused){
 (void)widget;(void)unused;if(child)return;stopping=FALSE;
 g_setenv("DOVE_AIRPLAY_AUDIO",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(audio))?
 "audio/x-raw,format=S16LE,rate=48000,channels=2 ! alsasink device=plughw:RTKAlsaCard,0 buffer-time=100000 latency-time=10000":"fakesink",TRUE);
 gchar *argv[]={"/tmp/doveos-airplay-run","--exec","chroot","/proc/1/root","/bin/sh","/media/usb0/DoveOS-v2/AirPlay/receiver.sh","start",NULL};
 GError *error=NULL;g_string_truncate(output,0);gtk_text_buffer_set_text(details,"",-1);
 if(!g_spawn_async_with_pipes(NULL,argv,NULL,G_SPAWN_SEARCH_PATH|G_SPAWN_DO_NOT_REAP_CHILD,NULL,NULL,&child,NULL,&pipe_fd,NULL,&error)){
  gtk_text_buffer_set_text(details,error->message,-1);g_error_free(error);child=0;
  set_state("Unable to start","Open Details, then try Start receiver again.");return;
 }
 fcntl(pipe_fd,F_SETFL,fcntl(pipe_fd,F_GETFL)|O_NONBLOCK);
 gtk_widget_set_sensitive(start,FALSE);gtk_widget_set_sensitive(stop,TRUE);gtk_widget_set_sensitive(audio,FALSE);
 set_state("Starting...","Wait until DoveAir is ready to connect.");g_child_watch_add(child,finished,NULL);output_timer=dove_timer(250,poll_output,NULL);
}
static gboolean close_window(GtkWidget *widget,GdkEvent *event,gpointer unused){(void)widget;(void)event;(void)unused;stop_receiver(NULL,NULL);gtk_main_quit();return FALSE;}
int main(int argc,char **argv){
 gboolean preview=argc>1&&!strcmp(argv[1],"--preview");int width=preview&&argc>2?atoi(argv[2]):520;
 gtk_init(&argc,&argv);output=g_string_new("");
 GtkWidget *window=dove_form("DoveAir",width,330),*box=gtk_vbox_new(FALSE,8);
 dove_window_fit(window,width,330);gtk_window_set_wmclass(GTK_WINDOW(window),"doveos-airplay-ui","DoveAir");
 gtk_window_set_icon_from_file(GTK_WINDOW(window),"/media/usb0/DoveOS-v2/AppIcons/dove-airplay-v2.png",NULL);
 gtk_container_set_border_width(GTK_CONTAINER(box),12);gtk_container_add(GTK_CONTAINER(window),box);
 GtkWidget *heading=gtk_hbox_new(FALSE,10);pack(heading,dove_image("/media/usb0/DoveOS-v2/AppIcons/dove-airplay-v2.png"));
 GtkWidget *title=line("");gtk_label_set_markup(GTK_LABEL(title),"<b>DoveAir</b>\nAirPlay screen mirroring");
 gtk_box_pack_start(GTK_BOX(heading),title,TRUE,TRUE,0);pack(box,heading);
 GtkWidget *steps=gtk_vbox_new(FALSE,5);
 pack(steps,line("1. Connect both devices to the same network."));
 pack(steps,line("2. Click Start receiver, then open Screen Mirroring."));
 pack(steps,line("3. Choose DoveAir on your device."));pack(box,steps);
 audio=dove_checkbox("Play sound through the dock",TRUE);pack(box,audio);
 GtkWidget *frame=dove_groupbox("Receiver"),*inside=gtk_vbox_new(FALSE,5);
 gtk_container_set_border_width(GTK_CONTAINER(inside),10);gtk_container_add(GTK_CONTAINER(frame),inside);
 state=line("Ready");hint=line("Click Start receiver to make DoveAir available.");gtk_label_set_selectable(GTK_LABEL(hint),TRUE);
 pack(inside,state);pack(inside,hint);pack(box,frame);
 GtkWidget *actions=gtk_hbox_new(FALSE,8);start=dove_button("Start receiver");stop=dove_button("Stop");
 gtk_widget_set_size_request(start,120,28);gtk_widget_set_size_request(stop,85,28);gtk_widget_set_sensitive(stop,FALSE);
 pack(actions,start);pack(actions,stop);pack(box,actions);
 GtkWidget *expander=gtk_expander_new("Details"),*scroll=gtk_scrolled_window_new(NULL,NULL),*view=gtk_text_view_new();
 gtk_text_view_set_editable(GTK_TEXT_VIEW(view),FALSE);gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view),FALSE);
 gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view),GTK_WRAP_WORD_CHAR);details=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
 gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
 gtk_widget_set_size_request(scroll,-1,110);gtk_container_add(GTK_CONTAINER(scroll),view);gtk_container_add(GTK_CONTAINER(expander),scroll);pack(box,expander);
 pack(box,line("Ctrl+Alt+Shift+Q returns to DoveOS during mirroring."));
 g_signal_connect(start,"clicked",G_CALLBACK(start_receiver),NULL);g_signal_connect(stop,"clicked",G_CALLBACK(stop_receiver),NULL);g_signal_connect(window,"delete-event",G_CALLBACK(close_window),NULL);
 gtk_widget_show_all(window);if(preview)set_state("Ready to connect","Choose DoveAir in your device's Screen Mirroring menu.");
 gtk_main();g_string_free(output,TRUE);return 0;
}
