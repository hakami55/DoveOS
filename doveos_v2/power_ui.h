#ifndef DOVEOS_POWER_UI_H
#define DOVEOS_POWER_UI_H
/* Classic radio-button power menu. Actions use the host namespace and RAM worker. */
typedef struct { GtkWidget *description; const char *action; } DovePowerChoice;
static const char *power_description(const char *action) {
 if(!strcmp(action,"standby"))return "Turn off the screen. Move the touchpad or press a key to wake. Apps stay open.";
 if(!strcmp(action,"restart"))return "Save and close your apps, then restart the whole Lap Dock. Keep the system USB connected.";
 if(!strcmp(action,"cpu"))return "Restart the desktop using CPU rendering. You can return to GPU Mode here.";
 if(!strcmp(action,"gpu"))return "Restart the desktop using GPU acceleration at the saved resolution.";
 return "Close DoveOS and release its system USB. Wait for the Safe to remove screen before unplugging it.";
}
static void power_choice_changed(GtkToggleButton *button,gpointer data) {
 DovePowerChoice *choice=data;
 if(gtk_toggle_button_get_active(button))gtk_label_set_text(GTK_LABEL(choice->description),power_description(choice->action));
}
static int power_host(const char *verb,const char *action,char **output) {
 gchar *args[]={"/usr/sbin/chroot","/proc/1/root","/bin/sh","/media/usb0/DoveOS-v2/Power/host.sh",(gchar *)verb,(gchar *)action,NULL};
 gint result=-1;GError *error=NULL;gchar *err=NULL;
 gboolean ok=g_spawn_sync(NULL,args,NULL,0,NULL,NULL,output,&err,&result,&error);
 if(!ok){g_free(*output);*output=g_strdup(error?error->message:"Could not contact the power service.");}
 else if(result&&(!*output||!**output)){g_free(*output);*output=g_strdup(err&&*err?err:"Power action could not be prepared.");}
 if(error)g_error_free(error);
 g_free(err);
 return ok&&WIFEXITED(result)&&WEXITSTATUS(result)==0;
}
static void power_error(const char *text) {
 GtkWidget *dialog=gtk_message_dialog_new(NULL,GTK_DIALOG_MODAL,GTK_MESSAGE_WARNING,GTK_BUTTONS_CLOSE,"%s",text&&*text?text:"Power action unavailable.");
 gtk_dialog_run(GTK_DIALOG(dialog));gtk_widget_destroy(dialog);
}
static int dove_power_dialog(int preview) {
 char renderer[160];read_line("/tmp/doveos-render-status",renderer,sizeof renderer);
 int cpu=!strncmp(renderer,"CPU",3);
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Power Options - DoveOS",NULL,0,
     "_OK",GTK_RESPONSE_OK,"_Cancel",GTK_RESPONSE_CANCEL,"_Help",GTK_RESPONSE_HELP,NULL);
 GtkWidget *buttons=GTK_DIALOG(dialog)->action_area;
 gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons),GTK_BUTTONBOX_END);
 int responses[]={GTK_RESPONSE_OK,GTK_RESPONSE_CANCEL,GTK_RESPONSE_HELP};
 for(int i=0;i<3;i++){
  GtkWidget *button=gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog),responses[i]);
  gtk_button_box_set_child_secondary(GTK_BUTTON_BOX(buttons),button,FALSE);
  gtk_box_reorder_child(GTK_BOX(buttons),button,i);gtk_widget_set_size_request(button,75,25);
 }
 gtk_window_set_wmclass(GTK_WINDOW(dialog),"DovePower","DovePower");
 gtk_window_set_position(GTK_WINDOW(dialog),GTK_WIN_POS_CENTER);
 gtk_window_set_resizable(GTK_WINDOW(dialog),FALSE);
 gtk_window_set_icon_from_file(GTK_WINDOW(dialog),"/etc/doveos/dove-start.png",NULL);
 gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_CANCEL);
 GtkWidget *content=gtk_dialog_get_content_area(GTK_DIALOG(dialog));gtk_container_set_border_width(GTK_CONTAINER(content),14);
 GtkWidget *row=gtk_hbox_new(FALSE,16);gtk_box_pack_start(GTK_BOX(content),row,TRUE,TRUE,0);
 GtkWidget *icon=gtk_image_new_from_file("/etc/doveos/classic-icons/48/preferences-system.png");
 GtkWidget *align=gtk_alignment_new(0,0,0,0);gtk_container_add(GTK_CONTAINER(align),icon);gtk_box_pack_start(GTK_BOX(row),align,FALSE,FALSE,0);
 GtkWidget *body=gtk_vbox_new(FALSE,7);gtk_box_pack_start(GTK_BOX(row),body,TRUE,TRUE,0);
 GtkWidget *prompt=gtk_label_new("What do you want DoveOS to do?");gtk_misc_set_alignment(GTK_MISC(prompt),0,0.5);gtk_box_pack_start(GTK_BOX(body),prompt,FALSE,FALSE,3);
 const char *labels[]={"_Stand by (screen off)","_Restart the Lap Dock",cpu?"Restart in _GPU Mode":"Restart in _CPU Mode","Safely _unmount system USB","Shut _down (coming later)"};
 const char *actions[]={"standby","restart",cpu?"gpu":"cpu","unmount","shutdown"};
 GtkWidget *options[5];DovePowerChoice choices[5];GSList *group=NULL;
 GtkWidget *description=gtk_label_new(power_description(actions[0]));
 gtk_label_set_line_wrap(GTK_LABEL(description),TRUE);gtk_misc_set_alignment(GTK_MISC(description),0,0);
 gtk_widget_set_size_request(description,350,56);
 for(int i=0;i<5;i++){
  options[i]=gtk_radio_button_new_with_mnemonic(group,labels[i]);group=gtk_radio_button_get_group(GTK_RADIO_BUTTON(options[i]));
  choices[i].description=description;choices[i].action=actions[i];
  g_signal_connect(options[i],"toggled",G_CALLBACK(power_choice_changed),&choices[i]);
  gtk_box_pack_start(GTK_BOX(body),options[i],FALSE,FALSE,0);
 }
 gtk_widget_set_sensitive(options[4],FALSE);
 gtk_widget_set_tooltip_text(options[4],"Available after reliable power-off and wake are supported.");
 gtk_box_pack_start(GTK_BOX(body),gtk_hseparator_new(),FALSE,FALSE,3);
 gtk_box_pack_start(GTK_BOX(body),description,FALSE,FALSE,0);
 gtk_widget_show_all(dialog);
 if(preview){while(gtk_events_pending())gtk_main_iteration();g_usleep(300000);printf("POWER_MENU choices=5 shutdown_disabled=%d mode=%s\n",!gtk_widget_get_sensitive(options[4]),actions[2]);gtk_widget_destroy(dialog);return 0;}
 int response;const char *action=NULL;
 for(;;){
  response=gtk_dialog_run(GTK_DIALOG(dialog));
  if(response!=GTK_RESPONSE_HELP)break;
  GtkWidget *help=gtk_message_dialog_new(GTK_WINDOW(dialog),GTK_DIALOG_MODAL,GTK_MESSAGE_INFO,GTK_BUTTONS_CLOSE,
   "Stand by keeps your apps and system running with the screen off.\n\nRestart reboots the Lap Dock. CPU/GPU Mode restarts only the desktop.\n\nUnmount closes DoveOS. Remove only the system USB after the Safe to remove message; keep power connected. Reinsert the USB and restart the dock to use DoveOS again.\n\nFull shutdown is not available yet.");
  gtk_dialog_run(GTK_DIALOG(help));gtk_widget_destroy(help);
 }
 if(response==GTK_RESPONSE_OK)for(int i=0;i<4;i++)if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(options[i])))action=actions[i];
 gtk_widget_destroy(dialog);if(!action)return 0;
 gchar *output=NULL;
 if(!power_host("--check",action,&output)){power_error(output);g_free(output);return 1;}g_free(output);output=NULL;
 if(strcmp(action,"standby")&&!prepare_shutdown())return 0;
 if(!power_host("--request",action,&output)){power_error(output);g_free(output);return 1;}g_free(output);return 0;
}
static void open_power_options(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;
 gchar *args[]={"/usr/bin/doveos-settings-ui","power",NULL};
 g_spawn_async(NULL,args,NULL,0,NULL,NULL,NULL,NULL);
}
#endif
