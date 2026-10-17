#ifndef DOVE_VIEWS_H
#define DOVE_VIEWS_H
#include <pango/pango.h>
/* Native columns work with filtered/sorted models and normal GTK signals. */
static inline GtkTreeViewColumn *dove_table_text(GtkWidget *view,const char *title,int model_column,gboolean expand) {
 GtkCellRenderer *cell=gtk_cell_renderer_text_new();
 g_object_set(cell,"ellipsize",PANGO_ELLIPSIZE_END,NULL);
 GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(title,cell,"text",model_column,NULL);
 gtk_tree_view_column_set_resizable(column,TRUE);gtk_tree_view_column_set_expand(column,expand);
 gtk_tree_view_column_set_sizing(column,GTK_TREE_VIEW_COLUMN_FIXED);gtk_tree_view_column_set_fixed_width(column,expand?180:100);
 gtk_tree_view_append_column(GTK_TREE_VIEW(view),column);return column;
}
static inline GtkTreeViewColumn *dove_table_toggle(GtkWidget *view,const char *title,int model_column,GCallback callback,gpointer data) {
 GtkCellRenderer *cell=gtk_cell_renderer_toggle_new();
 g_object_set(cell,"activatable",callback!=NULL,NULL);
 if(callback)g_signal_connect(cell,"toggled",callback,data);
 GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(title,cell,"active",model_column,NULL);
 gtk_tree_view_append_column(GTK_TREE_VIEW(view),column);return column;
}
static inline GtkWidget *dove_iconlist(GtkTreeModel *model,int text_column,int pixbuf_column) {
 GtkWidget *view=gtk_icon_view_new_with_model(model);
 gtk_icon_view_set_text_column(GTK_ICON_VIEW(view),text_column);
 gtk_icon_view_set_pixbuf_column(GTK_ICON_VIEW(view),pixbuf_column);
 gtk_icon_view_set_item_width(GTK_ICON_VIEW(view),96);
 gtk_icon_view_set_selection_mode(GTK_ICON_VIEW(view),GTK_SELECTION_SINGLE);return view;
}
typedef void (*DoveSidebarChanged)(gint id,gpointer data);
typedef struct { DoveSidebarChanged callback;gpointer data; } DoveSidebar;
static inline void dove_sidebar_changed(GtkTreeSelection *selection,gpointer data) {
 DoveSidebar *sidebar=data;GtkTreeModel *model;GtkTreeIter iter;gint id;
 if(gtk_tree_selection_get_selected(selection,&model,&iter)) {
  gtk_tree_model_get(model,&iter,1,&id,-1);if(sidebar->callback)sidebar->callback(id,sidebar->data);
 }
}
static inline GtkWidget *dove_sidebar(DoveSidebarChanged callback,gpointer data) {
 GtkListStore *store=gtk_list_store_new(2,G_TYPE_STRING,G_TYPE_INT);
 GtkWidget *view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));g_object_unref(store);
 GtkTreeViewColumn *column=dove_table_text(view,"Places",0,TRUE);gtk_tree_view_column_set_fixed_width(column,125);
 gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view),FALSE);
 gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),GTK_SELECTION_BROWSE);
 DoveSidebar *sidebar=g_new0(DoveSidebar,1);sidebar->callback=callback;sidebar->data=data;
 g_object_set_data_full(G_OBJECT(view),"dove-sidebar",sidebar,g_free);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),"changed",G_CALLBACK(dove_sidebar_changed),sidebar);
 return view;
}
static inline void dove_sidebar_add(GtkWidget *view,const char *title,gint id) {
 GtkListStore *store=GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(view)));GtkTreeIter iter;
 gtk_list_store_append(store,&iter);gtk_list_store_set(store,&iter,0,title,1,id,-1);
}
static inline gboolean dove_sidebar_select(GtkWidget *view,gint id) {
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));GtkTreeIter iter;gint row;
 for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) {
  gtk_tree_model_get(model,&iter,1,&row,-1);if(row==id){
   GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(view));gtk_tree_selection_select_iter(selection,&iter);
   GtkTreePath *path=gtk_tree_model_get_path(model,&iter);gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(view),path,NULL,FALSE,0,0);gtk_tree_path_free(path);return TRUE;
  }
 }return FALSE;
}
/* Decode at requested bounds instead of retaining a full-resolution photo. */
static inline GtkWidget *dove_picture_fit(const char *file,int width,int height,GError **error) {
 GdkPixbuf *pixbuf=gdk_pixbuf_new_from_file_at_scale(file,MAX(width,1),MAX(height,1),TRUE,error);
 if(!pixbuf){return NULL;}GtkWidget *image=dove_image_stable(gtk_image_new_from_pixbuf(pixbuf));g_object_unref(pixbuf);
 GtkWidget *frame=dove_groupbox(NULL);gtk_frame_set_shadow_type(GTK_FRAME(frame),GTK_SHADOW_IN);
 gtk_container_add(GTK_CONTAINER(frame),image);return frame;
}
#endif
