#ifndef DOVEOS_SDK_H
#define DOVEOS_SDK_H
#include <gtk/gtk.h>
#include <stdlib.h>
#define DOVEOS_SDK_VERSION "2.0-preview.1"
#define DOVEOS_SDK_VERSION_PATCH 0
/* Inspectable build marker; no process, timer or runtime allocation. */
#if defined(__GNUC__)
static const char dove_sdk_build_version[] __attribute__((used)) = "DoveOS SDK " DOVEOS_SDK_VERSION;
#endif
#define DOVEOS_SDK_VERSION_MAJOR 2
#define DOVEOS_SDK_VERSION_MINOR 0
/* GTK controls inherit the session theme. Native JWM decorations supply the
 * shared DoveOS title bar, window controls, gray frame and taskbar button. */
static inline GtkWidget *dove_window(const char *title,int width,int height){GtkWidget *w=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_decorated(GTK_WINDOW(w),TRUE);gtk_window_set_title(GTK_WINDOW(w),title);gtk_window_set_default_size(GTK_WINDOW(w),width,height);return w;}
#include "dove_image.h"
/* Classic message boxes remain native dialogs: normal focus, modal ownership,
 * keyboard defaults and a Close button, without app-style resize controls. */
static inline void dove_dialog_style(GtkWidget *dialog,GtkWindow *parent,const char *title){
 gtk_window_set_decorated(GTK_WINDOW(dialog),TRUE);gtk_window_set_resizable(GTK_WINDOW(dialog),FALSE);
 gtk_window_set_type_hint(GTK_WINDOW(dialog),GDK_WINDOW_TYPE_HINT_DIALOG);
 gtk_window_set_skip_taskbar_hint(GTK_WINDOW(dialog),TRUE);
 gtk_window_set_title(GTK_WINDOW(dialog),title&&*title?title:(parent&&gtk_window_get_title(parent)?gtk_window_get_title(parent):"DoveOS"));
 gtk_button_box_set_layout(GTK_BUTTON_BOX(GTK_DIALOG(dialog)->action_area),GTK_BUTTONBOX_CENTER);
 GList *buttons=gtk_container_get_children(GTK_CONTAINER(GTK_DIALOG(dialog)->action_area));
 for(GList *it=buttons;it;it=it->next)if(GTK_IS_BUTTON(it->data)){
  GtkButton *button=GTK_BUTTON(it->data);GtkStockItem stock;
  if(gtk_button_get_use_stock(button)&&gtk_stock_lookup(gtk_button_get_label(button),&stock)){
   gtk_button_set_use_stock(button,FALSE);gtk_button_set_label(button,stock.label);gtk_button_set_use_underline(button,TRUE);
  }
  gtk_button_set_image(button,NULL);gtk_widget_set_size_request(GTK_WIDGET(button),75,23);
 }
 g_list_free(buttons);
}
static inline GtkWidget *dove_label(const char *text){GtkWidget *w=gtk_label_new(text);gtk_misc_set_alignment(GTK_MISC(w),0,0.5);gtk_label_set_line_wrap(GTK_LABEL(w),TRUE);return w;}
static inline void dove_add(GtkWidget *box,GtkWidget *w){gtk_box_pack_start(GTK_BOX(box),w,FALSE,FALSE,4);}
static inline GtkWidget *dove_messagebox(GtkWindow *parent,const char *text,GtkMessageType type,GtkButtonsType buttons);
static inline void dove_message(GtkWindow *parent,const char *text){GtkWidget *dialog=dove_messagebox(parent,text,GTK_MESSAGE_INFO,GTK_BUTTONS_OK);gtk_dialog_run(GTK_DIALOG(dialog));gtk_widget_destroy(dialog);}
/* SDK 1.1: native GTK2 controls, with normal GTK ownership and signals.
 * Containers own added widgets. Tree/list models are referenced by their view.
 * Dialogs must be destroyed by their caller; tray icons must be unreferenced. */
