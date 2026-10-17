#ifndef DOVE_FILE_DIALOG_H
#define DOVE_FILE_DIALOG_H
#include <glib/gstdio.h>
#include <gdk/gdkkeysyms.h>
#include <errno.h>
#include <string.h>
/* Explorer layout around the real GTK2 FileChooser. Keep its public interface,
 * asynchronous listing, filters, multiselect and native Save validation. */
typedef struct {
 GtkWidget *dialog,*address,*places,*name,*list;
 GtkToolItem *back,*forward,*up;
 GPtrArray *history; gint at,requested;
 gboolean syncing;
 GtkFileChooserAction action;
} DoveFileDialog;
static inline GtkWidget *dove_fd_find(GtkWidget *w,const char *type){
 if(!w)return NULL;
 if(!strcmp(G_OBJECT_TYPE_NAME(w),type))return w;
 if(GTK_IS_CONTAINER(w)){GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *i=children;i&&!found;i=i->next)found=dove_fd_find(i->data,type);g_list_free(children);return found;}return NULL;
}
static inline void dove_fd_hide(GtkWidget *w){if(w){gtk_widget_set_no_show_all(w,TRUE);gtk_widget_hide(w);}}
static inline void dove_fd_free(gpointer data){DoveFileDialog *d=data;g_ptr_array_free(d->history,TRUE);g_free(d);}
static inline gboolean dove_fd_go(DoveFileDialog *d,const char *path){
 GError *error=NULL;gboolean ok=gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(d->dialog),path);
 if(!ok){d->requested=-1;g_set_error_literal(&error,G_FILE_ERROR,G_FILE_ERROR_NOENT,"This folder could not be opened.");dove_message(GTK_WINDOW(d->dialog),error->message);g_error_free(error);}return ok;
}
static inline void dove_fd_folder(GtkFileChooser *chooser,gpointer data){
 DoveFileDialog *d=data;gchar *path=gtk_file_chooser_get_current_folder(chooser);if(!path)return;
 if(d->requested>=0&&!g_strcmp0(path,g_ptr_array_index(d->history,d->requested)))d->at=d->requested;
 else if(d->at<0||g_strcmp0(path,g_ptr_array_index(d->history,d->at))){
  while((gint)d->history->len>d->at+1)g_ptr_array_remove_index(d->history,d->history->len-1);
  g_ptr_array_add(d->history,g_strdup(path));if(d->history->len>100)g_ptr_array_remove_index(d->history,0);d->at=(gint)d->history->len-1;
 }
 d->requested=-1;gtk_entry_set_text(GTK_ENTRY(d->address),path);
 gtk_widget_set_sensitive(GTK_WIDGET(d->back),d->at>0);gtk_widget_set_sensitive(GTK_WIDGET(d->forward),d->at+1<(gint)d->history->len);gtk_widget_set_sensitive(GTK_WIDGET(d->up),strcmp(path,"/")!=0);
 GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(d->places));gtk_tree_selection_unselect_all(selection);
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(d->places));GtkTreeIter row;gboolean valid=gtk_tree_model_get_iter_first(model,&row);
 while(valid){gchar *place;gtk_tree_model_get(model,&row,2,&place,-1);if(!g_strcmp0(place,path))gtk_tree_selection_select_iter(selection,&row);g_free(place);valid=gtk_tree_model_iter_next(model,&row);}g_free(path);
}
static inline void dove_fd_action(GtkWidget *w,gpointer data){
 DoveFileDialog *d=data;const char *kind=g_object_get_data(G_OBJECT(w),"dove-fd-action");
 if(!strcmp(kind,"back")||!strcmp(kind,"forward")){gint next=d->at+(!strcmp(kind,"back")?-1:1);if(next>=0&&next<(gint)d->history->len){d->requested=next;dove_fd_go(d,g_ptr_array_index(d->history,next));}return;}
 gchar *path=gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(d->dialog));if(!path)return;
 if(!strcmp(kind,"up")){gchar *parent=g_path_get_dirname(path);dove_fd_go(d,parent);g_free(parent);}
 else if(!strcmp(kind,"refresh"))dove_fd_go(d,path);
 else if(!strcmp(kind,"new")){
  GtkWidget *prompt=gtk_dialog_new_with_buttons("New Folder",GTK_WINDOW(d->dialog),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,GTK_STOCK_CANCEL,GTK_RESPONSE_CANCEL,"Create",GTK_RESPONSE_ACCEPT,NULL);
  GtkWidget *entry=gtk_entry_new();gtk_entry_set_text(GTK_ENTRY(entry),"New Folder");gtk_entry_set_activates_default(GTK_ENTRY(entry),TRUE);gtk_dialog_set_default_response(GTK_DIALOG(prompt),GTK_RESPONSE_ACCEPT);gtk_container_set_border_width(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(prompt))),10);gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(prompt))),entry,FALSE,FALSE,4);gtk_widget_show_all(prompt);
  if(gtk_dialog_run(GTK_DIALOG(prompt))==GTK_RESPONSE_ACCEPT){const char *name=gtk_entry_get_text(GTK_ENTRY(entry));if(!*name||!strcmp(name,".")||!strcmp(name,"..")||strchr(name,'/'))dove_message(GTK_WINDOW(prompt),"Enter a folder name without a slash.");else{gchar *dest=g_build_filename(path,name,NULL);if(g_mkdir(dest,0755))dove_message(GTK_WINDOW(prompt),g_strerror(errno));else dove_fd_go(d,dest);g_free(dest);}}gtk_widget_destroy(prompt);
 }g_free(path);
}
static inline GtkToolItem *dove_fd_tool(DoveFileDialog *d,GtkWidget *bar,const char *label,const char *stock,const char *icon,const char *action){
 GtkToolItem *item=dove_toolbutton(label,stock);gchar *path=g_build_filename("/media/usb0/DoveOS-v2/Explorer/toolbar-icons",icon,NULL);GdkPixbuf *pix=gdk_pixbuf_new_from_file(path,NULL);g_free(path);if(pix){GtkWidget *image=dove_toolbar_icon(pix);g_object_unref(pix);gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(item),image);gtk_widget_show(image);}gtk_tool_item_set_is_important(item,TRUE);gtk_toolbar_insert(GTK_TOOLBAR(bar),item,-1);g_object_set_data(G_OBJECT(item),"dove-fd-action",(gpointer)action);g_signal_connect(item,"clicked",G_CALLBACK(dove_fd_action),d);return item;
}
static inline void dove_fd_address(GtkEntry *entry,gpointer data){DoveFileDialog *d=data;const char *input=gtk_entry_get_text(entry);gchar *current=gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(d->dialog));gchar *path=g_canonicalize_filename(input,current?current:"/");if(g_file_test(path,G_FILE_TEST_IS_DIR))dove_fd_go(d,path);else dove_message(GTK_WINDOW(d->dialog),"This folder does not exist.");g_free(path);g_free(current);}
static inline void dove_fd_place(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data){(void)column;GtkTreeIter row;GtkTreeModel *model=gtk_tree_view_get_model(view);if(gtk_tree_model_get_iter(model,&row,path)){gchar *folder;gtk_tree_model_get(model,&row,2,&folder,-1);dove_fd_go(data,folder);g_free(folder);}}
static inline gboolean dove_fd_place_click(GtkWidget *view,GdkEventButton *event,gpointer data){if(event->button!=1||event->type!=GDK_BUTTON_PRESS)return FALSE;GtkTreePath *path=NULL;if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(view),event->x,event->y,&path,NULL,NULL,NULL)){dove_fd_place(GTK_TREE_VIEW(view),path,NULL,data);gtk_tree_path_free(path);}return FALSE;}
static inline void dove_fd_selection(GtkFileChooser *chooser,gpointer data){
 DoveFileDialog *d=data;if(d->action!=GTK_FILE_CHOOSER_ACTION_OPEN||d->syncing)return;GSList *files=gtk_file_chooser_get_filenames(chooser);GString *text=g_string_new(NULL);
 for(GSList *i=files;i;i=i->next){gchar *base=g_filename_display_basename(i->data);if(i!=files)g_string_append(text,"; ");g_string_append(text,base);g_free(base);}d->syncing=TRUE;gtk_entry_set_text(GTK_ENTRY(d->name),text->str);d->syncing=FALSE;g_string_free(text,TRUE);g_slist_free_full(files,g_free);
}
static inline void dove_fd_name(GtkEntry *entry,gpointer data){
 DoveFileDialog *d=data;if(d->syncing)return;const char *text=gtk_entry_get_text(entry);gchar *folder=gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(d->dialog));gchar *path=g_canonicalize_filename(text,folder?folder:"/");d->syncing=TRUE;gtk_file_chooser_unselect_all(GTK_FILE_CHOOSER(d->dialog));if(*text&&g_file_test(path,G_FILE_TEST_IS_REGULAR))gtk_file_chooser_select_filename(GTK_FILE_CHOOSER(d->dialog),path);d->syncing=FALSE;g_free(path);g_free(folder);
}
static inline void dove_fd_name_activate(GtkEntry *entry,gpointer data){
 DoveFileDialog *d=data;gchar *folder=gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(d->dialog));gchar *path=g_canonicalize_filename(gtk_entry_get_text(entry),folder?folder:"/");if(g_file_test(path,G_FILE_TEST_IS_DIR))dove_fd_go(d,path);else if(g_file_test(path,G_FILE_TEST_IS_REGULAR)){gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(d->dialog),path);gtk_dialog_response(GTK_DIALOG(d->dialog),GTK_RESPONSE_ACCEPT);}else dove_message(GTK_WINDOW(d->dialog),"Choose an existing file.");g_free(path);g_free(folder);
}
static inline gboolean dove_fd_key(GtkWidget *widget,GdkEventKey *event,gpointer data){
 (void)widget;DoveFileDialog *d=data;GtkWidget *tool=NULL;
 if(event->state&GDK_MOD1_MASK){if(event->keyval==GDK_Left)tool=GTK_WIDGET(d->back);else if(event->keyval==GDK_Right)tool=GTK_WIDGET(d->forward);else if(event->keyval==GDK_Up)tool=GTK_WIDGET(d->up);}
 if(event->keyval==GDK_F5){gchar *path=gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(d->dialog));if(path){dove_fd_go(d,path);g_free(path);}return TRUE;}
 if((event->state&GDK_CONTROL_MASK)&&(event->keyval==GDK_l||event->keyval==GDK_L)){gtk_widget_grab_focus(d->address);gtk_editable_select_region(GTK_EDITABLE(d->address),0,-1);return TRUE;}
 if(tool){if(gtk_widget_get_sensitive(tool))dove_fd_action(tool,d);return TRUE;}return FALSE;
}
static inline GtkWidget *dove_filedialog_action(GtkWindow *parent,const char *title,GtkFileChooserAction action){
 GtkWidget *dialog=gtk_file_chooser_dialog_new(title,parent,action,GTK_STOCK_CANCEL,GTK_RESPONSE_CANCEL,action==GTK_FILE_CHOOSER_ACTION_SAVE?GTK_STOCK_SAVE:GTK_STOCK_OPEN,GTK_RESPONSE_ACCEPT,NULL);
 gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(dialog),TRUE);if(action==GTK_FILE_CHOOSER_ACTION_SAVE)gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),TRUE);
 DoveFileDialog *d=g_new0(DoveFileDialog,1);d->dialog=dialog;d->action=action;d->at=-1;d->requested=-1;d->history=g_ptr_array_new_with_free_func(g_free);g_object_set_data_full(G_OBJECT(dialog),"dove-file-dialog",d,dove_fd_free);
 GtkWidget *content=gtk_dialog_get_content_area(GTK_DIALOG(dialog));GtkWidget *chooser=dove_fd_find(content,"GtkFileChooserWidget"),*paned=dove_fd_find(chooser,"GtkHPaned"),*pathbar=dove_fd_find(chooser,"GtkPathBar");
 if(!chooser||!paned||(action==GTK_FILE_CHOOSER_ACTION_SAVE&&!dove_fd_find(chooser,"GtkFileChooserEntry"))){gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),"/media/usb0");return dialog;}
 /* GTK2 restores its saved chooser-only size after map. Our additional rows
  * need the dialog's normal requisition instead; leave all selection handlers. */
 guint size_signal=g_signal_lookup("default-size-changed",G_OBJECT_TYPE(chooser));if(size_signal)g_signal_handlers_disconnect_matched(chooser,G_SIGNAL_MATCH_ID|G_SIGNAL_MATCH_DATA,size_signal,0,NULL,NULL,dialog);
 /* Preserve internal widgets and signals; only replace their visible layout. */
 if(pathbar)dove_fd_hide(gtk_widget_get_parent(pathbar));
 GtkWidget *bar=dove_toolbar();gtk_toolbar_set_show_arrow(GTK_TOOLBAR(bar),FALSE);gtk_box_pack_start(GTK_BOX(content),bar,FALSE,FALSE,0);gtk_box_reorder_child(GTK_BOX(content),bar,0);
 d->back=dove_fd_tool(d,bar,"Back",GTK_STOCK_GO_BACK,"go-previous.png","back");d->forward=dove_fd_tool(d,bar,"Forward",GTK_STOCK_GO_FORWARD,"go-next.png","forward");d->up=dove_fd_tool(d,bar,"Up",GTK_STOCK_GO_UP,"go-up.png","up");gtk_toolbar_insert(GTK_TOOLBAR(bar),gtk_separator_tool_item_new(),-1);dove_fd_tool(d,bar,"Refresh",GTK_STOCK_REFRESH,"view-refresh.png","refresh");dove_fd_tool(d,bar,"New Folder",GTK_STOCK_DIRECTORY,"folder-new.png","new");
 GtkWidget *address=gtk_hbox_new(FALSE,6);gtk_box_pack_start(GTK_BOX(address),gtk_label_new("Address:"),FALSE,FALSE,0);d->address=gtk_entry_new();gtk_box_pack_start(GTK_BOX(address),d->address,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(content),address,FALSE,FALSE,3);gtk_box_reorder_child(GTK_BOX(content),address,1);g_signal_connect(d->address,"activate",G_CALLBACK(dove_fd_address),d);
 GtkListStore *places=gtk_list_store_new(3,GDK_TYPE_PIXBUF,G_TYPE_STRING,G_TYPE_STRING);const char *labels[]={"Desktop","Media","Documents","Downloads","USB Drive","File System"};const char *icons[]={"user-desktop","folder-music","folder-documents","folder-download","drive-removable-media","computer"};gchar *base=g_strdup(g_file_test("/media/usb0/DrMoSa",G_FILE_TEST_IS_DIR)?"/media/usb0/DrMoSa":g_get_home_dir());
 for(int i=0;i<6;i++){gchar *folder=i==0?g_build_filename(g_get_home_dir(),"Desktop",NULL):i==4?g_strdup("/media/usb0"):i==5?g_strdup("/"):g_build_filename(base,labels[i],NULL);GdkPixbuf *pix=gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),icons[i],16,GTK_ICON_LOOKUP_USE_BUILTIN,NULL);GtkTreeIter row;gtk_list_store_insert_with_values(places,&row,-1,0,pix,1,labels[i],2,folder,-1);if(pix)g_object_unref(pix);g_free(folder);}
 d->places=gtk_tree_view_new_with_model(GTK_TREE_MODEL(places));g_object_unref(places);GtkTreeViewColumn *column=gtk_tree_view_column_new();GtkCellRenderer *pix=gtk_cell_renderer_pixbuf_new(),*text=gtk_cell_renderer_text_new();gtk_tree_view_column_pack_start(column,pix,FALSE);gtk_tree_view_column_add_attribute(column,pix,"pixbuf",0);gtk_tree_view_column_pack_start(column,text,TRUE);gtk_tree_view_column_add_attribute(column,text,"text",1);gtk_tree_view_append_column(GTK_TREE_VIEW(d->places),column);gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(d->places),FALSE);
 if(paned){GtkWidget *left=gtk_paned_get_child1(GTK_PANED(paned));GList *children=gtk_container_get_children(GTK_CONTAINER(left));for(GList *i=children;i;i=i->next)dove_fd_hide(i->data);g_list_free(children);GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);gtk_container_add(GTK_CONTAINER(scroll),d->places);gtk_box_pack_start(GTK_BOX(left),scroll,TRUE,TRUE,0);gtk_widget_set_size_request(scroll,150,-1);gtk_paned_set_position(GTK_PANED(paned),154);gtk_widget_show_all(scroll);d->list=dove_fd_find(gtk_paned_get_child2(GTK_PANED(paned)),"GtkTreeView");}
 g_signal_connect(d->places,"row-activated",G_CALLBACK(dove_fd_place),d);g_signal_connect(d->places,"button-press-event",G_CALLBACK(dove_fd_place_click),d);
 if(action!=GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER){
  GtkWidget *row=gtk_hbox_new(FALSE,6);gtk_box_pack_start(GTK_BOX(row),gtk_label_new("File name:"),FALSE,FALSE,0);
  d->name=action==GTK_FILE_CHOOSER_ACTION_SAVE?dove_fd_find(chooser,"GtkFileChooserEntry"):gtk_entry_new();
  if(action==GTK_FILE_CHOOSER_ACTION_SAVE&&d->name){GtkWidget *table=gtk_widget_get_parent(d->name);g_object_ref(d->name);gtk_container_remove(GTK_CONTAINER(table),d->name);gtk_box_pack_start(GTK_BOX(row),d->name,TRUE,TRUE,0);g_object_unref(d->name);dove_fd_hide(table);}
  else{gtk_box_pack_start(GTK_BOX(row),d->name,TRUE,TRUE,0);g_signal_connect(d->name,"changed",G_CALLBACK(dove_fd_name),d);g_signal_connect(d->name,"activate",G_CALLBACK(dove_fd_name_activate),d);g_signal_connect(dialog,"selection-changed",G_CALLBACK(dove_fd_selection),d);}
  gtk_box_pack_start(GTK_BOX(content),row,FALSE,FALSE,4);gtk_box_reorder_child(GTK_BOX(content),chooser,2);gtk_box_reorder_child(GTK_BOX(content),row,3);gtk_widget_show_all(row);
 }
 gtk_widget_show_all(bar);gtk_widget_show_all(address);
 g_signal_connect(dialog,"current-folder-changed",G_CALLBACK(dove_fd_folder),d);g_signal_connect(dialog,"key-press-event",G_CALLBACK(dove_fd_key),d);gtk_window_set_default_size(GTK_WINDOW(dialog),740,500);
 gchar *initial=g_build_filename(base,"Documents",NULL);gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),g_file_test(initial,G_FILE_TEST_IS_DIR)?initial:"/media/usb0");g_free(initial);g_free(base);gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT);gtk_window_resize(GTK_WINDOW(dialog),740,500);return dialog;
}
static inline GtkWidget *dove_filedialog(GtkWindow *parent,const char *title,gboolean save){return dove_filedialog_action(parent,title,save?GTK_FILE_CHOOSER_ACTION_SAVE:GTK_FILE_CHOOSER_ACTION_OPEN);}
static inline GtkWidget *dove_openfiledialog(GtkWindow *parent,const char *title){return dove_filedialog(parent,title,FALSE);}
static inline GtkWidget *dove_savefiledialog(GtkWindow *parent,const char *title){return dove_filedialog(parent,title,TRUE);}
static inline GtkWidget *dove_folderdialog(GtkWindow *parent,const char *title){return dove_filedialog_action(parent,title,GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);}
#endif
