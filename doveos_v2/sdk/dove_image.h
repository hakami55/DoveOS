#ifndef DOVE_IMAGE_H
#define DOVE_IMAGE_H
/* Keep GtkImage and its normal setters. Paint an opaque composite on the
 * nested GPU display, which can lose alpha-blended GTK image surfaces. */
static inline GdkPixbuf *dove_image_source(GtkWidget *w){
 GtkImageType kind=gtk_image_get_storage_type(GTK_IMAGE(w));
 if(kind==GTK_IMAGE_PIXBUF){GdkPixbuf *p=gtk_image_get_pixbuf(GTK_IMAGE(w));return p?g_object_ref(p):NULL;}
 if(kind==GTK_IMAGE_STOCK){gchar *stock=NULL;GtkIconSize size;gtk_image_get_stock(GTK_IMAGE(w),&stock,&size);return gtk_widget_render_icon(w,stock,size,NULL);}
 return NULL;
}
static inline GdkPixbuf *dove_image_pixels(GtkWidget *w){
 GdkPixbuf *source=dove_image_source(w);if(!source)return NULL;
 int width=gdk_pixbuf_get_width(source),height=gdk_pixbuf_get_height(source);
 GdkPixbuf *rgb=gdk_pixbuf_new(GDK_COLORSPACE_RGB,FALSE,8,width,height);
 GdkColor bg=gtk_widget_get_style(w)->bg[GTK_STATE_NORMAL];
 gdk_pixbuf_fill(rgb,((guint32)(bg.red>>8)<<24)|((guint32)(bg.green>>8)<<16)|((guint32)(bg.blue>>8)<<8)|255);
 if(GTK_WIDGET_IS_SENSITIVE(w))gdk_pixbuf_composite(source,rgb,0,0,width,height,0,0,1,1,GDK_INTERP_NEAREST,255);
 else {GdkPixbuf *gray=gdk_pixbuf_copy(source);gdk_pixbuf_saturate_and_pixelate(source,gray,0,FALSE);gdk_pixbuf_composite(gray,rgb,0,0,width,height,0,0,1,1,GDK_INTERP_NEAREST,128);g_object_unref(gray);}
 g_object_unref(source);return rgb;
}
static inline void dove_image_paint(GtkWidget *w){
 if(!GTK_WIDGET_DRAWABLE(w)){return;}GdkPixbuf *rgb=dove_image_pixels(w);
 if(!rgb&&gtk_image_get_storage_type(GTK_IMAGE(w))!=GTK_IMAGE_EMPTY)return;
 GtkAllocation a;gtk_widget_get_allocation(w,&a);gfloat ax,ay;gint px,py;gtk_misc_get_alignment(GTK_MISC(w),&ax,&ay);gtk_misc_get_padding(GTK_MISC(w),&px,&py);
 /* Paint the entire allocation as one opaque RGB surface. Native rectangle
  * clearing followed by RGB drawing loses pixels on this nested display. */
 gint width=MAX(a.width,1),height=MAX(a.height,1);
 GdkPixbuf *canvas=gdk_pixbuf_new(GDK_COLORSPACE_RGB,FALSE,8,width,height);
 GdkColor bg=gtk_widget_get_style(w)->bg[GTK_STATE_NORMAL];
 gdk_pixbuf_fill(canvas,((guint32)(bg.red>>8)<<24)|((guint32)(bg.green>>8)<<16)|((guint32)(bg.blue>>8)<<8)|255);
 if(rgb){
  gint rw=gdk_pixbuf_get_width(rgb),rh=gdk_pixbuf_get_height(rgb);
  gint dx=px+(gint)((a.width-2*px-rw)*ax),dy=py+(gint)((a.height-2*py-rh)*ay);
  gint sx=MAX(0,-dx),sy=MAX(0,-dy),cw=MIN(rw-sx,width-MAX(dx,0)),ch=MIN(rh-sy,height-MAX(dy,0));
  if(cw>0&&ch>0)gdk_pixbuf_copy_area(rgb,sx,sy,cw,ch,canvas,MAX(dx,0),MAX(dy,0));
  g_object_unref(rgb);
 }
 gint x=gtk_widget_get_has_window(w)?0:a.x,y=gtk_widget_get_has_window(w)?0:a.y;
 GdkGC *gc=gdk_gc_new(gtk_widget_get_window(w));GdkRectangle clip={x,y,a.width,a.height};gdk_gc_set_clip_rectangle(gc,&clip);
 gdk_draw_rgb_image(gtk_widget_get_window(w),gc,x,y,width,height,GDK_RGB_DITHER_NONE,gdk_pixbuf_get_pixels(canvas),gdk_pixbuf_get_rowstride(canvas));
 g_object_unref(gc);g_object_unref(canvas);
}
static inline gboolean dove_image_idle(gpointer data){GtkWidget *w=data;g_object_set_data(G_OBJECT(w),"dove-image-repaint",NULL);dove_image_paint(w);return FALSE;}
static inline void dove_image_update(GtkWidget *w,gpointer data){(void)data;if(g_object_get_data(G_OBJECT(w),"dove-image-repaint"))return;guint id=g_idle_add_full(G_PRIORITY_LOW,dove_image_idle,g_object_ref(w),g_object_unref);g_object_set_data(G_OBJECT(w),"dove-image-repaint",GUINT_TO_POINTER(id));}
static inline gboolean dove_image_expose(GtkWidget *w,GdkEventExpose *event,gpointer data){(void)event;(void)data;GtkImageType type=gtk_image_get_storage_type(GTK_IMAGE(w));if(type!=GTK_IMAGE_PIXBUF&&type!=GTK_IMAGE_STOCK&&type!=GTK_IMAGE_EMPTY)return FALSE;dove_image_paint(w);dove_image_update(w,NULL);return TRUE;}
static inline void dove_image_allocate(GtkWidget *w,GtkAllocation *a,gpointer data){
 if(gtk_widget_get_realized(w)&&gtk_widget_get_has_window(w))gdk_window_move_resize(gtk_widget_get_window(w),a->x,a->y,MAX(a->width,1),MAX(a->height,1));
 dove_image_update(w,data);
}
static inline void dove_image_state(GtkWidget *w,GtkStateType state,gpointer data){(void)state;dove_image_update(w,data);}
static inline GtkWidget *dove_image_stable(GtkWidget *image){
 /* GtkMisc's native realize path supports its own child window. Keeping the
  * image in that window prevents a Frame/Dialog background erasing it. */
 gtk_widget_set_has_window(image,TRUE);
 gtk_widget_set_double_buffered(image,FALSE);
 g_signal_connect(image,"expose-event",G_CALLBACK(dove_image_expose),NULL);
 g_signal_connect(image,"map",G_CALLBACK(dove_image_update),NULL);
 g_signal_connect(image,"size-allocate",G_CALLBACK(dove_image_allocate),NULL);
 g_signal_connect(image,"state-changed",G_CALLBACK(dove_image_state),NULL);
 g_signal_connect(image,"style-set",G_CALLBACK(dove_image_update),NULL);return image;
}
#endif
