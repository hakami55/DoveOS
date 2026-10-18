#define _POSIX_C_SOURCE 200809L
#include "doveos.h"
#include "dove_ui_layout.h"
#include <libuvc/libuvc.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
static uvc_context_t *context;
static uvc_device_t **devices,*choices[32];
static GtkWidget *window,*select_device,*status,*start_button,*refresh,*sound,*duration,*resolution;
static GPid viewer;
static void enabled(gboolean value){gtk_widget_set_sensitive(start_button,value);gtk_widget_set_sensitive(refresh,value);gtk_widget_set_sensitive(select_device,value);gtk_widget_set_sensitive(sound,value);gtk_widget_set_sensitive(duration,value);gtk_widget_set_sensitive(resolution,value);}
static void done(GPid pid,gint code,gpointer unused){
 (void)unused;viewer=0;g_spawn_close_pid(pid);enabled(TRUE);gtk_widget_show(window);
 char message[512];FILE *f=fopen("/proc/1/root/tmp/doveos-hdmi-test-last.status","r");
 if(f&&fgets(message,sizeof message,f))gtk_label_set_text(GTK_LABEL(status),message);
 else gtk_label_set_text(GTK_LABEL(status),WIFEXITED(code)&&WEXITSTATUS(code)==0?"HDMI closed. Ready to open again.":"HDMI stopped. Check the source and try again.");
 if(f)fclose(f);
}
static void rescan(GtkWidget *widget,gpointer unused){
 (void)widget;(void)unused;if(viewer)return;
 GtkTreeModel *model=gtk_combo_box_get_model(GTK_COMBO_BOX(select_device));gtk_list_store_clear(GTK_LIST_STORE(model));
 if(devices){uvc_free_device_list(devices,1);devices=NULL;}
 for(unsigned i=0;i<32;i++)choices[i]=NULL;int count=0;
 if(uvc_get_device_list(context,&devices)>=0)for(int i=0;devices[i]&&count<32;i++){
  uvc_device_descriptor_t *d=NULL;if(uvc_get_device_descriptor(devices[i],&d)<0)continue;
  if(d->idVendor==0x2b89&&d->idProduct==0x5854){choices[count++]=devices[i];gtk_combo_box_append_text(GTK_COMBO_BOX(select_device),d->product?d->product:"UGREEN HDMI capture");}
  uvc_free_device_descriptor(d);
 }
 gtk_combo_box_set_active(GTK_COMBO_BOX(select_device),count?0:-1);gtk_widget_set_sensitive(start_button,count>0);
 gtk_label_set_text(GTK_LABEL(status),count?"Capture card ready. Click Open HDMI to view the source.":"Connect the UGREEN HDMI capture card, then click Refresh.");
}
static void start(GtkWidget *widget,gpointer unused){
 (void)widget;(void)unused;int n=gtk_combo_box_get_active(GTK_COMBO_BOX(select_device));if(viewer||n<0||n>=32||!choices[n])return;
 char bus[16],address[16];snprintf(bus,sizeof bus,"%u",uvc_get_bus_number(choices[n]));snprintf(address,sizeof address,"%u",uvc_get_device_address(choices[n]));
 const char *lengths[]={"0","60","120","300"};int length=gtk_combo_box_get_active(GTK_COMBO_BOX(duration));if(length<0||length>3)length=0;
 gchar *args[]={"/tmp/doveos-hdmi-test-run","--exec","chroot","/proc/1/root","/bin/sh","/media/usb0/DoveOS-v2/Capture/Test/test-run.sh",bus,address,gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sound))?"1":"0",(gchar*)lengths[length],gtk_combo_box_get_active(GTK_COMBO_BOX(resolution))==1?"1080":"720",NULL};GError *error=NULL;
 if(!g_spawn_async(NULL,args,NULL,G_SPAWN_DO_NOT_REAP_CHILD,NULL,NULL,&viewer,&error)){gtk_label_set_text(GTK_LABEL(status),error?error->message:"Could not open HDMI.");if(error)g_error_free(error);viewer=0;return;}
 enabled(FALSE);gtk_widget_hide(window);g_child_watch_add(viewer,done,NULL);
}
static void quit(GtkWidget *widget,gpointer unused){(void)widget;(void)unused;if(!viewer)gtk_main_quit();}
static gboolean close_window(GtkWidget *widget,GdkEvent *event,gpointer unused){(void)event;quit(widget,unused);return TRUE;}
int main(int argc,char **argv){
 if(uvc_init(&context,NULL)<0)return 3;gdk_set_program_class("doveos-hdmi-test");gtk_init(&argc,&argv);
 window=dove_form("HDMI In",560,420);gtk_window_set_wmclass(GTK_WINDOW(window),"doveos-hdmi-test","doveos-hdmi-test");GtkWidget *box=gtk_vbox_new(FALSE,9);gtk_container_set_border_width(GTK_CONTAINER(box),12);gtk_container_add(GTK_CONTAINER(window),box);
 ui_header(box,"HDMI In","Watch an HDMI source with picture and sound.","/etc/doveos/classic-icons/32/video-display.png");
 GtkWidget *connection=ui_group(box,"Capture card");select_device=gtk_combo_box_new_text();ui_pack(connection,select_device);
 GtkWidget *options=ui_group(box,"Playback"),*mode_row=gtk_hbox_new(FALSE,8);ui_pack(mode_row,ui_text("Resolution"));resolution=gtk_combo_box_new_text();gtk_combo_box_append_text(GTK_COMBO_BOX(resolution),"720p / 60 fps");gtk_combo_box_append_text(GTK_COMBO_BOX(resolution),"1080p / 60 fps (experimental)");gtk_combo_box_set_active(GTK_COMBO_BOX(resolution),0);gtk_box_pack_start(GTK_BOX(mode_row),resolution,TRUE,TRUE,0);ui_pack(options,mode_row);
 GtkWidget *length_row=gtk_hbox_new(FALSE,8);ui_pack(length_row,ui_text("Duration"));duration=gtk_combo_box_new_text();gtk_combo_box_append_text(GTK_COMBO_BOX(duration),"Until I exit");gtk_combo_box_append_text(GTK_COMBO_BOX(duration),"1 minute");gtk_combo_box_append_text(GTK_COMBO_BOX(duration),"2 minutes");gtk_combo_box_append_text(GTK_COMBO_BOX(duration),"5 minutes");gtk_combo_box_set_active(GTK_COMBO_BOX(duration),0);gtk_box_pack_start(GTK_BOX(length_row),duration,TRUE,TRUE,0);ui_pack(options,length_row);
 sound=dove_checkbox("Play HDMI sound through the dock",TRUE);ui_pack(options,sound);
 ui_pack(options,ui_text("On the source, select the capture card as audio output."));
 gtk_widget_set_tooltip_text(resolution,"1080p60 is experimental; actual playback may be slower. 720p is recommended.");
 ui_pack(box,ui_text("Return to DoveOS: Esc, F10 or Ctrl+Alt+Shift+Q."));
 status=ui_status(box,"Looking for the capture card...");
 GtkWidget *row=gtk_hbox_new(FALSE,8);refresh=dove_button("Refresh");start_button=dove_button("Open HDMI");GtkWidget *close=dove_button("Close");dove_add(row,start_button);dove_add(row,refresh);gtk_box_pack_end(GTK_BOX(row),close,FALSE,FALSE,0);ui_pack(box,row);
 g_signal_connect(refresh,"clicked",G_CALLBACK(rescan),NULL);g_signal_connect(start_button,"clicked",G_CALLBACK(start),NULL);g_signal_connect(close,"clicked",G_CALLBACK(quit),NULL);g_signal_connect(window,"delete-event",G_CALLBACK(close_window),NULL);
 rescan(NULL,NULL);gtk_widget_show_all(window);gtk_main();if(devices)uvc_free_device_list(devices,1);uvc_exit(context);return 0;
}
