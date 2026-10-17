#define _POSIX_C_SOURCE 200809L
#include "doveos.h"
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
static GtkWidget *start,*stop,*status,*audio;
static GPid child;
static guint output_timer;
static int pipe_fd=-1;
static GString *output;
static gboolean poll_output(gpointer unused){
 (void)unused;
 if(pipe_fd<0)return FALSE;
 char text[1025];ssize_t size;
 while((size=read(pipe_fd,text,1024))>0){text[size]=0;g_string_append(output,text);}
 if(output->len>3000)g_string_erase(output,0,output->len-3000);
 gtk_label_set_text(GTK_LABEL(status),output->str);
 return TRUE;
}
static void finished(GPid pid,gint result,gpointer unused){
 (void)unused;poll_output(NULL);g_spawn_close_pid(pid);child=0;
 if(output_timer){g_source_remove(output_timer);output_timer=0;}
 if(pipe_fd>=0){close(pipe_fd);pipe_fd=-1;}
 gtk_widget_set_sensitive(start,TRUE);gtk_widget_set_sensitive(stop,FALSE);
 gtk_widget_set_sensitive(audio,TRUE);
 if(WIFEXITED(result)&&WEXITSTATUS(result)==0)
  g_string_append(output,"\nStopped. DoveOS is restored; you can reconnect and start again.");
 else if(WIFEXITED(result))
  g_string_append_printf(output,"\nCould not continue (code %d). See the details above, then try Start receiver again.",WEXITSTATUS(result));
 else g_string_append(output,"\nReceiver was interrupted. You can start it again.");
 gtk_label_set_text(GTK_LABEL(status),output->str);
}
static void stop_receiver(GtkWidget *widget,gpointer unused){
 (void)widget;(void)unused;if(!child)return;
 int fd=open("/tmp/doveos-airplay.stop",O_WRONLY|O_CREAT,0600);if(fd>=0)close(fd);
}
static void start_receiver(GtkWidget *widget,gpointer unused){
 (void)widget;(void)unused;if(child)return;
 g_setenv("DOVE_AIRPLAY_AUDIO",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(audio))?
 "audio/x-raw,format=S16LE,rate=48000,channels=2 ! alsasink device=plughw:RTKAlsaCard,0 buffer-time=100000 latency-time=10000":"fakesink",TRUE);
 gchar *argv[]={"/tmp/doveos-airplay-run","--exec","chroot","/proc/1/root","/bin/sh",
 "/media/usb0/DoveOS-v2/AirPlay/receiver.sh","start",NULL};
 GError *error=NULL;
 if(!g_spawn_async_with_pipes(NULL,argv,NULL,G_SPAWN_SEARCH_PATH|G_SPAWN_DO_NOT_REAP_CHILD,
 NULL,NULL,&child,NULL,&pipe_fd,NULL,&error)){
 gtk_label_set_text(GTK_LABEL(status),error->message);g_error_free(error);child=0;return;
 }
 fcntl(pipe_fd,F_SETFL,fcntl(pipe_fd,F_GETFL)|O_NONBLOCK);
 g_string_truncate(output,0);gtk_widget_set_sensitive(start,FALSE);
 gtk_widget_set_sensitive(stop,TRUE);gtk_widget_set_sensitive(audio,FALSE);
 gtk_label_set_text(GTK_LABEL(status),"Preparing receiver. Please wait for Listening.");
 g_child_watch_add(child,finished,NULL);output_timer=dove_timer(250,poll_output,NULL);
}
static gboolean close_window(GtkWidget *widget,GdkEvent *event,gpointer unused){
 (void)widget;(void)event;(void)unused;stop_receiver(NULL,NULL);gtk_main_quit();return FALSE;
}
int main(int argc,char **argv){
 gtk_init(&argc,&argv);output=g_string_new("");
 GtkWidget *window=dove_form("DoveOS DoveAir",530,410);
 GtkWidget *box=gtk_vbox_new(FALSE,6);gtk_container_set_border_width(GTK_CONTAINER(box),12);
 gtk_container_add(GTK_CONTAINER(window),box);
 dove_add(box,dove_label("AirPlay screen mirroring"));
 dove_add(box,dove_label("1. Connect DoveOS and your iPhone/iPad to the same reachable network.\n"
 "2. Click Start receiver and wait for Listening.\n"
 "3. On the phone, open Control Center > Screen Mirroring.\n"
 "4. Choose DoveAir.\n"
 "Rotation briefly restarts the picture; sound stays connected.\n\n"
 "Ctrl+Alt+Shift+Q returns to DoveOS. The receiver stops after 30 minutes."));
 audio=dove_checkbox("Forward sound to Lap Dock speakers",TRUE);dove_add(box,audio);
 GtkWidget *row=gtk_hbox_new(FALSE,8);start=dove_button("Start receiver");
 stop=dove_button("Stop receiver");gtk_widget_set_sensitive(stop,FALSE);dove_add(row,start);dove_add(row,stop);dove_add(box,row);
 status=dove_label("Preview: portrait, landscape rotation and dock speaker audio tested.\nThe picture may pause briefly while changing orientation.");
 gtk_misc_set_alignment(GTK_MISC(status),0,0);
 gtk_label_set_selectable(GTK_LABEL(status),TRUE);
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);
 gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
 gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroll),status);
 gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 g_signal_connect(start,"clicked",G_CALLBACK(start_receiver),NULL);
 g_signal_connect(stop,"clicked",G_CALLBACK(stop_receiver),NULL);
 g_signal_connect(window,"delete-event",G_CALLBACK(close_window),NULL);
 gtk_widget_show_all(window);gtk_main();g_string_free(output,TRUE);return 0;
}
