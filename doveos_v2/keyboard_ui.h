/* SDK 1.1 typing and language settings. Fn/remapping project is on hold. */
#include "keyboard_layouts.h"
static GtkWidget *kb_delay,*kb_interval,*kb_layout_view,*kb_toggle;
static GtkWidget *kb_group(GtkWidget *parent,const char *title){GtkWidget *f=dove_groupbox(title),*b=dove_panel(FALSE,5);gtk_container_set_border_width(GTK_CONTAINER(b),10);gtk_container_add(GTK_CONTAINER(f),b);add(parent,f);return b;}
static DoveLayouts kb_layouts;
static GtkListStore *kb_layout_model;
static GtkWidget *kb_layout_add,*kb_layout_delete,*kb_layout_default;
static int kb_layout_index(void){GtkTreeIter it;GtkTreeSelection *sel=gtk_tree_view_get_selection(GTK_TREE_VIEW(kb_layout_view));if(!gtk_tree_selection_get_selected(sel,NULL,&it))return -1;GtkTreePath *p=gtk_tree_model_get_path(GTK_TREE_MODEL(kb_layout_model),&it);int i=gtk_tree_path_get_indices(p)[0];gtk_tree_path_free(p);return i;}
static void kb_layout_selected(GtkTreeSelection *sel,gpointer data){(void)sel;(void)data;int i=kb_layout_index();if(!kb_layout_add)return;gtk_widget_set_sensitive(kb_layout_add,kb_layouts.count<2);gtk_widget_set_sensitive(kb_layout_delete,kb_layouts.count>1&&i>=0);gtk_widget_set_sensitive(kb_layout_default,i>0);}
static void kb_layout_rows(void){
 gtk_list_store_clear(kb_layout_model);for(int i=0;i<kb_layouts.count;i++){DoveLayoutInfo info;GtkTreeIter it;gtk_list_store_append(kb_layout_model,&it);gtk_list_store_set(kb_layout_model,&it,0,dove_layout_info(kb_layouts.ids[i],&info)?info.name:kb_layouts.ids[i],1,i==0?"Default":"",-1);}
 GtkTreePath *path=gtk_tree_path_new_from_indices(0,-1);gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(kb_layout_view)),path);gtk_tree_path_free(path);kb_layout_selected(NULL,NULL);
}
static void kb_delete_layout(GtkWidget *w,gpointer data){(void)w;(void)data;int i=kb_layout_index();if(kb_layouts.count<2||i<0)return;if(i==0)strcpy(kb_layouts.ids[0],kb_layouts.ids[1]);kb_layouts.count=1;kb_layouts.ids[1][0]=0;kb_layout_rows();snprintf(message,sizeof message,"Layout removed. Apply to save.");}
static void kb_default_layout(GtkWidget *w,gpointer data){(void)w;(void)data;if(kb_layout_index()!=1)return;char id[16];strcpy(id,kb_layouts.ids[0]);strcpy(kb_layouts.ids[0],kb_layouts.ids[1]);strcpy(kb_layouts.ids[1],id);kb_layout_rows();snprintf(message,sizeof message,"Startup layout changed. Apply to save.");}
static void kb_add_layout(GtkWidget *w,gpointer data){
 (void)w;(void)data;if(kb_layouts.count>=2)return;
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Add keyboard layout",GTK_WINDOW(window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,GTK_STOCK_CANCEL,GTK_RESPONSE_CANCEL,GTK_STOCK_ADD,GTK_RESPONSE_OK,NULL);
 GtkWidget *body=gtk_dialog_get_content_area(GTK_DIALOG(dialog));add(body,label("Choose a layout. Type its name to search."));
 GtkListStore *model=gtk_list_store_new(2,G_TYPE_STRING,G_TYPE_STRING);DoveLayoutInfo items[256];int n=dove_layout_catalogue(items,256);
 for(int i=0;i<n;i++){int exists=0;for(int j=0;j<kb_layouts.count;j++)if(!strcmp(items[i].id,kb_layouts.ids[j]))exists=1;if(exists)continue;GtkTreeIter it;gtk_list_store_append(model,&it);gtk_list_store_set(model,&it,0,items[i].name,1,items[i].id,-1);}
 const char *columns[]={"Available layouts"};GtkWidget *view=dove_listview(GTK_TREE_MODEL(model),columns,1);g_object_unref(model);gtk_tree_view_set_search_column(GTK_TREE_VIEW(view),0);add(body,dove_scrolled(view,420,280));gtk_widget_show_all(dialog);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_OK){GtkTreeIter it;GtkTreeModel *selected;if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),&selected,&it)){gchar *id=NULL;gtk_tree_model_get(selected,&it,1,&id,-1);if(id&&kb_layouts.count<2){snprintf(kb_layouts.ids[kb_layouts.count++],16,"%s",id);kb_layout_rows();snprintf(message,sizeof message,"Layout added. Apply to save.");}g_free(id);}}
 gtk_widget_destroy(dialog);
}
static void kb_apply_done(GPid pid,gint code,gpointer data){(void)data;g_spawn_close_pid(pid);snprintf(message,sizeof message,code==0?"Typing and language settings saved and applied.":"Saved, but layout/repeat helper failed. Check keyboard status.");}
static void kb_apply(GtkWidget *w,gpointer data){
 (void)w;(void)data;DoveKeyboard k=dove_keyboard_load();k.swap=0;k.count=0;memset(k.fn,0,sizeof k.fn);
 k.delay=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(kb_delay));k.interval=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(kb_interval));k.layout=kb_layouts.count==2?2:!strcmp(kb_layouts.ids[0],"ara")?1:0;k.toggle=gtk_combo_box_get_active(GTK_COMBO_BOX(kb_toggle));
 if(dove_layout_save(&kb_layouts)||dove_keyboard_save(k)){snprintf(message,sizeof message,"Could not save keyboard settings on USB.");return;}
 gchar *argv[]={"/bin/sh","/tmp/doveos-theme/keyboard-apply.sh",NULL};GPid pid;GError *error=NULL;
 if(g_spawn_async(NULL,argv,NULL,G_SPAWN_DO_NOT_REAP_CHILD,NULL,NULL,&pid,&error))g_child_watch_add(pid,kb_apply_done,NULL);
 else{snprintf(message,sizeof message,"Saved, but could not apply: %.160s",error?error->message:"unknown error");g_clear_error(&error);}
}
static void kb_restore(GtkWidget *w,gpointer data){
 (void)w;(void)data;GtkWidget *dialog=dove_messagebox(GTK_WINDOW(window),"Restore default typing speed and English keyboard layout?",GTK_MESSAGE_QUESTION,GTK_BUTTONS_OK_CANCEL);
 int result=gtk_dialog_run(GTK_DIALOG(dialog));gtk_widget_destroy(dialog);if(result!=GTK_RESPONSE_OK)return;
 gtk_spin_button_set_value(GTK_SPIN_BUTTON(kb_delay),500);gtk_spin_button_set_value(GTK_SPIN_BUTTON(kb_interval),40);kb_layouts=(DoveLayouts){.count=1,.ids={"us"}};kb_layout_rows();gtk_combo_box_set_active(GTK_COMBO_BOX(kb_toggle),0);kb_apply(NULL,NULL);
}
static void kb_add_apply(GtkWidget *box){GtkWidget *row=dove_panel(TRUE,8);add(box,row);GtkWidget *b=dove_button("Apply keyboard settings");add(row,b);g_signal_connect(b,"clicked",G_CALLBACK(kb_apply),NULL);b=dove_button("Restore defaults...");add(row,b);g_signal_connect(b,"clicked",G_CALLBACK(kb_restore),NULL);}
static void keyboard_pages(void){
 DoveKeyboard k=dove_keyboard_load();GtkWidget *box=page("Keyboard"),*group=kb_group(box,"Typing");GtkWidget *row=dove_panel(TRUE,8);add(group,row);
 add(row,label("Repeat delay (ms):"));kb_delay=dove_numberbox(200,2000,50);gtk_spin_button_set_value(GTK_SPIN_BUTTON(kb_delay),k.delay);add(row,kb_delay);
 add(row,label("Repeat interval (ms):"));kb_interval=dove_numberbox(20,250,5);gtk_spin_button_set_value(GTK_SPIN_BUTTON(kb_interval),k.interval);add(row,kb_interval);
 GtkWidget *entry=dove_textbox("");dove_tooltip(entry,"Type here to test repeat speed and your keyboard layout.");add(group,entry);
 box=page("Language & Layout");group=kb_group(box,"Installed keyboard layouts");kb_layouts=dove_layout_load();
 kb_layout_model=gtk_list_store_new(2,G_TYPE_STRING,G_TYPE_STRING);const char *columns[]={"Language / layout","Startup"};kb_layout_view=dove_listview(GTK_TREE_MODEL(kb_layout_model),columns,2);g_object_unref(kb_layout_model);add(group,dove_scrolled(kb_layout_view,450,95));
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(kb_layout_view)),"changed",G_CALLBACK(kb_layout_selected),NULL);
 row=dove_panel(TRUE,8);add(group,row);kb_layout_add=dove_button("Add...");add(row,kb_layout_add);g_signal_connect(kb_layout_add,"clicked",G_CALLBACK(kb_add_layout),NULL);
 kb_layout_delete=dove_button("Delete");add(row,kb_layout_delete);g_signal_connect(kb_layout_delete,"clicked",G_CALLBACK(kb_delete_layout),NULL);
 kb_layout_default=dove_button("Use as default");add(row,kb_layout_default);g_signal_connect(kb_layout_default,"clicked",G_CALLBACK(kb_default_layout),NULL);kb_layout_rows();
 add(group,label("Keep one or two layouts. The default starts selected on startup.\nClick the language badge in the taskbar to switch layouts."));
 add(group,label("Keyboard shortcut:"));const char *toggles[]={"Alt+Shift","Windows+Space"};kb_toggle=dove_combobox(toggles,2,k.toggle);add(group,kb_toggle);
 group=kb_group(box,"Interface language");add(group,label("English. Full interface translation is not installed.\nKeyboard layout changes typing; it does not translate application menus."));
}
