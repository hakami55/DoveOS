#ifndef DOVE_UI_LAYOUT_H
#define DOVE_UI_LAYOUT_H
/* Shared, full-width layout helpers for the built-in GTK applications. */
static void ui_pack(GtkWidget *box,GtkWidget *child){gtk_box_pack_start(GTK_BOX(box),child,FALSE,TRUE,0);}
static GtkWidget *ui_text(const char *text){GtkWidget *w=gtk_label_new(text);gtk_misc_set_alignment(GTK_MISC(w),0,0.5);return w;}
static void ui_header(GtkWidget *box,const char *title,const char *subtitle,const char *icon){
 GtkWidget *row=gtk_hbox_new(FALSE,10),*labels=gtk_vbox_new(FALSE,3);ui_pack(box,row);
 if(icon&&g_file_test(icon,G_FILE_TEST_IS_REGULAR))gtk_box_pack_start(GTK_BOX(row),gtk_image_new_from_file(icon),FALSE,FALSE,0);
 GtkWidget *name=ui_text("");gchar *markup=g_markup_printf_escaped("<b>%s</b>",title);gtk_label_set_markup(GTK_LABEL(name),markup);g_free(markup);
 ui_pack(labels,name);ui_pack(labels,ui_text(subtitle));gtk_box_pack_start(GTK_BOX(row),labels,TRUE,TRUE,0);ui_pack(box,gtk_hseparator_new());
}
static GtkWidget *ui_group(GtkWidget *box,const char *title){GtkWidget *frame=gtk_frame_new(title),*inner=gtk_vbox_new(FALSE,7);gtk_container_set_border_width(GTK_CONTAINER(inner),9);gtk_container_add(GTK_CONTAINER(frame),inner);ui_pack(box,frame);return inner;}
static GtkWidget *ui_status(GtkWidget *box,const char *text){GtkWidget *inner=ui_group(box,"Status"),*w=ui_text(text);gtk_label_set_ellipsize(GTK_LABEL(w),PANGO_ELLIPSIZE_END);ui_pack(inner,w);return w;}
#endif
