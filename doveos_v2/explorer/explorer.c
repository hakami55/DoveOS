#define _GNU_SOURCE
#include "doveos.h"
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

/* Native SDK controls, asynchronous directory reads and file operations.
 * PCManFM continues to own desktop icons and the existing MIME handlers. */
enum { ICON, NAME, SIZE, TYPE, MODIFIED, PATH, DIRECTORY, BYTES, MTIME, COLS };
typedef struct {
 GtkWidget *window, *address, *search, *details, *icons, *views, *status, *places;
 GtkListStore *store; GtkTreeModelFilter *filter; GtkTreeModel *model;
 GtkToolItem *back, *forward, *up;
 gchar *current, *roots[6]; GPtrArray *history; gint history_at;
 GCancellable *cancel; GFileMonitor *monitor; guint generation, refresh_timer;
 GHashTable *icon_cache; GPtrArray *clipboard; gboolean cut, hidden, icon_view, closed, operating, loading;
 guint count; guint64 bytes;
} Explorer;
static Explorer app;
typedef struct { guint generation; gchar *path; gint history; GCancellable *cancel; GFileEnumerator *enumerator; } Load;
typedef struct { gchar *kind, *destination, *name; GPtrArray *sources; } Operation;
static void navigate(const gchar *path, gint history);
static void action(GtkWidget *widget, gpointer data);

