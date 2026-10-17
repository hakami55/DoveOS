#include "doveos.h"
#include <string.h>
static GtkWidget *window,*view,*editor;
static GtkListStore *rows;
static GtkTreeViewColumn *column;
static int changes,result=1;
static gboolean smoke;
static void changed(GtkTreeView *v,const gchar *path,GtkTreeModel *options,GtkTreeIter *choice,gpointer data){
 (void)v;(void)data;GtkTreeIter row;char *label=NULL;gtk_tree_model_get(options,choice,0,&label,-1);if(gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(rows),&row,path))gtk_list_store_set(rows,&row,1,label,-1);g_free(label);changes++;
}
static void editing(GtkCellRenderer *renderer,GtkCellEditable *editable,gchar *path,gpointer data){(void)renderer;(void)path;(void)data;editor=GTK_WIDGET(editable);}
static void close_window(GtkWidget *w,gpointer data){(void)w;(void)data;gtk_main_quit();}
static gboolean finish_test(gpointer data){
 (void)data;gboolean popup=FALSE;if(editor)g_object_get(editor,"popup-shown",&popup,NULL);
 GdkWindow *native=gtk_widget_get_window(window);int width,height;gdk_drawable_get_size(native,&width,&height);GdkPixbuf *image=gdk_pixbuf_get_from_drawable(NULL,native,NULL,0,0,0,0,width,height);if(image){gdk_pixbuf_save(image,"/tmp/sdk-table-dropdown.png","png",NULL,NULL);g_object_unref(image);}
 if(editor){g_object_ref(editor);gtk_combo_box_set_active(GTK_COMBO_BOX(editor),1);}
 while(gtk_events_pending())gtk_main_iteration();
 GtkTreeIter row;char *label=NULL;gtk_tree_model_get_iter_first(GTK_TREE_MODEL(rows),&row);gtk_tree_model_get(GTK_TREE_MODEL(rows),&row,1,&label,-1);
 gboolean selected=!g_strcmp0(label,"Steam Big Picture");g_free(label);result=popup&&selected&&changes>0?0:1;
 g_print("SDK=%s SINGLE_CLICK_POPUP=%d SELECTION_CALLBACKS=%d SELECTED_CORRECT=%d RESULT=%d\n",DOVEOS_SDK_VERSION,popup,changes,selected,result);
 if(editor){gtk_combo_box_popdown(GTK_COMBO_BOX(editor));g_object_unref(editor);editor=NULL;}
 gtk_widget_destroy(window);return FALSE;
}
static gboolean click_test(gpointer data){
 (void)data;GtkTreePath *path=gtk_tree_path_new_from_indices(0,-1);GdkRectangle area;gtk_tree_view_get_cell_area(GTK_TREE_VIEW(view),path,column,&area);gtk_tree_path_free(path);
 GdkEventButton event;memset(&event,0,sizeof event);event.type=GDK_BUTTON_PRESS;event.button=1;event.window=gtk_tree_view_get_bin_window(GTK_TREE_VIEW(view));event.x=area.x+area.width-12;event.y=area.y+area.height/2;event.time=GDK_CURRENT_TIME;gboolean handled=FALSE;GdkRectangle disabled_area;GtkTreePath *disabled=gtk_tree_path_new_from_indices(2,-1);gtk_tree_view_get_cell_area(GTK_TREE_VIEW(view),disabled,column,&disabled_area);gtk_tree_path_free(disabled);double original_y=event.y;event.y=disabled_area.y+disabled_area.height/2;gboolean disabled_handled=dove_table_dropdown_press(view,&event,column);g_print("DISABLED_ROW_BLOCKED=%d\n",!disabled_handled);event.y=original_y;g_signal_emit_by_name(view,"button-press-event",&event,&handled);event.type=GDK_BUTTON_RELEASE;gboolean released=FALSE;g_signal_emit_by_name(view,"button-release-event",&event,&released);g_print("CLICK_HANDLED=%d RELEASE_HANDLED=%d\n",handled,released);g_timeout_add(300,finish_test,NULL);return FALSE;
}
int main(int argc,char **argv){
 gtk_init(&argc,&argv);smoke=argc>1&&!strcmp(argv[1],"--verify");window=dove_form("SDK 1.1b - Table dropdown",680,330);g_signal_connect(window,"destroy",G_CALLBACK(close_window),NULL);GtkWidget *box=dove_panel(FALSE,8);gtk_container_set_border_width(GTK_CONTAINER(box),12);gtk_container_add(GTK_CONTAINER(window),box);dove_add(box,dove_label("Click the Application arrow once. Each row has its own choices."));
 GtkListStore *first=gtk_list_store_new(1,G_TYPE_STRING),*second=gtk_list_store_new(1,G_TYPE_STRING);GtkTreeIter it;const char *names[]={"Desktop","Steam Big Picture"};for(int i=0;i<2;i++){gtk_list_store_append(first,&it);gtk_list_store_set(first,&it,0,names[i],-1);}gtk_list_store_append(second,&it);gtk_list_store_set(second,&it,0,"Desktop only",-1);
 rows=gtk_list_store_new(4,G_TYPE_STRING,G_TYPE_STRING,GTK_TYPE_TREE_MODEL,G_TYPE_BOOLEAN);gtk_list_store_append(rows,&it);gtk_list_store_set(rows,&it,0,"Gaming PC",1,"Desktop",2,first,3,TRUE,-1);gtk_list_store_append(rows,&it);gtk_list_store_set(rows,&it,0,"Office PC",1,"Desktop only",2,second,3,TRUE,-1);gtk_list_store_append(rows,&it);gtk_list_store_set(rows,&it,0,"Offline PC",1,"Connect first",3,FALSE,-1);g_object_unref(first);g_object_unref(second);
 const char *titles[]={"Computer"};view=dove_listview(GTK_TREE_MODEL(rows),titles,1);gtk_tree_view_column_set_min_width(gtk_tree_view_get_column(GTK_TREE_VIEW(view),0),220);column=dove_table_dropdown(view,"Application",1,2,3,0,changed,NULL);gtk_tree_view_column_set_expand(column,TRUE);GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));g_signal_connect(cells->data,"editing-started",G_CALLBACK(editing),NULL);g_list_free(cells);gtk_box_pack_start(GTK_BOX(box),dove_scrolled(view,-1,200),TRUE,TRUE,0);gtk_widget_show_all(window);if(smoke)g_timeout_add(150,click_test,NULL);gtk_main();g_object_unref(rows);return smoke?result:0;
}
