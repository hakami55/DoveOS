#include "doveos.h"
#include <sys/wait.h>
#include "layout-examples.h"
#define DOVE_CONTROLS_EMBEDDED
#define window controls_window
#include "controls.c"
#undef window
#undef DOVE_CONTROLS_EMBEDDED
static GtkWidget *window,*pages,*nav,*task_button,*cancel_button,*result;
static DoveApp *app;static DovePreferences *prefs;static DoveTask *task;
static gint exit_result;
static gboolean combined_smoke(gpointer data) {
 (void)data;gboolean ok=ticks>=2&&busy_id==0&&gtk_status_icon_is_embedded(tray);
 g_print("SDK_2_SMOKE timer_ticks=%u busy_restored=%d tray_embedded=%d pass=%d\n",ticks,busy_id==0,gtk_status_icon_is_embedded(tray),ok);
 exit_result=ok?0:1;gtk_widget_destroy(window);return FALSE;
}
static void task_released(gpointer data){(void)data;task=NULL;}
static void table_choice(GtkTreeView *view,const gchar *path,GtkTreeModel *options,GtkTreeIter *choice,gpointer data) {
 (void)view;GtkTreeIter row;gchar *label=NULL;gtk_tree_model_get(options,choice,0,&label,-1);
 if(gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(data),&row,path))gtk_list_store_set(GTK_LIST_STORE(data),&row,4,label,-1);
 g_free(label);
}
static void table_toggle(GtkCellRendererToggle *cell,gchar *path,gpointer data) {
 (void)cell;GtkTreeIter row;gboolean active;
 if(gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(data),&row,path)){
  gtk_tree_model_get(GTK_TREE_MODEL(data),&row,2,&active,-1);gtk_list_store_set(GTK_LIST_STORE(data),&row,2,!active,-1);
 }
}
static void section(gint id,gpointer data){(void)data;gtk_notebook_set_current_page(GTK_NOTEBOOK(pages),id);}
static void finished(const char *output,const char *errors,gint status,gboolean cancelled,gpointer data) {
 (void)data;task=NULL;gtk_widget_set_sensitive(task_button,TRUE);gtk_widget_set_sensitive(cancel_button,FALSE);
 gchar *safe=g_utf8_make_valid(*errors?errors:output,-1);gtk_label_set_text(GTK_LABEL(result),safe);g_free(safe);
 dove_status(app->status,cancelled?"Task cancelled":WIFEXITED(status)&&WEXITSTATUS(status)==0?"Background task completed":"Background task failed");
}
static void run(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;if(task)return;
 /* Fixed demo command: no user text or hardware action. */
 const char *argv[]={"/bin/sh","-c","sleep 2; printf 'GTK stayed responsive while the task ran.\\n'",NULL};GError *error=NULL;
 task=dove_task_start(window,argv,5000,finished,NULL,task_released,&error);
 if(!task){dove_status(app->status,error->message);g_error_free(error);return;}
 gtk_widget_set_sensitive(task_button,FALSE);gtk_widget_set_sensitive(cancel_button,TRUE);dove_status(app->status,"Working; you can change pages or cancel.");
}
static void cancel(GtkWidget *widget,gpointer data){(void)widget;(void)data;dove_task_cancel(task);}
static void save(GtkWidget *widget,gpointer data) {
 (void)widget;GError *error=NULL;
 if(!prefs){dove_status(app->status,"Settings file unavailable; existing settings preserved.");return;}
 g_key_file_set_boolean(prefs->values,"Window","remember",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(data)));
 gint width,height;gtk_window_get_size(GTK_WINDOW(window),&width,&height);
 g_key_file_set_integer(prefs->values,"Window","width",width);g_key_file_set_integer(prefs->values,"Window","height",height);
 if(!dove_preferences_save(prefs,&error)){dove_status(app->status,error->message);g_error_free(error);}else dove_status(app->status,"Preferences saved to your persistent home folder.");
}
static void close_window(GtkWidget *widget,gpointer data){(void)widget;(void)data;gtk_main_quit();}
int main(int argc,char **argv) {
 gtk_init(&argc,&argv);GError *error=NULL;prefs=dove_preferences_open("sdk-workbench",NULL,&error);
 window=dove_form("DoveOS SDK 2 Demo",840,540);gtk_window_set_icon_from_file(GTK_WINDOW(window),"/media/usb0/DoveOS-v2/ClassicApps/icons/sdk-48.png",NULL);
 gint width=840,height=540;
 if(prefs&&dove_preferences_bool(prefs,"Window","remember",FALSE)){width=dove_preferences_int(prefs,"Window","width",840,420,1100);height=dove_preferences_int(prefs,"Window","height",540,300,620);}
 dove_window_fit(window,width,height);app=dove_app_layout(window);
 dove_pack(app->header,dove_label("SDK 2 - controls, dialogs, layouts and application examples"),FALSE);
 GtkWidget *split=gtk_hpaned_new();dove_pack(app->content,split,TRUE);
 nav=dove_sidebar(section,NULL);GtkWidget *scroll=dove_scroll(nav);gtk_widget_set_size_request(scroll,145,-1);gtk_paned_pack1(GTK_PANED(split),scroll,FALSE,FALSE);gtk_paned_set_position(GTK_PANED(split),150);
 pages=dove_tabs();gtk_notebook_set_show_tabs(GTK_NOTEBOOK(pages),FALSE);gtk_notebook_set_show_border(GTK_NOTEBOOK(pages),FALSE);gtk_paned_pack2(GTK_PANED(split),pages,TRUE,FALSE);
 const char *names[]={"Layouts","Tables","Pictures","Background tasks","Preferences","Controls / dialogs"};
 for(gint i=0;i<6;i++)dove_sidebar_add(nav,names[i],i);
 dove_tab_add(pages,layout_examples(),names[0]);
 GtkWidget *page=dove_page();GtkListStore *store=gtk_list_store_new(5,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_BOOLEAN,GTK_TYPE_TREE_MODEL,G_TYPE_STRING);GtkTreeIter iter;
 GtkListStore *options=gtk_list_store_new(1,G_TYPE_STRING);const char *actions_list[]={"Desktop","Browser","Media Player"};
 for(gint i=0;i<3;i++){gtk_list_store_append(options,&iter);gtk_list_store_set(options,&iter,0,actions_list[i],-1);}
 for(gint i=0;i<3;i++){gtk_list_store_append(store,&iter);gtk_list_store_set(store,&iter,0,names[i],1,"Ready",2,TRUE,3,options,4,"Desktop",-1);}g_object_unref(options);
 GtkWidget *view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));dove_table_text(view,"Component",0,TRUE);dove_table_text(view,"Status",1,FALSE);dove_table_toggle(view,"Available",2,G_CALLBACK(table_toggle),store);
 dove_table_dropdown(view,"Example choice",4,3,2,0,table_choice,store);g_object_unref(store);dove_pack(page,dove_scroll(view),TRUE);
 dove_pack(page,dove_label("Click a row's arrow to choose. Uncheck Available to disable its dropdown. These choices only change the demo table."),FALSE);dove_tab_add(pages,page,names[1]);
 page=dove_page();GtkWidget *picture=dove_picture_fit("/etc/doveos/dove-start.png",160,110,NULL);
 dove_pack(page,dove_label("Aspect-preserving, bounded picture decoding:"),FALSE);if(picture)dove_pack(page,picture,FALSE);
 dove_pack(page,dove_label("Transparent images use the repaired native image painter. Missing files return a GError so apps can show a useful message."),FALSE);dove_tab_add(pages,dove_scroll(page),names[2]);
 page=dove_page();dove_pack(page,dove_label("Run a two-second task without blocking menus or pointer movement."),FALSE);GtkWidget *actions=dove_actions();dove_pack(page,actions,FALSE);
 task_button=dove_action(actions,"_Run example",G_CALLBACK(run),NULL);cancel_button=dove_action(actions,"_Cancel",G_CALLBACK(cancel),NULL);gtk_widget_set_sensitive(cancel_button,FALSE);
 result=dove_label("No task has run.");gtk_label_set_selectable(GTK_LABEL(result),TRUE);dove_pack(page,result,FALSE);dove_tab_add(pages,dove_scroll(page),names[3]);
 page=dove_page();GtkWidget *remember=dove_checkbox("Remember window size",prefs&&dove_preferences_bool(prefs,"Window","remember",FALSE));dove_pack(page,remember,FALSE);
 actions=dove_actions();dove_pack(page,actions,FALSE);dove_action(actions,"_Save settings",G_CALLBACK(save),remember);dove_pack(page,dove_label("Preferences use a small INI file. Existing application profiles stay in their current locations."),FALSE);dove_tab_add(pages,dove_scroll(page),names[4]);
 dove_tab_add(pages,dove_scroll(sdk_controls_page(window)),names[5]);
 dove_action(app->actions,"_Close",G_CALLBACK(gtk_widget_destroy),window);dove_shortcut(window,task_button,"clicked","<Control>r");
 g_signal_connect(window,"destroy",G_CALLBACK(close_window),NULL);gtk_widget_show_all(window);dove_sidebar_select(nav,0);
 if(argc>1&&!strcmp(argv[1],"--dialogs")){dove_sidebar_select(nav,5);gtk_notebook_set_current_page(GTK_NOTEBOOK(book),1);}
 if(argc>1&&!strcmp(argv[1],"--tray-preview")){dove_sidebar_select(nav,5);gtk_notebook_set_current_page(GTK_NOTEBOOK(book),4);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_tray),TRUE);}
 if(argc>1&&!strcmp(argv[1],"--smoke-test")){dove_sidebar_select(nav,5);gtk_status_icon_set_visible(tray,TRUE);busy(NULL,NULL);dove_timer(3500,combined_smoke,NULL);}
 dove_status(app->status,error?error->message:"GTK2 runtime | Native DoveOS theme | SDK 1 APIs retained");g_clear_error(&error);gtk_main();
 /* Pending child watches finish even if the window closes while a task runs. */
 while(task)g_main_context_iteration(NULL,TRUE);
 dove_preferences_free(prefs);return exit_result;
}
