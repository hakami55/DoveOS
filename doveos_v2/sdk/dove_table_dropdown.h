#ifndef DOVEOS_TABLE_DROPDOWN_H
#define DOVEOS_TABLE_DROPDOWN_H
/* SDK 1.1b: row-specific, native GTK2 dropdowns in TreeView tables.
 * Row model columns: label STRING, options GTK_TYPE_TREE_MODEL, enabled BOOLEAN.
 * Options can have any other columns (IDs, icons, etc.). Callback borrows model
 * and iter for the duration of the call; row_path refers to the VIEW model. */
typedef void (*DoveTableDropdownChanged)(GtkTreeView *view,const gchar *row_path,
 GtkTreeModel *options,GtkTreeIter *choice,gpointer user_data);
typedef struct {GtkTreeView *view;GtkTreeViewColumn *column;GtkCellRenderer *combo;gchar *pending_path;
 int options_column,enabled_column;DoveTableDropdownChanged changed;gpointer data;} DoveTableDropdown;
static inline gboolean dove_table_dropdown_popup(gpointer widget){
 if(GTK_IS_COMBO_BOX(widget)&&gtk_widget_get_mapped(GTK_WIDGET(widget)))gtk_combo_box_popup(GTK_COMBO_BOX(widget));
 return FALSE;
}
static inline void dove_table_dropdown_editing(GtkCellRenderer *renderer,GtkCellEditable *editor,gchar *path,gpointer data){
 (void)renderer;(void)path;(void)data;if(GTK_IS_COMBO_BOX(editor))g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,dove_table_dropdown_popup,g_object_ref(editor),g_object_unref);
}
static inline void dove_table_dropdown_data(GtkTreeViewColumn *column,GtkCellRenderer *renderer,GtkTreeModel *model,GtkTreeIter *row,gpointer data){
 (void)column;DoveTableDropdown *d=data;GtkTreeModel *options=NULL;gboolean enabled=TRUE;
 gtk_tree_model_get(model,row,d->options_column,&options,-1);if(d->enabled_column>=0)gtk_tree_model_get(model,row,d->enabled_column,&enabled,-1);
 enabled=enabled&&options&&gtk_tree_model_iter_n_children(options,NULL)>0;
 if(renderer==d->combo)g_object_set(renderer,"model",options,"editable",enabled,"sensitive",enabled,NULL);else g_object_set(renderer,"sensitive",enabled,NULL);
 if(options)g_object_unref(options);
}
typedef struct {GtkTreeView *view;GtkTreeViewColumn *column;GtkTreeModel *options;GtkTreeRowReference *choice;gchar *path;} DoveDropdownDelivery;
static inline void dove_dropdown_destroyed(GtkWidget *widget,gpointer unused){(void)unused;g_object_set_data(G_OBJECT(widget),"dove-dropdown-destroyed",GINT_TO_POINTER(1));}
static inline gboolean dove_dropdown_deliver(gpointer data){
 DoveDropdownDelivery *event=data;DoveTableDropdown *d=g_object_get_data(G_OBJECT(event->column),"dove-table-dropdown");GtkTreePath *path=gtk_tree_row_reference_get_path(event->choice);GtkTreeIter choice;
 if(!g_object_get_data(G_OBJECT(event->view),"dove-dropdown-destroyed")&&path&&gtk_tree_model_get_iter(event->options,&choice,path)&&d->changed)d->changed(event->view,event->path,event->options,&choice,d->data);
 if(path)gtk_tree_path_free(path);
 return FALSE;
}
static inline void dove_dropdown_delivery_free(gpointer data){DoveDropdownDelivery *event=data;gtk_tree_row_reference_free(event->choice);g_object_unref(event->options);g_object_unref(event->column);g_object_unref(event->view);g_free(event->path);g_free(event);}
static inline void dove_table_dropdown_changed(GtkCellRendererCombo *renderer,gchar *path,GtkTreeIter *choice,gpointer data){
 (void)renderer;DoveTableDropdown *d=data;GtkTreeModel *rows=gtk_tree_view_get_model(d->view),*options=NULL;GtkTreeIter row;gboolean enabled=TRUE;
 if(!gtk_tree_model_get_iter_from_string(rows,&row,path))return;
 gtk_tree_model_get(rows,&row,d->options_column,&options,-1);if(d->enabled_column>=0)gtk_tree_model_get(rows,&row,d->enabled_column,&enabled,-1);
 if(enabled&&options&&d->changed){GtkTreePath *choice_path=gtk_tree_model_get_path(options,choice);DoveDropdownDelivery *event=g_new0(DoveDropdownDelivery,1);event->view=g_object_ref(d->view);event->column=g_object_ref(d->column);event->options=g_object_ref(options);event->choice=gtk_tree_row_reference_new(options,choice_path);event->path=g_strdup(path);gtk_tree_path_free(choice_path);g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,dove_dropdown_deliver,event,dove_dropdown_delivery_free);}
 if(options)g_object_unref(options);
}
static inline gboolean dove_table_dropdown_press(GtkWidget *view,GdkEventButton *event,gpointer column){
 if(event->button!=1||event->type!=GDK_BUTTON_PRESS)return FALSE;
 GtkTreePath *path=NULL;GtkTreeViewColumn *hit=NULL;
 if(!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(view),(int)event->x,(int)event->y,&path,&hit,NULL,NULL))return FALSE;
 DoveTableDropdown *d=g_object_get_data(G_OBJECT(column),"dove-table-dropdown");
 if(hit!=d->column){gtk_tree_path_free(path);return FALSE;}
 GtkTreeModel *model=gtk_tree_view_get_model(d->view);GtkTreeIter row;gboolean enabled=TRUE;GtkTreeModel *options=NULL;
 if(gtk_tree_model_get_iter(model,&row,path)){if(d->enabled_column>=0)gtk_tree_model_get(model,&row,d->enabled_column,&enabled,-1);gtk_tree_model_get(model,&row,d->options_column,&options,-1);}else enabled=FALSE;
 enabled=enabled&&options&&gtk_tree_model_iter_n_children(options,NULL)>0;if(options)g_object_unref(options);
 if(enabled){g_free(d->pending_path);d->pending_path=gtk_tree_path_to_string(path);gtk_tree_view_set_cursor_on_cell(d->view,path,d->column,d->combo,FALSE);}
 gtk_tree_path_free(path);return enabled;
}
static inline gboolean dove_table_dropdown_release(GtkWidget *view,GdkEventButton *event,gpointer column){
 DoveTableDropdown *d=g_object_get_data(G_OBJECT(column),"dove-table-dropdown");if(event->button!=1||!d->pending_path)return FALSE;
 GtkTreePath *pending=gtk_tree_path_new_from_string(d->pending_path),*hit=NULL;GtkTreeViewColumn *hit_column=NULL;g_free(d->pending_path);d->pending_path=NULL;
 gboolean same=gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(view),(int)event->x,(int)event->y,&hit,&hit_column,NULL,NULL)&&hit_column==d->column&&!gtk_tree_path_compare(pending,hit);
 if(same)gtk_tree_view_set_cursor_on_cell(d->view,pending,d->column,d->combo,TRUE);
 if(hit)gtk_tree_path_free(hit);
 gtk_tree_path_free(pending);return TRUE;
}
static inline void dove_table_dropdown_free(gpointer data){DoveTableDropdown *d=data;g_free(d->pending_path);g_free(d);}
/* Render the arrow with GTK's own button/arrow theme primitives, so the
 * collapsed table cell has the same raised arrow button as a ComboBox. */