static inline GtkWidget *dove_form(const char *title,int width,int height){return dove_window(title,width,height);}
static inline GtkWidget *dove_textbox(const char *text){GtkWidget *w=gtk_entry_new();gtk_entry_set_text(GTK_ENTRY(w),text?text:"");return w;}
static inline GtkWidget *dove_textarea(const char *text){GtkWidget *w=gtk_text_view_new();gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(w),GTK_WRAP_WORD_CHAR);gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(w)),text?text:"",-1);return w;}
static inline GtkWidget *dove_button(const char *text){return gtk_button_new_with_label(text);}
static inline GtkWidget *dove_checkbox(const char *text,gboolean checked){GtkWidget *w=gtk_check_button_new_with_label(text);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),checked);return w;}
static inline GtkWidget *dove_radiobutton(GtkWidget *previous,const char *text){return previous?gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(previous),text):gtk_radio_button_new_with_label(NULL,text);}
static inline GtkWidget *dove_groupbox(const char *text){return gtk_frame_new(text);}
static inline GtkWidget *dove_panel(gboolean horizontal,int spacing){return horizontal?gtk_hbox_new(FALSE,spacing):gtk_vbox_new(FALSE,spacing);}
static inline GtkWidget *dove_image(const char *filename){return dove_image_stable(filename?gtk_image_new_from_file(filename):gtk_image_new());}
static inline GtkWidget *dove_picturebox(const char *filename){GtkWidget *w=gtk_frame_new(NULL);gtk_frame_set_shadow_type(GTK_FRAME(w),GTK_SHADOW_IN);gtk_container_add(GTK_CONTAINER(w),dove_image(filename));return w;}
static inline GtkWidget *dove_combobox(const char *const *items,int count,int selected){GtkWidget *w=gtk_combo_box_text_new();for(int i=0;i<count;i++)gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w),items[i]);if(selected>=0&&selected<count)gtk_combo_box_set_active(GTK_COMBO_BOX(w),selected);return w;}
/* ListBox is a native, keyboard accessible one-column TreeView, not GTK3 ListBox. */
static inline GtkWidget *dove_listview(GtkTreeModel *model,const char *const *titles,int count){GtkWidget *w=gtk_tree_view_new_with_model(model);for(int i=0;i<count;i++){GtkCellRenderer *r=gtk_cell_renderer_text_new();gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(w),-1,titles[i],r,"text",i,NULL);}gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(w),TRUE);return w;}
static inline GtkWidget *dove_listbox(const char *const *items,int count){GtkListStore *m=gtk_list_store_new(1,G_TYPE_STRING);GtkTreeIter it;for(int i=0;i<count;i++){gtk_list_store_append(m,&it);gtk_list_store_set(m,&it,0,items[i],-1);}const char *titles[]={"Items"};GtkWidget *w=dove_listview(GTK_TREE_MODEL(m),titles,1);gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(w),FALSE);g_object_unref(m);return w;}
static inline GtkWidget *dove_treeview(GtkTreeModel *model,const char *title){const char *titles[]={title};return dove_listview(model,titles,1);}
static inline GtkWidget *dove_tabs(void){return gtk_notebook_new();}
static inline int dove_tab_add(GtkWidget *tabs,GtkWidget *page,const char *title){return gtk_notebook_append_page(GTK_NOTEBOOK(tabs),page,gtk_label_new(title));}
static inline GtkWidget *dove_progressbar(double fraction){GtkWidget *w=gtk_progress_bar_new();gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w),CLAMP(fraction,0.0,1.0));return w;}
static inline GtkWidget *dove_slider(gboolean horizontal,double min,double max,double step){return horizontal?gtk_hscale_new_with_range(min,max,step):gtk_vscale_new_with_range(min,max,step);}
/* 'max' is the content extent; largest scroll offset is max minus page_size. */
static inline GtkWidget *dove_scrollbar(gboolean horizontal,double value,double min,double max,double step,double page_size){GtkAdjustment *a=GTK_ADJUSTMENT(gtk_adjustment_new(value,min,max,step,MAX(step,page_size),page_size));return horizontal?gtk_hscrollbar_new(a):gtk_vscrollbar_new(a);}
static inline GtkWidget *dove_hscrollbar(double value,double min,double max,double step,double page_size){return dove_scrollbar(TRUE,value,min,max,step,page_size);}
static inline GtkWidget *dove_vscrollbar(double value,double min,double max,double step,double page_size){return dove_scrollbar(FALSE,value,min,max,step,page_size);}
static inline GtkWidget *dove_menubar(void){return gtk_menu_bar_new();}
static inline GtkWidget *dove_contextmenu(void){return gtk_menu_new();}
static inline GtkWidget *dove_menuitem(const char *text){return gtk_menu_item_new_with_mnemonic(text);}
static inline GtkWidget *dove_toolbar(void){GtkWidget *w=gtk_toolbar_new();gtk_toolbar_set_style(GTK_TOOLBAR(w),GTK_TOOLBAR_BOTH_HORIZ);gtk_toolbar_set_icon_size(GTK_TOOLBAR(w),GTK_ICON_SIZE_SMALL_TOOLBAR);return w;}
#include "dove_toolbar_icons.h"
static inline GtkToolItem *dove_toolbutton(const char *text,const char *stock_icon){
 GtkWidget *image=NULL;if(stock_icon){GtkWidget *probe=gtk_image_new();g_object_ref_sink(probe);GdkPixbuf *pix=gtk_widget_render_icon(probe,stock_icon,GTK_ICON_SIZE_SMALL_TOOLBAR,NULL);image=dove_toolbar_icon(pix);if(pix)g_object_unref(pix);g_object_unref(probe);}
 GtkToolItem *item=gtk_tool_button_new(NULL,text);if(image){gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(item),image);gtk_widget_show(image);}return item;
}
static inline GtkWidget *dove_statusbar(void){return gtk_statusbar_new();}
static inline void dove_status(GtkWidget *bar,const char *text){guint id=gtk_statusbar_get_context_id(GTK_STATUSBAR(bar),"DoveOS");gtk_statusbar_pop(GTK_STATUSBAR(bar),id);gtk_statusbar_push(GTK_STATUSBAR(bar),id,text);}
/* GLib timers run in the GUI loop. Return FALSE to stop. Remove before freeing data. */
static inline guint dove_timer(guint milliseconds,GSourceFunc callback,gpointer data){return g_timeout_add(milliseconds,callback,data);}
static inline gboolean dove_timer_stop(guint id){return id?g_source_remove(id):FALSE;}
static inline GtkWidget *dove_messagebox(GtkWindow *parent,const char *text,GtkMessageType type,GtkButtonsType buttons){
 GtkWidget *w=gtk_message_dialog_new(parent,GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,type,buttons,"%s",text?text:"");
 if(type>=GTK_MESSAGE_INFO&&type<=GTK_MESSAGE_ERROR){const char *stocks[]={GTK_STOCK_DIALOG_INFO,GTK_STOCK_DIALOG_WARNING,GTK_STOCK_DIALOG_QUESTION,GTK_STOCK_DIALOG_ERROR};GtkWidget *image=dove_image_stable(gtk_image_new_from_stock(stocks[type],GTK_ICON_SIZE_DIALOG));gtk_message_dialog_set_image(GTK_MESSAGE_DIALOG(w),image);gtk_widget_show(image);}
 else if(type==GTK_MESSAGE_OTHER){GtkWidget *image=gtk_message_dialog_get_image(GTK_MESSAGE_DIALOG(w));gtk_widget_set_no_show_all(image,TRUE);gtk_widget_hide(image);}
 dove_dialog_style(w,parent,NULL);
 gint response=buttons==GTK_BUTTONS_OK||buttons==GTK_BUTTONS_OK_CANCEL?GTK_RESPONSE_OK:buttons==GTK_BUTTONS_YES_NO?GTK_RESPONSE_YES:buttons==GTK_BUTTONS_CLOSE?GTK_RESPONSE_CLOSE:buttons==GTK_BUTTONS_CANCEL?GTK_RESPONSE_CANCEL:GTK_RESPONSE_NONE;
 if(response!=GTK_RESPONSE_NONE){
  /* GTK's Unix default puts Cancel/No first. DoveOS puts OK/Yes first. */
  GtkWidget *first=gtk_dialog_get_widget_for_response(GTK_DIALOG(w),response);
  if(first)gtk_box_reorder_child(GTK_BOX(GTK_DIALOG(w)->action_area),first,0);
  gtk_dialog_set_default_response(GTK_DIALOG(w),response);
 }return w;
}
/* DoveOS combinations extend the existing GtkButtonsType API. */
typedef enum {DOVE_BUTTONS_OK,DOVE_BUTTONS_OK_CANCEL,DOVE_BUTTONS_YES_NO,DOVE_BUTTONS_YES_NO_CANCEL,DOVE_BUTTONS_RETRY_CANCEL,DOVE_BUTTONS_ABORT_RETRY_IGNORE,DOVE_BUTTONS_CLOSE,DOVE_BUTTONS_CANCEL,DOVE_BUTTONS_NONE} DoveMessageButtons;
enum {DOVE_RESPONSE_ABORT=1001,DOVE_RESPONSE_RETRY=1002,DOVE_RESPONSE_IGNORE=1003};
static inline GtkWidget *dove_messagebox_classic(GtkWindow *parent,const char *text,GtkMessageType type,DoveMessageButtons buttons){
 GtkWidget *w=dove_messagebox(parent,text,type,GTK_BUTTONS_NONE);gint first=GTK_RESPONSE_NONE;
 switch(buttons){
 case DOVE_BUTTONS_OK:case DOVE_BUTTONS_OK_CANCEL:gtk_dialog_add_button(GTK_DIALOG(w),"_OK",GTK_RESPONSE_OK);first=GTK_RESPONSE_OK;break;
 case DOVE_BUTTONS_YES_NO:case DOVE_BUTTONS_YES_NO_CANCEL:gtk_dialog_add_button(GTK_DIALOG(w),"_Yes",GTK_RESPONSE_YES);gtk_dialog_add_button(GTK_DIALOG(w),"_No",GTK_RESPONSE_NO);first=GTK_RESPONSE_YES;break;
 case DOVE_BUTTONS_RETRY_CANCEL:gtk_dialog_add_button(GTK_DIALOG(w),"_Retry",DOVE_RESPONSE_RETRY);first=DOVE_RESPONSE_RETRY;break;
 case DOVE_BUTTONS_ABORT_RETRY_IGNORE:gtk_dialog_add_button(GTK_DIALOG(w),"_Abort",DOVE_RESPONSE_ABORT);gtk_dialog_add_button(GTK_DIALOG(w),"_Retry",DOVE_RESPONSE_RETRY);gtk_dialog_add_button(GTK_DIALOG(w),"_Ignore",DOVE_RESPONSE_IGNORE);first=DOVE_RESPONSE_ABORT;break;
 case DOVE_BUTTONS_CLOSE:gtk_dialog_add_button(GTK_DIALOG(w),"_Close",GTK_RESPONSE_CLOSE);first=GTK_RESPONSE_CLOSE;break;
 case DOVE_BUTTONS_CANCEL:case DOVE_BUTTONS_NONE:break;
 }
 if(buttons==DOVE_BUTTONS_OK_CANCEL||buttons==DOVE_BUTTONS_YES_NO_CANCEL||buttons==DOVE_BUTTONS_RETRY_CANCEL||buttons==DOVE_BUTTONS_CANCEL){gtk_dialog_add_button(GTK_DIALOG(w),"_Cancel",GTK_RESPONSE_CANCEL);if(first==GTK_RESPONSE_NONE)first=GTK_RESPONSE_CANCEL;}
 dove_dialog_style(w,parent,NULL);if(first!=GTK_RESPONSE_NONE)gtk_dialog_set_default_response(GTK_DIALOG(w),first);return w;
}
#include "dove_file_dialog.h"
/* JWM registers the standard XEmbed notification area beside the clock.
 * Keep gtk_main running; destroy the icon when the app exits. */
