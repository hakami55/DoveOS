#ifndef DOVE_TOOLBAR_ICONS_H
#define DOVE_TOOLBAR_ICONS_H
/* Small native GTK toolbar images. A low-priority repaint runs once after
 * each expose/layout/state change, after the parent's buffered background.
 * This avoids images being erased on the dock's nested X display. */
static inline GdkPixbuf *dove_toolbar_icon_pixels(GtkWidget *w) {
 GdkPixbuf *source=g_object_get_data(G_OBJECT(w),"dove-toolbar-source");if(!source)return NULL;
 int width=gdk_pixbuf_get_width(source),height=gdk_pixbuf_get_height(source);
 GdkPixbuf *rgb=gdk_pixbuf_new(GDK_COLORSPACE_RGB,FALSE,8,width,height);
 GdkColor bg=gtk_widget_get_style(w)->bg[GTK_STATE_NORMAL];
 gdk_pixbuf_fill(rgb,((guint32)(bg.red>>8)<<24)|((guint32)(bg.green>>8)<<16)|((guint32)(bg.blue>>8)<<8)|255);
 if(GTK_WIDGET_IS_SENSITIVE(w))gdk_pixbuf_composite(source,rgb,0,0,width,height,0,0,1,1,GDK_INTERP_NEAREST,255);
 else {GdkPixbuf *gray=gdk_pixbuf_copy(source);gdk_pixbuf_saturate_and_pixelate(source,gray,0,FALSE);gdk_pixbuf_composite(gray,rgb,0,0,width,height,0,0,1,1,GDK_INTERP_NEAREST,128);g_object_unref(gray);}
 return rgb;
}
static inline void dove_toolbar_icon_paint(GtkWidget *w) {
 if(!GTK_WIDGET_DRAWABLE(w))return;
 GdkPixbuf *rgb=dove_toolbar_icon_pixels(w);if(!rgb)return;
 GdkGC *gc=gdk_gc_new(gtk_widget_get_window(w));
 gdk_draw_rgb_image(gtk_widget_get_window(w),gc,0,0,gdk_pixbuf_get_width(rgb),gdk_pixbuf_get_height(rgb),GDK_RGB_DITHER_NONE,gdk_pixbuf_get_pixels(rgb),gdk_pixbuf_get_rowstride(rgb));
 g_object_unref(gc);g_object_unref(rgb);
}
static inline gboolean dove_toolbar_icon_idle(gpointer data) {
 GtkWidget *w=data;g_object_set_data(G_OBJECT(w),"dove-toolbar-repaint",NULL);dove_toolbar_icon_paint(w);return FALSE;
}
static inline void dove_toolbar_icon_update(GtkWidget *w,gpointer data) {
 (void)data;if(g_object_get_data(G_OBJECT(w),"dove-toolbar-repaint"))return;
 guint id=g_idle_add_full(G_PRIORITY_LOW,dove_toolbar_icon_idle,g_object_ref(w),g_object_unref);
 g_object_set_data(G_OBJECT(w),"dove-toolbar-repaint",GUINT_TO_POINTER(id));
}
static inline gboolean dove_toolbar_icon_expose(GtkWidget *w,GdkEventExpose *event,gpointer data) {
 (void)event;(void)data;dove_toolbar_icon_paint(w);dove_toolbar_icon_update(w,NULL);return TRUE;
}
static inline void dove_toolbar_icon_size(GtkWidget *w,GtkAllocation *allocation,gpointer data) {(void)allocation;dove_toolbar_icon_update(w,data);}
static inline void dove_toolbar_icon_state(GtkWidget *w,GtkStateType state,gpointer data) {(void)state;dove_toolbar_icon_update(w,data);}
static inline GtkWidget *dove_toolbar_icon(GdkPixbuf *source) {
 GtkWidget *w=gtk_drawing_area_new();gtk_widget_set_double_buffered(w,FALSE);
 if(source){g_object_set_data_full(G_OBJECT(w),"dove-toolbar-source",g_object_ref(source),g_object_unref);gtk_widget_set_size_request(w,gdk_pixbuf_get_width(source),gdk_pixbuf_get_height(source));}
 else gtk_widget_set_size_request(w,16,16);
 g_signal_connect(w,"expose-event",G_CALLBACK(dove_toolbar_icon_expose),NULL);
 g_signal_connect(w,"map",G_CALLBACK(dove_toolbar_icon_update),NULL);
 g_signal_connect(w,"size-allocate",G_CALLBACK(dove_toolbar_icon_size),NULL);
 g_signal_connect(w,"state-changed",G_CALLBACK(dove_toolbar_icon_state),NULL);return w;
}
#endif