static void dove_dropdown_arrow_render(GtkCellRenderer *renderer,GdkWindow *window,GtkWidget *widget,GdkRectangle *background,GdkRectangle *cell,GdkRectangle *expose,GtkCellRendererState flags){
 GtkCellRendererClass *parent=GTK_CELL_RENDERER_CLASS(g_type_class_peek_parent(G_OBJECT_GET_CLASS(renderer)));GdkRectangle label=*cell;label.width=MAX(1,label.width-24);parent->render(renderer,window,widget,background,&label,expose,flags);
 gboolean sensitive=TRUE;g_object_get(renderer,"sensitive",&sensitive,NULL);GtkStateType state=sensitive?GTK_STATE_NORMAL:GTK_STATE_INSENSITIVE;GtkStyle *style=gtk_widget_get_style(widget);
 int x=cell->x+cell->width-23,y=cell->y+1,width=22,height=MAX(16,cell->height-2);
 gtk_paint_box(style,window,state,GTK_SHADOW_OUT,expose,widget,"button",x,y,width,height);
 gtk_paint_arrow(style,window,state,GTK_SHADOW_NONE,expose,widget,"arrow",GTK_ARROW_DOWN,TRUE,x+(width-9)/2,y+(height-5)/2,9,5);
}
static void dove_dropdown_arrow_class(gpointer klass,gpointer unused){(void)unused;GTK_CELL_RENDERER_CLASS(klass)->render=dove_dropdown_arrow_render;}
static inline GtkCellRenderer *dove_dropdown_arrow(void){
 GType type=g_type_from_name("DoveTableDropdownCombo");if(!type){const GTypeInfo info={sizeof(GtkCellRendererComboClass),NULL,NULL,dove_dropdown_arrow_class,NULL,NULL,sizeof(GtkCellRendererCombo),0,NULL,NULL};type=g_type_register_static(GTK_TYPE_CELL_RENDERER_COMBO,"DoveTableDropdownCombo",&info,0);}
 return g_object_new(type,"height",24,NULL);
}
static inline GtkTreeViewColumn *dove_table_dropdown(GtkWidget *view,const char *title,
 int label_column,int options_column,int enabled_column,int option_label_column,
 DoveTableDropdownChanged changed,gpointer user_data){
 g_return_val_if_fail(GTK_IS_TREE_VIEW(view),NULL);
 DoveTableDropdown *d=g_new0(DoveTableDropdown,1);d->view=GTK_TREE_VIEW(view);d->options_column=options_column;d->enabled_column=enabled_column;d->changed=changed;d->data=user_data;
 d->combo=dove_dropdown_arrow();g_object_set(d->combo,"text-column",option_label_column,"has-entry",FALSE,"xpad",6,"ypad",3,NULL);
 d->column=gtk_tree_view_column_new();gtk_tree_view_column_set_title(d->column,title);gtk_tree_view_column_pack_start(d->column,d->combo,TRUE);gtk_tree_view_column_add_attribute(d->column,d->combo,"text",label_column);
 gtk_tree_view_column_set_cell_data_func(d->column,d->combo,dove_table_dropdown_data,d,NULL);
 g_object_set_data_full(G_OBJECT(d->column),"dove-table-dropdown",d,dove_table_dropdown_free);
 g_signal_connect(d->combo,"editing-started",G_CALLBACK(dove_table_dropdown_editing),d);g_signal_connect(d->combo,"changed",G_CALLBACK(dove_table_dropdown_changed),d);
 g_signal_connect_object(view,"button-press-event",G_CALLBACK(dove_table_dropdown_press),d->column,0);g_signal_connect_object(view,"button-release-event",G_CALLBACK(dove_table_dropdown_release),d->column,0);
 g_signal_connect(view,"destroy",G_CALLBACK(dove_dropdown_destroyed),NULL);gtk_tree_view_append_column(d->view,d->column);return d->column;
}
#endif