static void notice(const gchar *text) { dove_message(GTK_WINDOW(app.window),text); }
static void error_message(const gchar *prefix, GError *error) {
 gchar *text=g_strdup_printf("%s\n\n%s",prefix,error?error->message:"Unknown error"); notice(text);g_free(text);
}
static void status(void) {
 gchar *size=g_format_size(app.bytes);
 gchar *text=g_strdup_printf("%u items  |  %s%s%s",app.count,size,app.operating?"  |  Working...":"",app.clipboard->len?(app.cut?"  |  Items ready to move":"  |  Items ready to copy"):"");
 dove_status(app.status,text);g_free(text);g_free(size);
}
static GdkPixbuf *icon(const gchar *name, gint size) {
 gchar *key=g_strdup_printf("%s:%d",name,size);GdkPixbuf *pix=g_hash_table_lookup(app.icon_cache,key);
 if(!pix) {
  pix=gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),name,size,GTK_ICON_LOOKUP_USE_BUILTIN,NULL);
  if(!pix) pix=gtk_widget_render_icon(app.window,GTK_STOCK_FILE,size==16?GTK_ICON_SIZE_MENU:GTK_ICON_SIZE_DND,NULL);
  if(pix) g_hash_table_insert(app.icon_cache,key,pix);else g_free(key);
 } else g_free(key);
 return pix;
}
static gboolean visible(GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
 (void)data;gchar *name=NULL;gtk_tree_model_get(model,iter,NAME,&name,-1);
 if(!name)return FALSE;
 const gchar *query=gtk_entry_get_text(GTK_ENTRY(app.search));
 gboolean ok=app.hidden||name[0]!='.';
 if(ok&&*query) {gchar *a=g_utf8_casefold(name,-1),*b=g_utf8_casefold(query,-1);ok=strstr(a,b)!=NULL;g_free(a);g_free(b);}
 g_free(name);return ok;
}
static gint sort_name(GtkTreeModel *model,GtkTreeIter *a,GtkTreeIter *b,gpointer data) {
 (void)data;gboolean ad,bd;gchar *an,*bn;
 gtk_tree_model_get(model,a,DIRECTORY,&ad,NAME,&an,-1);gtk_tree_model_get(model,b,DIRECTORY,&bd,NAME,&bn,-1);
 gint result=ad!=bd?(ad?-1:1):g_utf8_collate(an,bn);g_free(an);g_free(bn);return result;
}
static void load_free(Load *load) {
 if(load->enumerator){g_object_unref(load->enumerator);}g_object_unref(load->cancel);g_free(load->path);g_free(load);
}
static gboolean current_load(Load *load) { return !app.closed&&load->generation==app.generation; }
static gboolean refresh_later(gpointer data) {
 (void)data;app.refresh_timer=0;if(app.current)navigate(app.current,-2);return FALSE;
}
static void changed(GFileMonitor *monitor,GFile *file,GFile *other,GFileMonitorEvent event,gpointer data) {
 (void)monitor;(void)file;(void)other;(void)event;(void)data;
 if(!app.closed&&!app.refresh_timer)app.refresh_timer=dove_timer(350,refresh_later,NULL);
}
static void watch_folder(void) {
 if(app.monitor){g_file_monitor_cancel(app.monitor);g_object_unref(app.monitor);app.monitor=NULL;}
 GFile *file=g_file_new_for_path(app.current);app.monitor=g_file_monitor_directory(file,G_FILE_MONITOR_NONE,NULL,NULL);g_object_unref(file);
 if(app.monitor)g_signal_connect(app.monitor,"changed",G_CALLBACK(changed),NULL);
}
static void finish_load(Load *load,GError *error) {
 if(current_load(load)) {
  app.loading=FALSE;
  dove_pointer(app.window,GDK_LEFT_PTR);
  if(error&&!g_error_matches(error,G_IO_ERROR,G_IO_ERROR_CANCELLED))error_message("Could not read this folder.",error);
  else {status();watch_folder();}
  gtk_entry_set_text(GTK_ENTRY(app.address),app.current?app.current:"/media/usb0");
 }
 if(error){g_error_free(error);}load_free(load);
}
static void next_files(GObject *source,GAsyncResult *result,gpointer data) {
 Load *load=data;GError *error=NULL;GList *files=g_file_enumerator_next_files_finish(G_FILE_ENUMERATOR(source),result,&error);
 if(!current_load(load)||error||!files) {g_list_free_full(files,g_object_unref);finish_load(load,error);return;}
 for(GList *it=files;it;it=it->next) {
  GFileInfo *info=it->data;const gchar *name=g_file_info_get_name(info);if(!name)continue;
  gboolean dir=g_file_info_get_file_type(info)==G_FILE_TYPE_DIRECTORY;
  gchar *path=g_build_filename(app.current,name,NULL);
  /* Follow a directory link only when opened; never recurse through it on copy. */
  if(g_file_info_get_is_symlink(info))dir=g_file_test(path,G_FILE_TEST_IS_DIR);
  guint64 bytes=g_file_info_get_size(info),stamp=g_file_info_get_attribute_uint64(info,G_FILE_ATTRIBUTE_TIME_MODIFIED);
  gchar *size=dir?g_strdup(""):g_format_size(bytes);const gchar *content=g_file_info_get_content_type(info);
  gchar *type=dir?g_strdup("File folder"):(g_file_info_get_is_symlink(info)?g_strdup("Shortcut"):content?g_content_type_get_description(content):g_strdup("File"));
  time_t t=(time_t)stamp;struct tm tm;gchar date[64]="";if(localtime_r(&t,&tm))strftime(date,sizeof date,"%Y-%m-%d %H:%M",&tm);
  const gchar *image=dir?"folder":content&&g_str_has_prefix(content,"image/")?"image-x-generic":content&&g_str_has_prefix(content,"audio/")?"audio-x-generic":content&&g_str_has_prefix(content,"video/")?"video-x-generic":"text-x-generic";
  GtkTreeIter row;gtk_list_store_insert_with_values(app.store,&row,-1,ICON,icon(image,app.icon_view?32:16),NAME,g_file_info_get_display_name(info),SIZE,size,TYPE,type,MODIFIED,date,PATH,path,DIRECTORY,dir,BYTES,bytes,MTIME,stamp,-1);
  app.count++;if(!dir)app.bytes+=bytes;g_free(size);g_free(type);g_free(path);
 }
 g_list_free_full(files,g_object_unref);
 g_file_enumerator_next_files_async(load->enumerator,64,G_PRIORITY_DEFAULT,load->cancel,next_files,load);
}
static void enumerated(GObject *source,GAsyncResult *result,gpointer data) {
 Load *load=data;GError *error=NULL;load->enumerator=g_file_enumerate_children_finish(G_FILE(source),result,&error);
 if(!current_load(load)||!load->enumerator){finish_load(load,error);return;}
 gboolean different=g_strcmp0(app.current,load->path)!=0;
 g_free(app.current);app.current=g_strdup(load->path);gtk_list_store_clear(app.store);app.count=0;app.bytes=0;
 if(different)gtk_entry_set_text(GTK_ENTRY(app.search),"");
 if(load->history==-1) {
  if(app.history_at<0||g_strcmp0(g_ptr_array_index(app.history,app.history_at),app.current)) {
   while((gint)app.history->len>app.history_at+1)g_ptr_array_remove_index(app.history,app.history->len-1);
   g_ptr_array_add(app.history,g_strdup(app.current));if(app.history->len>100)g_ptr_array_remove_index(app.history,0);
   app.history_at=(gint)app.history->len-1;
  }
 } else if(load->history>=0)app.history_at=load->history;
 gtk_widget_set_sensitive(GTK_WIDGET(app.back),app.history_at>0);
 gtk_widget_set_sensitive(GTK_WIDGET(app.forward),app.history_at+1<(gint)app.history->len);
 gtk_widget_set_sensitive(GTK_WIDGET(app.up),strcmp(app.current,"/")!=0);
 gtk_entry_set_text(GTK_ENTRY(app.address),app.current);
 gchar *base=g_path_get_basename(app.current),*title=g_strdup_printf("%s - DoveOS Explorer",base);
 gtk_window_set_title(GTK_WINDOW(app.window),title);g_free(base);g_free(title);
 GtkTreeModel *places=gtk_tree_view_get_model(GTK_TREE_VIEW(app.places));GtkTreeIter row;gboolean valid=gtk_tree_model_get_iter_first(places,&row);
 gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(app.places)));
 while(valid) {gchar *path;gtk_tree_model_get(places,&row,2,&path,-1);if(!strcmp(path,app.current))gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(app.places)),&row);g_free(path);valid=gtk_tree_model_iter_next(places,&row);}
 g_file_enumerator_next_files_async(load->enumerator,64,G_PRIORITY_DEFAULT,load->cancel,next_files,load);
}
static void navigate(const gchar *path,gint history) {
 if(!path||!*path||app.closed)return;
 gchar *full=g_canonicalize_filename(path,app.current?app.current:"/");
 if(app.cancel){g_cancellable_cancel(app.cancel);g_object_unref(app.cancel);}app.cancel=g_cancellable_new();app.generation++;app.loading=TRUE;
 if(app.refresh_timer){dove_timer_stop(app.refresh_timer);app.refresh_timer=0;}
 if(app.monitor){g_file_monitor_cancel(app.monitor);g_object_unref(app.monitor);app.monitor=NULL;}
 Load *load=g_new0(Load,1);load->generation=app.generation;load->path=full;load->history=history;load->cancel=g_object_ref(app.cancel);
 dove_status(app.status,"Reading folder...");dove_pointer(app.window,GDK_WATCH);
 GFile *file=g_file_new_for_path(full);
 g_file_enumerate_children_async(file,"standard::name,standard::display-name,standard::type,standard::is-symlink,standard::size,standard::content-type,time::modified",G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,G_PRIORITY_DEFAULT,load->cancel,enumerated,load);g_object_unref(file);
}
static GPtrArray *selected(void) {
 GPtrArray *paths=g_ptr_array_new_with_free_func(g_free);GList *rows=app.icon_view?gtk_icon_view_get_selected_items(GTK_ICON_VIEW(app.icons)):gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(app.details)),NULL);
 for(GList *it=rows;it;it=it->next) {GtkTreeIter row;gchar *path=NULL;if(gtk_tree_model_get_iter(app.model,&row,it->data)){gtk_tree_model_get(app.model,&row,PATH,&path,-1);g_ptr_array_add(paths,path);}}
 g_list_free_full(rows,(GDestroyNotify)gtk_tree_path_free);return paths;
}
static void open_path(const gchar *path) {
 if(g_file_test(path,G_FILE_TEST_IS_DIR)){navigate(path,-1);return;}
 GError *error=NULL;gboolean ok=FALSE;
 if(g_str_has_suffix(path,".desktop")) {
  GDesktopAppInfo *desktop=g_desktop_app_info_new_from_filename(path);
  if(desktop){ok=g_app_info_launch(G_APP_INFO(desktop),NULL,NULL,&error);g_object_unref(desktop);}
 } else {
  gchar *uri=g_filename_to_uri(path,NULL,&error);
  if(uri){ok=g_app_info_launch_default_for_uri(uri,NULL,&error);g_free(uri);}
 }
 if(!ok){
  /* The base image has no xdg-open executable. libfm handles file types
   * without a registered GIO default (including its Open With dialog). */
  g_clear_error(&error);gchar *content=g_content_type_guess(path,NULL,0,NULL);
  gchar *argv[]={content&&g_str_has_prefix(content,"text/")?"/usr/bin/xedit":"/usr/bin/pcmanfm",(gchar*)path,NULL};
  if(!g_spawn_async(NULL,argv,NULL,0,NULL,NULL,NULL,&error)){error_message("Could not open the file.",error);g_error_free(error);}
  g_free(content);
 }
}
static void open_selected(void) {
 GPtrArray *paths=selected();if(paths->len==1)open_path(g_ptr_array_index(paths,0));else if(paths->len>1)notice("Select one file or folder to open.");g_ptr_array_unref(paths);
}
static void activated(GtkTreeView *tree,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {(void)tree;(void)path;(void)column;(void)data;open_selected();}
static void icon_activated(GtkIconView *view,GtkTreePath *path,gpointer data) {(void)view;(void)data;GtkTreeIter row;gchar *name=NULL;if(gtk_tree_model_get_iter(app.model,&row,path)){gtk_tree_model_get(app.model,&row,PATH,&name,-1);open_path(name);g_free(name);}}
static void place_activated(GtkTreeView *tree,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)column;(void)data;GtkTreeIter row;gchar *name=NULL;GtkTreeModel *model=gtk_tree_view_get_model(tree);if(gtk_tree_model_get_iter(model,&row,path)){gtk_tree_model_get(model,&row,2,&name,-1);navigate(name,-1);g_free(name);}
}
static gboolean place_click(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)data;if(event->button!=1||event->type!=GDK_BUTTON_PRESS)return FALSE;GtkTreePath *path=NULL;
 if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),(gint)event->x,(gint)event->y,&path,NULL,NULL,NULL)){place_activated(GTK_TREE_VIEW(widget),path,NULL,NULL);gtk_tree_path_free(path);}return FALSE;
}
static void address_enter(GtkEntry *entry,gpointer data) {(void)data;navigate(gtk_entry_get_text(entry),-1);}
static void search_changed(GtkEntry *entry,gpointer data) {(void)entry;(void)data;gtk_tree_model_filter_refilter(app.filter);}
static gchar *prompt(const gchar *title,const gchar *value) {
 GtkWidget *dialog=gtk_dialog_new_with_buttons(title,GTK_WINDOW(app.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,GTK_STOCK_CANCEL,GTK_RESPONSE_CANCEL,GTK_STOCK_OK,GTK_RESPONSE_OK,NULL);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog)),*entry=dove_textbox(value);gtk_container_set_border_width(GTK_CONTAINER(box),12);dove_add(box,dove_label("Name:"));dove_add(box,entry);
 gtk_entry_set_activates_default(GTK_ENTRY(entry),TRUE);gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_OK);gtk_widget_show_all(dialog);gtk_widget_grab_focus(entry);gtk_editable_select_region(GTK_EDITABLE(entry),0,-1);
 gchar *name=NULL;if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_OK)name=g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));gtk_widget_destroy(dialog);
 if(name&&(!*name||!strcmp(name,".")||!strcmp(name,"..")||strchr(name,'/'))) {notice("Use a non-empty name without a slash.");g_free(name);name=NULL;}return name;
}
/* All traversal/copying runs outside GTK's loop, refuses replacement, and
 * copies symbolic links as links. A failed partial copy is left for review. */