static inline GtkStatusIcon *dove_trayicon(const char *filename,const char *tooltip){GtkStatusIcon *w=gtk_status_icon_new_from_file(filename);gtk_status_icon_set_tooltip_text(w,tooltip);gtk_status_icon_set_visible(w,TRUE);return w;}
static inline void dove_trayicon_visible(GtkStatusIcon *icon,gboolean visible){gtk_status_icon_set_visible(icon,visible);}
static inline gboolean dove_trayicon_embedded(GtkStatusIcon *icon){return gtk_status_icon_is_embedded(icon);}
static inline void dove_trayicon_popup(GtkStatusIcon *icon,GtkWidget *menu,guint button,guint timestamp){gtk_widget_show_all(menu);gtk_menu_popup(GTK_MENU(menu),NULL,NULL,gtk_status_icon_position_menu,icon,button,timestamp);}
static inline void dove_trayicon_destroy(GtkStatusIcon *icon){if(icon){gtk_status_icon_set_visible(icon,FALSE);g_object_unref(icon);}}
static inline GtkWidget *dove_line(gboolean horizontal){return horizontal?gtk_hseparator_new():gtk_vseparator_new();}
static inline GtkWidget *dove_scrolled(GtkWidget *child,int width,int height){GtkWidget *w=gtk_scrolled_window_new(NULL,NULL);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(w),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);gtk_widget_set_size_request(w,width,height);gtk_container_add(GTK_CONTAINER(w),child);return w;}
static inline GtkWidget *dove_numberbox(double min,double max,double step){return gtk_spin_button_new_with_range(min,max,step);}
static inline void dove_tooltip(GtkWidget *widget,const char *text){gtk_widget_set_tooltip_text(widget,text);}
/* Call after realizing the widget; WATCH indicates busy, LEFT_PTR restores it. */
static inline gboolean dove_pointer(GtkWidget *widget,GdkCursorType type){GdkWindow *w=gtk_widget_get_window(widget);if(!w)return FALSE;GdkCursor *cursor=gdk_cursor_new(type);gdk_window_set_cursor(w,cursor);gdk_cursor_unref(cursor);return TRUE;}
/* Launch an approved helper asynchronously, without interpolating shell text.
 * Hardware actions are opt-in. Passwords must never be passed in argv. */
static inline gboolean dove_settings_async(const char *action,const char *argument,GError **error){
 const char *allowed[]={"sound-set","wifi-status","wifi-scan","bluetooth-scan",NULL};gboolean ok=FALSE;
 for(int i=0;allowed[i];i++)if(!g_strcmp0(action,allowed[i]))ok=TRUE;
 if(!ok){g_set_error_literal(error,G_SPAWN_ERROR,G_SPAWN_ERROR_FAILED,"Action is not in the SDK allowlist");return FALSE;}
 if(!g_strcmp0(action,"sound-set")){char *end=NULL;long n=strtol(argument?argument:"",&end,10);if(!argument||!*argument||*end||n<0||n>100){g_set_error_literal(error,G_SPAWN_ERROR,G_SPAWN_ERROR_FAILED,"Volume must be 0 to 100");return FALSE;}}
 gchar *argv[]={"/usr/bin/doveos-settings-proxy",(gchar*)action,(gchar*)(argument?argument:""),NULL};
 return g_spawn_async(NULL,argv,NULL,0,NULL,NULL,NULL,error);
}
#include "dove_table_dropdown.h"
#include "dove_layout.h"
#include "dove_views.h"
#include "dove_preferences.h"
#include "dove_task.h"
#endif
