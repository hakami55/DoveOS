#define _GNU_SOURCE
#include "doveos.h"
#include <stdio.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

static GtkWidget *window,*filebox,*status,*slider,*playbutton,*pausebutton,*stopbutton,*browsebutton,*time_label;
static GPid child;
static int updating,closing;
static double position,duration;
static char last_state[64]="Ready";
#define BASE "/media/usb0/DoveOS-v2/DovePlayer"
#define STATE "/tmp/doveplayer/state"
static void status_text(const char *s){gtk_label_set_text(GTK_LABEL(status),s);}
static void command(int signal_number){
 FILE *f=fopen("/tmp/doveplayer/core.pid","r");long pid=0;if(f){if(fscanf(f,"%ld",&pid)!=1)pid=0;fclose(f);}
 if(pid>1){char path[80],name[80]={0};snprintf(path,sizeof path,"/proc/%ld/comm",pid);f=fopen(path,"r");if(f){if(!fgets(name,sizeof name,f))name[0]=0;fclose(f);}if(!strncmp(name,"ld-linux-armhf",14)||!strncmp(name,"doveplayer-core",15)){kill((pid_t)pid,signal_number);return;}}
 if(child)kill(child,SIGTERM);
}
static void sensitivity(void){
 gtk_widget_set_sensitive(playbutton,!child);gtk_widget_set_sensitive(pausebutton,child!=0);gtk_widget_set_sensitive(stopbutton,child!=0);
 gtk_widget_set_sensitive(browsebutton,!child);gtk_widget_set_sensitive(filebox,!child);gtk_widget_set_sensitive(slider,!child&&duration>0);
}
static gboolean read_state(gpointer unused){
 (void)unused;FILE *f=fopen(STATE,"r");char state[64];double p,d;
 if(f){if(fgets(state,sizeof state,f)&&fscanf(f,"%lf%lf",&p,&d)==2){
  state[strcspn(state,"\r\n")]=0;snprintf(last_state,sizeof last_state,"%s",state);
  if(child){position=p;duration=d;updating=1;gtk_range_set_range(GTK_RANGE(slider),0,d>0?d:1);gtk_range_set_value(GTK_RANGE(slider),p);updating=0;}
 }fclose(f);}
 char text[160];snprintf(text,sizeof text,"%02d:%02d / %02d:%02d",(int)position/60,(int)position%60,(int)duration/60,(int)duration%60);gtk_label_set_text(GTK_LABEL(time_label),text);
 if(child)status_text(!strcmp(last_state,"Playing")?"Playing — Space: pause and show controls; Esc: stop.":last_state);
 return TRUE;
}
static void ended(GPid pid,gint code,gpointer unused){
 (void)unused;read_state(NULL);g_spawn_close_pid(pid);child=0;
 if(WIFEXITED(code)&&WEXITSTATUS(code)==21){status_text("Paused. Adjust the position or click Resume.");gtk_button_set_label(GTK_BUTTON(playbutton),"Resume");}
 else if(WIFEXITED(code)&&WEXITSTATUS(code)==0){status_text(!strcmp(last_state,"Finished")?"Finished.":"Stopped.");position=0;gtk_button_set_label(GTK_BUTTON(playbutton),"Play");}
 else {status_text("Playback could not finish. Click Details for the reason.");gtk_button_set_label(GTK_BUTTON(playbutton),"Play");}
 updating=1;gtk_range_set_value(GTK_RANGE(slider),position);updating=0;
 sensitivity();if(closing)gtk_main_quit();
}
static void play(GtkWidget *w,gpointer unused){
 (void)w;(void)unused;if(child)return;
 const char *path=gtk_entry_get_text(GTK_ENTRY(filebox));if(!*path){dove_message(GTK_WINDOW(window),"Choose a video or audio file first.");return;}
 char seconds[48];snprintf(seconds,sizeof seconds,"%.6f",position);
 gchar *args[]={"/usr/sbin/chroot","/proc/1/root","/bin/sh",BASE "/play.sh",(gchar*)path,seconds,"0",NULL};GError *error=NULL;
 if(!g_spawn_async(NULL,args,NULL,G_SPAWN_DO_NOT_REAP_CHILD,NULL,NULL,&child,&error)){status_text(error->message);g_error_free(error);child=0;return;}
 g_child_watch_add(child,ended,NULL);snprintf(last_state,sizeof last_state,"Starting");status_text("Starting playback...");sensitivity();
}
static void pause_clicked(GtkWidget *w,gpointer p){(void)w;(void)p;command(SIGUSR1);}
static void stop_clicked(GtkWidget *w,gpointer p){(void)w;(void)p;command(SIGTERM);}
static void slider_changed(GtkRange *r,gpointer p){(void)p;if(!updating&&!child)position=gtk_range_get_value(r);}
static void file_changed(GtkEditable *e,gpointer p){(void)e;(void)p;if(!child){position=duration=0;gtk_button_set_label(GTK_BUTTON(playbutton),"Play");sensitivity();}}
static void browse(GtkWidget *w,gpointer p){
 (void)w;(void)p;GtkWidget *dialog=dove_openfiledialog(GTK_WINDOW(window),"Open video or audio");
 GtkFileFilter *filter=gtk_file_filter_new();gtk_file_filter_set_name(filter,"Video and audio");
 const char *patterns[]={"*.mp4","*.m4v","*.mkv","*.mov","*.m4a","*.mp3","*.wav","*.flac","*.ogg",NULL};
 for(int i=0;patterns[i];i++)gtk_file_filter_add_pattern(filter,patterns[i]);
 gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),filter);
 GtkFileFilter *all=gtk_file_filter_new();gtk_file_filter_set_name(all,"All files");gtk_file_filter_add_pattern(all,"*");gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),all);
 gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),"/media/usb0/DrMoSa/Media");
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT){gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));gtk_entry_set_text(GTK_ENTRY(filebox),path);g_free(path);position=duration=0;gtk_button_set_label(GTK_BUTTON(playbutton),"Play");status_text("Ready. Video opens fullscreen at up to 720p.");sensitivity();}
 gtk_widget_destroy(dialog);
}
static void details(GtkWidget *w,gpointer p){
 (void)w;(void)p;GtkWidget *d=gtk_dialog_new_with_buttons("DovePlayer — Playback details",GTK_WINDOW(window),GTK_DIALOG_DESTROY_WITH_PARENT,GTK_STOCK_CLOSE,GTK_RESPONSE_CLOSE,NULL);
 GtkWidget *text=dove_textarea("");gtk_text_view_set_editable(GTK_TEXT_VIEW(text),FALSE);GString *all=g_string_new("DovePlayer Preview\nH.264 hardware video; audio decoded on CPU. Output up to 720p.\n\n");
 const char *files[]={"/tmp/doveplayer/launch.log","/tmp/doveplayer/player.log",NULL};
 for(int i=0;files[i];i++){gchar *s=NULL;gsize n=0;if(g_file_get_contents(files[i],&s,&n,NULL)){g_string_append_len(all,s,n>24000?24000:n);g_string_append(all,"\n");g_free(s);}}
 gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(text)),all->str,-1);g_string_free(all,TRUE);
 gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(d))),dove_scrolled(text,640,320),TRUE,TRUE,4);gtk_widget_show_all(d);gtk_dialog_run(GTK_DIALOG(d));gtk_widget_destroy(d);
}
static gboolean close_window(GtkWidget *w,GdkEvent *e,gpointer p){(void)w;(void)e;(void)p;if(child){closing=1;command(SIGTERM);status_text("Stopping playback...");return TRUE;}gtk_main_quit();return TRUE;}
int main(int argc,char **argv){
 gtk_init(&argc,&argv);window=dove_window("DovePlayer",620,270);gtk_container_set_border_width(GTK_CONTAINER(window),10);
 GtkWidget *box=dove_panel(FALSE,5);gtk_container_add(GTK_CONTAINER(window),box);
 dove_add(box,dove_label("DovePlayer"));
 GtkWidget *row=dove_panel(TRUE,6);filebox=dove_textbox(argc>1?argv[1]:"");browsebutton=dove_button("Open file...");gtk_box_pack_start(GTK_BOX(row),filebox,TRUE,TRUE,0);dove_add(row,browsebutton);dove_add(box,row);
 dove_add(box,dove_label("Video: fullscreen, up to 720p60. Space pauses; Esc stops.\nPreview: 16:9 H.264 video at 720p or lower, plus audio files."));
 GtkWidget *frame=dove_groupbox("Playback"),*inner=dove_panel(FALSE,3);gtk_container_set_border_width(GTK_CONTAINER(inner),7);gtk_container_add(GTK_CONTAINER(frame),inner);dove_add(box,frame);
 slider=dove_slider(TRUE,0,1,1);gtk_scale_set_draw_value(GTK_SCALE(slider),FALSE);dove_add(inner,slider);
 row=dove_panel(TRUE,6);playbutton=dove_button("Play");pausebutton=dove_button("Pause");stopbutton=dove_button("Stop");GtkWidget *info=dove_button("Details");
 dove_add(row,playbutton);dove_add(row,pausebutton);dove_add(row,stopbutton);time_label=dove_label("00:00 / 00:00");gtk_box_pack_start(GTK_BOX(row),time_label,TRUE,TRUE,8);dove_add(row,info);dove_add(inner,row);
 status=dove_label("Choose a local video or audio file.");dove_add(box,status);
 g_signal_connect(browsebutton,"clicked",G_CALLBACK(browse),NULL);g_signal_connect(playbutton,"clicked",G_CALLBACK(play),NULL);g_signal_connect(pausebutton,"clicked",G_CALLBACK(pause_clicked),NULL);g_signal_connect(stopbutton,"clicked",G_CALLBACK(stop_clicked),NULL);g_signal_connect(info,"clicked",G_CALLBACK(details),NULL);g_signal_connect(slider,"value-changed",G_CALLBACK(slider_changed),NULL);g_signal_connect(filebox,"changed",G_CALLBACK(file_changed),NULL);g_signal_connect(window,"delete-event",G_CALLBACK(close_window),NULL);
 sensitivity();dove_timer(250,read_state,NULL);gtk_widget_show_all(window);gtk_main();return 0;
}