static gboolean copy_tree(GFile *source,GFile *dest,GError **error) {
 GFileInfo *info=g_file_query_info(source,G_FILE_ATTRIBUTE_STANDARD_TYPE,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,error);if(!info)return FALSE;
 gboolean dir=g_file_info_get_file_type(info)==G_FILE_TYPE_DIRECTORY;g_object_unref(info);
 if(!dir)return g_file_copy(source,dest,G_FILE_COPY_NOFOLLOW_SYMLINKS,NULL,NULL,NULL,error);
 if(!g_file_make_directory(dest,NULL,error))return FALSE;
 GFileEnumerator *items=g_file_enumerate_children(source,G_FILE_ATTRIBUTE_STANDARD_NAME,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,error);if(!items)return FALSE;
 gboolean ok=TRUE;
 while(ok) {info=g_file_enumerator_next_file(items,NULL,error);if(!info){if(*error)ok=FALSE;break;}GFile *s=g_file_get_child(source,g_file_info_get_name(info)),*d=g_file_get_child(dest,g_file_info_get_name(info));ok=copy_tree(s,d,error);g_object_unref(s);g_object_unref(d);g_object_unref(info);}
 g_object_unref(items);return ok;
}
static gboolean inside_source(GFile *source,GFile *dest) {
 if(g_file_query_file_type(source,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL)!=G_FILE_TYPE_DIRECTORY)return FALSE;
 gchar *source_path=g_file_get_path(source),*dest_path=g_file_get_path(dest),*parent=g_path_get_dirname(dest_path),*base=g_path_get_basename(dest_path);
 char *resolved_source=realpath(source_path,NULL),*resolved_parent=realpath(parent,NULL);gboolean inside=FALSE;
 if(resolved_source&&resolved_parent){gchar *resolved_dest=g_build_filename(resolved_parent,base,NULL);GFile *s=g_file_new_for_path(resolved_source),*d=g_file_new_for_path(resolved_dest);inside=g_file_equal(s,d)||g_file_has_prefix(d,s);g_object_unref(s);g_object_unref(d);g_free(resolved_dest);}
 free(resolved_source);free(resolved_parent);g_free(source_path);g_free(dest_path);g_free(parent);g_free(base);return inside;
}
static void operation_worker(GTask *task,gpointer source,gpointer task_data,GCancellable *cancel) {
 (void)source;(void)cancel;Operation *op=task_data;GError *error=NULL;gboolean ok=TRUE;
 GFile *destdir=g_file_new_for_path(op->destination);
 if(!strcmp(op->kind,"new")) {GFile *dest=g_file_get_child(destdir,op->name);ok=g_file_make_directory(dest,NULL,&error);g_object_unref(dest);}
 else for(guint i=0;ok&&i<op->sources->len;i++) {
  GFile *src=g_file_new_for_path(g_ptr_array_index(op->sources,i));gchar *base=op->name?g_strdup(op->name):g_file_get_basename(src);GFile *dest=g_file_get_child(destdir,base);g_free(base);
  if(!strcmp(op->kind,"trash"))ok=g_file_trash(src,NULL,&error);
  else if(g_file_equal(src,dest)||g_file_has_prefix(dest,src)||inside_source(src,dest)) {g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Choose a different destination outside the source folder.");ok=FALSE;}
  else if(!strcmp(op->kind,"copy"))ok=copy_tree(src,dest,&error);
  else ok=g_file_move(src,dest,G_FILE_COPY_NOFOLLOW_SYMLINKS,NULL,NULL,NULL,&error);
  g_object_unref(src);g_object_unref(dest);
 }
 g_object_unref(destdir);if(ok)g_task_return_boolean(task,TRUE);else g_task_return_error(task,error);
}
static void operation_free(gpointer data) {Operation *op=data;g_free(op->kind);g_free(op->destination);g_free(op->name);g_ptr_array_unref(op->sources);g_free(op);}
static void operation_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source;(void)data;GError *error=NULL;gboolean ok=g_task_propagate_boolean(G_TASK(result),&error);Operation *op=g_task_get_task_data(G_TASK(result));if(app.closed){g_clear_error(&error);return;}
 app.operating=FALSE;if(!ok){error_message("Operation could not finish. Existing files were not replaced.\nA partial copy may remain; review the destination.",error);g_error_free(error);}
 else if(!strcmp(op->kind,"move")){g_ptr_array_set_size(app.clipboard,0);app.cut=FALSE;}
 if(app.current)navigate(app.current,-2);
}
static void operate(const gchar *kind,GPtrArray *sources,const gchar *name) {
 if(app.operating){notice("Please wait for the current file operation to finish.");g_ptr_array_unref(sources);return;}
 if(!app.current){g_ptr_array_unref(sources);return;}
 Operation *op=g_new0(Operation,1);op->kind=g_strdup(kind);op->destination=g_strdup(app.current);op->name=g_strdup(name);op->sources=sources;
 GTask *task=g_task_new(NULL,NULL,operation_done,NULL);g_task_set_task_data(task,op,operation_free);app.operating=TRUE;status();g_task_run_in_thread(task,operation_worker);g_object_unref(task);
}
static void properties(void) {
 GPtrArray *paths=selected();const gchar *path=paths->len==1?g_ptr_array_index(paths,0):app.current;
 if(path){GFile *f=g_file_new_for_path(path);GError *error=NULL;GFileInfo *info=g_file_query_info(f,"standard::type,standard::size,time::modified,access::can-write",G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,&error);
 if(info){gchar *size=g_format_size(g_file_info_get_size(info));gchar *text=g_strdup_printf("Location: %s\nType: %s\nSize: %s\nWritable: %s",path,g_file_info_get_file_type(info)==G_FILE_TYPE_DIRECTORY?"Folder":"File",size,g_file_info_get_attribute_boolean(info,G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE)?"Yes":"No");notice(text);g_free(text);g_free(size);g_object_unref(info);}else{error_message("Could not read properties.",error);g_clear_error(&error);}g_object_unref(f);}g_ptr_array_unref(paths);
}
static void action(GtkWidget *widget,gpointer data) {
 (void)widget;const gchar *cmd=data;
 if(!strcmp(cmd,"back")&&app.history_at>0)navigate(g_ptr_array_index(app.history,app.history_at-1),app.history_at-1);
 else if(!strcmp(cmd,"forward")&&app.history_at+1<(gint)app.history->len)navigate(g_ptr_array_index(app.history,app.history_at+1),app.history_at+1);
 else if(!strcmp(cmd,"up")&&app.current){gchar *parent=g_path_get_dirname(app.current);navigate(parent,-1);g_free(parent);}
 else if(!strcmp(cmd,"refresh")&&app.current)navigate(app.current,-2);
 else if(!strcmp(cmd,"open"))open_selected();
 else if(!strcmp(cmd,"close")){if(app.operating)notice("Please wait for the file operation to finish before closing Explorer.");else gtk_widget_destroy(app.window);}
 else if(!strcmp(cmd,"new")){gchar *name=prompt("New Folder","New Folder");if(name){operate("new",g_ptr_array_new_with_free_func(g_free),name);g_free(name);}}
 else if(!strcmp(cmd,"rename")){GPtrArray *paths=selected();if(paths->len==1){gchar *base=g_path_get_basename(g_ptr_array_index(paths,0)),*name=prompt("Rename",base);g_free(base);if(name){operate("rename",paths,name);g_free(name);return;}}g_ptr_array_unref(paths);}
 else if(!strcmp(cmd,"copy")||!strcmp(cmd,"cut")){GPtrArray *paths=selected();if(paths->len){g_ptr_array_unref(app.clipboard);app.clipboard=paths;app.cut=!strcmp(cmd,"cut");status();}else g_ptr_array_unref(paths);}
 else if(!strcmp(cmd,"paste")&&app.clipboard->len){GPtrArray *paths=g_ptr_array_new_with_free_func(g_free);for(guint i=0;i<app.clipboard->len;i++)g_ptr_array_add(paths,g_strdup(g_ptr_array_index(app.clipboard,i)));operate(app.cut?"move":"copy",paths,NULL);}
 else if(!strcmp(cmd,"trash")){GPtrArray *paths=selected();if(paths->len){GtkWidget *d=dove_messagebox(GTK_WINDOW(app.window),"Move the selected items to Trash?\nIf this drive does not support Trash, the items will be kept.",GTK_MESSAGE_QUESTION,GTK_BUTTONS_YES_NO);gint answer=gtk_dialog_run(GTK_DIALOG(d));gtk_widget_destroy(d);if(answer==GTK_RESPONSE_YES){operate("trash",paths,NULL);return;}}g_ptr_array_unref(paths);}
 else if(!strcmp(cmd,"properties"))properties();
 else if(!strcmp(cmd,"hidden")){app.hidden=!app.hidden;gtk_tree_model_filter_refilter(app.filter);}
 else if(!strcmp(cmd,"icons")||!strcmp(cmd,"details")){gboolean newview=!strcmp(cmd,"icons");if(newview!=app.icon_view){app.icon_view=newview;gtk_notebook_set_current_page(GTK_NOTEBOOK(app.views),newview?1:0);if(app.current)navigate(app.current,-2);}}
 else if(!strcmp(cmd,"all")){if(app.icon_view)gtk_icon_view_select_all(GTK_ICON_VIEW(app.icons));else gtk_tree_selection_select_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(app.details)));}
 else if(!strcmp(cmd,"about"))notice("DoveOS Explorer\nBuilt with DoveOS SDK " DOVEOS_SDK_VERSION "\n\nDouble-click to open. Alt+Left/Right: Back/Forward\nAlt+Up: Parent folder. Ctrl+L: Address\nCtrl+F: Filter this folder. F5: Refresh\nF2: Rename. Ctrl+C/X/V: Copy/Cut/Paste\n\nPersonal files stay on your USB drive.");
}
static GtkWidget *menu_entry(GtkWidget *menu,const gchar *label,const gchar *command) {
 GtkWidget *item=dove_menuitem(label);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",G_CALLBACK(action),(gpointer)command);return item;
}
static gboolean context(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)data;if(event->button!=3)return FALSE;
 if(widget==app.details){GtkTreePath *path=NULL;if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),(gint)event->x,(gint)event->y,&path,NULL,NULL,NULL)){GtkTreeSelection *s=gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));if(!gtk_tree_selection_path_is_selected(s,path)){gtk_tree_selection_unselect_all(s);gtk_tree_selection_select_path(s,path);}gtk_tree_path_free(path);}}
 else {GtkTreePath *path=gtk_icon_view_get_path_at_pos(GTK_ICON_VIEW(widget),(gint)event->x,(gint)event->y);if(path){if(!gtk_icon_view_path_is_selected(GTK_ICON_VIEW(widget),path)){gtk_icon_view_unselect_all(GTK_ICON_VIEW(widget));gtk_icon_view_select_path(GTK_ICON_VIEW(widget),path);}gtk_tree_path_free(path);}}
 GtkWidget *menu=dove_contextmenu();menu_entry(menu,"_Open","open");menu_entry(menu,"Cu_t","cut");menu_entry(menu,"_Copy","copy");GtkWidget *paste=menu_entry(menu,"_Paste","paste");gtk_widget_set_sensitive(paste,app.clipboard->len>0);menu_entry(menu,"_Rename","rename");menu_entry(menu,"Move to _Trash","trash");menu_entry(menu,"New _Folder","new");menu_entry(menu,"_Properties","properties");
 g_signal_connect_swapped(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),menu);gtk_widget_show_all(menu);gtk_menu_popup(GTK_MENU(menu),NULL,NULL,NULL,NULL,event->button,event->time);return TRUE;
}
static gboolean key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)widget;(void)data;gboolean ctrl=event->state&GDK_CONTROL_MASK,alt=event->state&GDK_MOD1_MASK;const gchar *cmd=NULL;
 if(ctrl&&(event->keyval==GDK_l||event->keyval==GDK_L)){gtk_widget_grab_focus(app.address);gtk_editable_select_region(GTK_EDITABLE(app.address),0,-1);return TRUE;}
 if(ctrl&&(event->keyval==GDK_f||event->keyval==GDK_F)){gtk_widget_grab_focus(app.search);return TRUE;}
 if(alt){if(event->keyval==GDK_Left)cmd="back";if(event->keyval==GDK_Right)cmd="forward";if(event->keyval==GDK_Up)cmd="up";}
 if(event->keyval==GDK_F5)cmd="refresh";
 if(!cmd&&GTK_IS_EDITABLE(gtk_window_get_focus(GTK_WINDOW(app.window))))return FALSE;
 if(ctrl){switch(event->keyval){case GDK_c:case GDK_C:cmd="copy";break;case GDK_x:case GDK_X:cmd="cut";break;case GDK_v:case GDK_V:cmd="paste";break;case GDK_a:case GDK_A:cmd="all";break;default:break;}}
 else {if(event->keyval==GDK_F2)cmd="rename";if(event->keyval==GDK_Delete)cmd="trash";if(event->keyval==GDK_BackSpace)cmd="up";}
 if(cmd){action(NULL,(gpointer)cmd);return TRUE;}return FALSE;
}
static gboolean delete_window(GtkWidget *widget,GdkEvent *event,gpointer data) {(void)widget;(void)event;(void)data;if(app.operating){notice("Please wait for the file operation to finish before closing Explorer.");return TRUE;}return FALSE;}
static void destroy(GtkWidget *widget,gpointer data) {(void)widget;(void)data;app.closed=TRUE;if(app.cancel)g_cancellable_cancel(app.cancel);if(app.refresh_timer)dove_timer_stop(app.refresh_timer);if(app.monitor)g_file_monitor_cancel(app.monitor);if(gtk_main_level())gtk_main_quit();}
/* Reuse Explorer while still honoring a folder requested by a shortcut.
 * The desktop has no session D-Bus, so use a local X11 client message. */
