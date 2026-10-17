#ifndef DOVE_LAYOUT_H
#define DOVE_LAYOUT_H
/* GTK2 containers handle layout; no custom title bar or polling. */
enum { DOVE_SPACE=6, DOVE_MARGIN=10, DOVE_BUTTON_WIDTH=75, DOVE_BUTTON_HEIGHT=23 };
static inline GtkWidget *dove_layout(gboolean horizontal) {
 return dove_panel(horizontal,DOVE_SPACE);
}
static inline void dove_pack(GtkWidget *box,GtkWidget *child,gboolean expand) {
 gtk_box_pack_start(GTK_BOX(box),child,expand,TRUE,0);
}
/* GtkTreeView/TextView/IconView have native scrolling; boxes need a viewport. */
static inline GtkWidget *dove_scroll(GtkWidget *child) {
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);
 gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
 if(GTK_IS_TREE_VIEW(child)||GTK_IS_TEXT_VIEW(child)||GTK_IS_ICON_VIEW(child)||GTK_IS_VIEWPORT(child))
  gtk_container_add(GTK_CONTAINER(scroll),child);
 else gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroll),child);
 return scroll;
}
static inline GtkWidget *dove_page(void) {
 GtkWidget *page=dove_layout(FALSE);
 gtk_container_set_border_width(GTK_CONTAINER(page),DOVE_MARGIN);return page;
}
static inline GtkWidget *dove_fields(void) {
 GtkWidget *table=gtk_table_new(1,2,FALSE);
 gtk_table_set_row_spacings(GTK_TABLE(table),DOVE_SPACE);
 gtk_table_set_col_spacings(GTK_TABLE(table),12);
 g_object_set_data(G_OBJECT(table),"dove-fields-row",GUINT_TO_POINTER(0));return table;
}
static inline void dove_field(GtkWidget *table,const char *label,GtkWidget *control) {
 guint row=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(table),"dove-fields-row"));
 gtk_table_resize(GTK_TABLE(table),row+1,2);
 GtkWidget *caption=gtk_label_new_with_mnemonic(label);gtk_misc_set_alignment(GTK_MISC(caption),0,0.5);
 gtk_label_set_mnemonic_widget(GTK_LABEL(caption),control);
 gtk_table_attach(GTK_TABLE(table),caption,0,1,row,row+1,GTK_FILL,GTK_FILL,0,0);
 gtk_table_attach(GTK_TABLE(table),control,1,2,row,row+1,GTK_EXPAND|GTK_FILL,GTK_FILL,0,0);
 g_object_set_data(G_OBJECT(table),"dove-fields-row",GUINT_TO_POINTER(row+1));
}
static inline GtkWidget *dove_actions(void) {
 GtkWidget *box=gtk_hbutton_box_new();gtk_button_box_set_layout(GTK_BUTTON_BOX(box),GTK_BUTTONBOX_END);
 gtk_box_set_spacing(GTK_BOX(box),DOVE_SPACE);return box;
}
static inline GtkWidget *dove_action(GtkWidget *box,const char *label,GCallback callback,gpointer data) {
 GtkWidget *button=gtk_button_new_with_mnemonic(label);
 gtk_widget_set_size_request(button,DOVE_BUTTON_WIDTH,DOVE_BUTTON_HEIGHT);
 gtk_container_add(GTK_CONTAINER(box),button);
 if(callback){g_signal_connect(button,"clicked",callback,data);}return button;
}
/* Call before show_all. GTK can still grow to fit an app's minimum requests. */
static inline void dove_window_fit(GtkWidget *window,int width,int height) {
 GdkScreen *screen=gtk_window_get_screen(GTK_WINDOW(window));GdkRectangle area;
 gint monitor=gdk_screen_get_monitor_at_point(screen,0,0);
 gdk_screen_get_monitor_geometry(screen,monitor,&area);
 gtk_window_set_default_size(GTK_WINDOW(window),MIN(width,MAX(320,area.width-40)),MIN(height,MAX(240,area.height-80)));
 gtk_window_set_position(GTK_WINDOW(window),GTK_WIN_POS_CENTER);
}
typedef struct { GtkWidget *root,*header,*content,*actions,*status; } DoveApp;
static inline DoveApp *dove_app_layout(GtkWidget *window) {
 DoveApp *app=g_new0(DoveApp,1);app->root=dove_page();app->header=dove_layout(FALSE);
 app->content=dove_layout(FALSE);app->actions=dove_actions();app->status=dove_statusbar();
 dove_pack(app->root,app->header,FALSE);dove_pack(app->root,app->content,TRUE);
 dove_pack(app->root,app->actions,FALSE);dove_pack(app->root,app->status,FALSE);
 gtk_container_add(GTK_CONTAINER(window),app->root);
 g_object_set_data_full(G_OBJECT(window),"dove-app-layout",app,g_free);return app;
}
#endif