static gboolean reuse(const gchar *path) {
 Display *display=GDK_DISPLAY_XDISPLAY(gdk_display_get_default());Window root=DefaultRootWindow(display),found=None;
 Atom type;int format;unsigned long count,left;unsigned char *data=NULL;
 if(XGetWindowProperty(display,root,XInternAtom(display,"_NET_CLIENT_LIST",False),0,4096,False,XA_WINDOW,&type,&format,&count,&left,&data)==Success&&format==32&&data){
  Window *windows=(Window*)data;for(unsigned long i=0;i<count;i++){XClassHint hint={0};if(XGetClassHint(display,windows[i],&hint)){if(hint.res_name&&!strcmp(hint.res_name,"doveos-explorer"))found=windows[i];if(hint.res_name)XFree(hint.res_name);if(hint.res_class)XFree(hint.res_class);}if(found)break;}
 }
 if(data){XFree(data);}if(!found)return FALSE;
 if(path&&g_path_is_absolute(path)&&strlen(path)<4096){Atom location=XInternAtom(display,"_DOVEOS_EXPLORER_LOCATION",False);XChangeProperty(display,found,location,XInternAtom(display,"UTF8_STRING",False),8,PropModeReplace,(const unsigned char*)path,(int)strlen(path));XEvent event={0};event.xclient.type=ClientMessage;event.xclient.window=found;event.xclient.message_type=XInternAtom(display,"_DOVEOS_EXPLORER_OPEN",False);event.xclient.format=32;XSendEvent(display,found,False,NoEventMask,&event);}
 XEvent activate={0};activate.xclient.type=ClientMessage;activate.xclient.window=found;activate.xclient.message_type=XInternAtom(display,"_NET_ACTIVE_WINDOW",False);activate.xclient.format=32;activate.xclient.data.l[0]=2;activate.xclient.data.l[1]=CurrentTime;XSendEvent(display,root,False,SubstructureRedirectMask|SubstructureNotifyMask,&activate);XMapRaised(display,found);XFlush(display);return TRUE;
}
static GdkFilterReturn request(GdkXEvent *event,GdkEvent *translated,gpointer unused) {
 (void)translated;(void)unused;XEvent *x=event;Display *display=GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
 if(x->type!=ClientMessage||x->xclient.message_type!=XInternAtom(display,"_DOVEOS_EXPLORER_OPEN",False))return GDK_FILTER_CONTINUE;
 Atom type;int format;unsigned long count,left;unsigned char *data=NULL;
 if(XGetWindowProperty(display,x->xclient.window,XInternAtom(display,"_DOVEOS_EXPLORER_LOCATION",False),0,1024,True,XInternAtom(display,"UTF8_STRING",False),&type,&format,&count,&left,&data)==Success&&format==8&&data&&count<4096&&left==0&&data[0]=='/'&&g_utf8_validate((const gchar*)data,(gssize)count,NULL)) {gchar *path=g_strndup((const gchar*)data,count);navigate(path,-1);g_free(path);}
 if(data){XFree(data);}return GDK_FILTER_REMOVE;
}
static GtkToolItem *tool(GtkWidget *bar,const gchar *label,const gchar *stock,const gchar *cmd) {
 const gchar *asset=!strcmp(cmd,"back")?"go-previous":!strcmp(cmd,"forward")?"go-next":!strcmp(cmd,"up")?"go-up":!strcmp(cmd,"refresh")?"view-refresh":!strcmp(cmd,"new")?"folder-new":!strcmp(cmd,"copy")?"edit-copy":"edit-paste";
 const gchar *root=g_getenv("DOVEOS_EXPLORER_ICONS");if(!root)root="/media/usb0/DoveOS-v2/Explorer/toolbar-icons";
 gchar *filename=g_strdup_printf("%s/%s.png",root,asset);
 GdkPixbuf *pixbuf=gdk_pixbuf_new_from_file_at_scale(filename,16,16,TRUE,NULL);g_free(filename);
 if(!pixbuf)pixbuf=gtk_widget_render_icon(app.window,stock,GTK_ICON_SIZE_SMALL_TOOLBAR,NULL);
 GtkToolItem *item=dove_toolbutton(label,NULL);GtkWidget *picture=dove_toolbar_icon(pixbuf);if(pixbuf)g_object_unref(pixbuf);
 gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(item),picture);gtk_tool_item_set_is_important(item,TRUE);gtk_toolbar_insert(GTK_TOOLBAR(bar),item,-1);
 g_object_set_data(G_OBJECT(item),"toolbar-picture",picture);g_object_set_data(G_OBJECT(item),"toolbar-label",(gpointer)label);
 gtk_widget_show_all(GTK_WIDGET(item));dove_tooltip(GTK_WIDGET(item),label);
 g_signal_connect(item,"clicked",G_CALLBACK(action),(gpointer)cmd);return item;
}
static void ui(void) {
 
 app.window=dove_form("DoveOS Explorer",920,570);dove_window_fit(app.window,920,570);gtk_window_set_wmclass(GTK_WINDOW(app.window),"doveos-explorer","DoveExplorer");gtk_window_set_position(GTK_WINDOW(app.window),GTK_WIN_POS_CENTER);gtk_window_set_icon_name(GTK_WINDOW(app.window),"folder");
 app.history=g_ptr_array_new_with_free_func(g_free);app.history_at=-1;app.clipboard=g_ptr_array_new_with_free_func(g_free);app.icon_cache=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_object_unref);
 GtkWidget *box=dove_panel(FALSE,0);gtk_container_add(GTK_CONTAINER(app.window),box);
 GtkWidget *menubar=dove_menubar();gtk_box_pack_start(GTK_BOX(box),menubar,FALSE,FALSE,0);
 const gchar *menus[]={"_File","_Edit","_View","_Help"};GtkWidget *m[4];for(gint i=0;i<4;i++){GtkWidget *item=dove_menuitem(menus[i]);m[i]=dove_contextmenu();gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),m[i]);gtk_menu_shell_append(GTK_MENU_SHELL(menubar),item);}
 menu_entry(m[0],"_Open","open");menu_entry(m[0],"New _Folder","new");menu_entry(m[0],"_Rename","rename");menu_entry(m[0],"Move to _Trash","trash");menu_entry(m[0],"_Properties","properties");menu_entry(m[0],"_Close","close");
 menu_entry(m[1],"Cu_t","cut");menu_entry(m[1],"_Copy","copy");menu_entry(m[1],"_Paste","paste");menu_entry(m[1],"Select _All","all");
 menu_entry(m[2],"_Details","details");menu_entry(m[2],"_Large Icons","icons");GtkWidget *hidden=gtk_check_menu_item_new_with_mnemonic("Show _Hidden Files");gtk_menu_shell_append(GTK_MENU_SHELL(m[2]),hidden);g_signal_connect(hidden,"toggled",G_CALLBACK(action),"hidden");menu_entry(m[2],"_Refresh","refresh");menu_entry(m[3],"_About Explorer","about");
 GtkWidget *bar=dove_toolbar();gtk_toolbar_set_show_arrow(GTK_TOOLBAR(bar),FALSE);gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);app.back=tool(bar,"Back",GTK_STOCK_GO_BACK,"back");app.forward=tool(bar,"Forward",GTK_STOCK_GO_FORWARD,"forward");app.up=tool(bar,"Up",GTK_STOCK_GO_UP,"up");gtk_toolbar_insert(GTK_TOOLBAR(bar),gtk_separator_tool_item_new(),-1);tool(bar,"Refresh",GTK_STOCK_REFRESH,"refresh");tool(bar,"New Folder",GTK_STOCK_DIRECTORY,"new");tool(bar,"Copy",GTK_STOCK_COPY,"copy");tool(bar,"Paste",GTK_STOCK_PASTE,"paste");
 GtkWidget *address=dove_panel(TRUE,6);gtk_container_set_border_width(GTK_CONTAINER(address),4);gtk_box_pack_start(GTK_BOX(box),address,FALSE,FALSE,0);dove_add(address,dove_label("Address"));app.address=dove_textbox("");gtk_box_pack_start(GTK_BOX(address),app.address,TRUE,TRUE,0);GtkWidget *go=dove_button("Go");dove_add(address,go);g_signal_connect(app.address,"activate",G_CALLBACK(address_enter),NULL);g_signal_connect_swapped(go,"clicked",G_CALLBACK(gtk_widget_activate),app.address);
 GtkWidget *split=gtk_hpaned_new();gtk_box_pack_start(GTK_BOX(box),split,TRUE,TRUE,0);gtk_paned_set_position(GTK_PANED(split),175);
 GtkWidget *left=dove_panel(FALSE,4);gtk_container_set_border_width(GTK_CONTAINER(left),6);gtk_paned_pack1(GTK_PANED(split),left,FALSE,FALSE);dove_add(left,dove_label("Quick access"));
 GtkListStore *places=gtk_list_store_new(3,GDK_TYPE_PIXBUF,G_TYPE_STRING,G_TYPE_STRING);const gchar *labels[]={"Desktop","Media","Documents","Downloads","USB Drive","File System"};const gchar *images[]={"user-desktop","folder-music","folder-documents","folder-download","drive-removable-media","computer"};
 const gchar *base=g_getenv("DOVEOS_PERSONAL_ROOT");if(!base)base="/media/usb0/DrMoSa";
 app.roots[0]=g_build_filename(g_get_home_dir(),"Desktop",NULL);app.roots[1]=g_build_filename(base,"Media",NULL);app.roots[2]=g_build_filename(base,"Documents",NULL);app.roots[3]=g_build_filename(base,"Downloads",NULL);app.roots[4]=g_strdup("/media/usb0");app.roots[5]=g_strdup("/");
 for(gint i=0;i<6;i++){GtkTreeIter row;gtk_list_store_append(places,&row);gtk_list_store_set(places,&row,0,icon(images[i],16),1,labels[i],2,app.roots[i],-1);}
 app.places=gtk_tree_view_new_with_model(GTK_TREE_MODEL(places));g_object_unref(places);GtkTreeViewColumn *col=gtk_tree_view_column_new();GtkCellRenderer *pix=gtk_cell_renderer_pixbuf_new(),*text=gtk_cell_renderer_text_new();gtk_tree_view_column_pack_start(col,pix,FALSE);gtk_tree_view_column_add_attribute(col,pix,"pixbuf",0);gtk_tree_view_column_pack_start(col,text,TRUE);gtk_tree_view_column_add_attribute(col,text,"text",1);gtk_tree_view_append_column(GTK_TREE_VIEW(app.places),col);gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(app.places),FALSE);gtk_tree_view_set_enable_search(GTK_TREE_VIEW(app.places),FALSE);gtk_box_pack_start(GTK_BOX(left),dove_scrolled(app.places,150,-1),TRUE,TRUE,0);g_signal_connect(app.places,"button-press-event",G_CALLBACK(place_click),NULL);g_signal_connect(app.places,"row-activated",G_CALLBACK(place_activated),NULL);
 GtkWidget *right=dove_panel(FALSE,4);gtk_paned_pack2(GTK_PANED(split),right,TRUE,FALSE);GtkWidget *filterrow=dove_panel(TRUE,6);gtk_container_set_border_width(GTK_CONTAINER(filterrow),4);dove_add(filterrow,dove_label("Filter"));app.search=dove_textbox("");dove_tooltip(app.search,"Find a name in the current folder (Ctrl+F)");gtk_box_pack_start(GTK_BOX(filterrow),app.search,TRUE,TRUE,0);GtkWidget *details=dove_button("Details"),*icons=dove_button("Icons");dove_add(filterrow,details);dove_add(filterrow,icons);g_signal_connect(details,"clicked",G_CALLBACK(action),"details");g_signal_connect(icons,"clicked",G_CALLBACK(action),"icons");gtk_box_pack_start(GTK_BOX(right),filterrow,FALSE,FALSE,0);
 app.store=gtk_list_store_new(COLS,GDK_TYPE_PIXBUF,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_BOOLEAN,G_TYPE_UINT64,G_TYPE_UINT64);app.filter=GTK_TREE_MODEL_FILTER(gtk_tree_model_filter_new(GTK_TREE_MODEL(app.store),NULL));gtk_tree_model_filter_set_visible_func(app.filter,visible,NULL,NULL);app.model=gtk_tree_model_sort_new_with_model(GTK_TREE_MODEL(app.filter));gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(app.model),NAME,sort_name,NULL,NULL);gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(app.model),NAME,GTK_SORT_ASCENDING);
 app.details=gtk_tree_view_new_with_model(app.model);gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(app.details),TRUE);gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(app.details)),GTK_SELECTION_MULTIPLE);gtk_tree_view_set_search_column(GTK_TREE_VIEW(app.details),NAME);
 col=gtk_tree_view_column_new();gtk_tree_view_column_set_title(col,"Name");pix=gtk_cell_renderer_pixbuf_new();text=gtk_cell_renderer_text_new();gtk_tree_view_column_pack_start(col,pix,FALSE);gtk_tree_view_column_add_attribute(col,pix,"pixbuf",ICON);gtk_tree_view_column_pack_start(col,text,TRUE);gtk_tree_view_column_add_attribute(col,text,"text",NAME);gtk_tree_view_column_set_sort_column_id(col,NAME);gtk_tree_view_column_set_resizable(col,TRUE);gtk_tree_view_column_set_min_width(col,200);gtk_tree_view_column_set_expand(col,TRUE);gtk_tree_view_append_column(GTK_TREE_VIEW(app.details),col);
 const gchar *titles[]={"Size","Type","Modified"};for(gint i=0;i<3;i++){text=gtk_cell_renderer_text_new();col=gtk_tree_view_column_new_with_attributes(titles[i],text,"text",SIZE+i,NULL);gtk_tree_view_column_set_resizable(col,TRUE);gtk_tree_view_column_set_min_width(col,i==0?75:i==2?140:120);gtk_tree_view_column_set_sort_column_id(col,i==0?BYTES:i==2?MTIME:TYPE);gtk_tree_view_append_column(GTK_TREE_VIEW(app.details),col);}
 app.icons=dove_iconlist(app.model,NAME,ICON);gtk_icon_view_set_selection_mode(GTK_ICON_VIEW(app.icons),GTK_SELECTION_MULTIPLE);gtk_icon_view_set_item_width(GTK_ICON_VIEW(app.icons),110);
 app.views=dove_tabs();gtk_notebook_set_show_tabs(GTK_NOTEBOOK(app.views),FALSE);gtk_notebook_set_show_border(GTK_NOTEBOOK(app.views),FALSE);dove_tab_add(app.views,dove_scrolled(app.details,-1,-1),"Details");dove_tab_add(app.views,dove_scrolled(app.icons,-1,-1),"Icons");gtk_box_pack_start(GTK_BOX(right),app.views,TRUE,TRUE,0);
 app.status=dove_statusbar();gtk_box_pack_end(GTK_BOX(box),app.status,FALSE,FALSE,0);
 g_signal_connect(app.search,"changed",G_CALLBACK(search_changed),NULL);g_signal_connect(app.details,"row-activated",G_CALLBACK(activated),NULL);g_signal_connect(app.icons,"item-activated",G_CALLBACK(icon_activated),NULL);g_signal_connect(app.details,"button-press-event",G_CALLBACK(context),NULL);g_signal_connect(app.icons,"button-press-event",G_CALLBACK(context),NULL);g_signal_connect(app.window,"key-press-event",G_CALLBACK(key),NULL);g_signal_connect(app.window,"delete-event",G_CALLBACK(delete_window),NULL);g_signal_connect(app.window,"destroy",G_CALLBACK(destroy),NULL);
 gtk_widget_show_all(app.window);gdk_window_add_filter(gtk_widget_get_window(app.window),request,NULL);gtk_widget_grab_focus(app.details);
}
int main(int argc,char **argv) {
 gtk_init(&argc,&argv);g_set_application_name("DoveOS Explorer");
 const gchar *path=argc>1?argv[1]:NULL;if(reuse(path))return 0;
 int lock=open("/tmp/doveos-explorer-window.lock",O_CREAT|O_RDWR|O_CLOEXEC,0600);
 if(lock<0)return 2;
 if(flock(lock,LOCK_EX|LOCK_NB)){
  /* Wait only in the newly launched process, before creating its window. */
  for(int i=0;i<50;i++){g_usleep(100000);if(reuse(path)){close(lock);return 0;}}
  g_printerr("Explorer is still starting. Try again shortly.\n");close(lock);return 3;
 }
 if(reuse(path)){close(lock);return 0;}
 ui();navigate(path?path:app.roots[2],-1);gtk_main();close(lock);return 0;
}
