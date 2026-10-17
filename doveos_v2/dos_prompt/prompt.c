#include "doveos.h"
#include "dove_terminal.h"
static GtkWidget *window,*terminal;static gboolean full;
static void quit(GtkWidget *w,gpointer data){(void)w;(void)data;gtk_main_quit();}
static void exited(GtkWidget *w,gint status,gpointer data){(void)data;if(status==-1)dove_message(GTK_WINDOW(window),dove_terminal_error(w));gtk_widget_destroy(window);}
static gboolean fit_later(gpointer data){GtkWidget *widget=data;dove_terminal_fit(widget);g_object_unref(widget);return FALSE;}
static void font_changed(GtkComboBox *box,gpointer data){(void)data;gint item=gtk_combo_box_get_active(box);if(item>=0)dove_terminal_key(terminal,XK_F1+item,ControlMask|Mod1Mask);g_timeout_add(120,fit_later,g_object_ref(terminal));dove_terminal_focus(terminal);}
static void action(GtkWidget *w,gpointer data){(void)w;const char *kind=data;
 if(!strcmp(kind,"copy"))dove_terminal_key(terminal,XK_c,ControlMask|ShiftMask);
 else if(!strcmp(kind,"paste"))dove_terminal_key(terminal,XK_v,ControlMask|ShiftMask);
 else if(!strcmp(kind,"clear"))dove_terminal_key(terminal,XK_l,ControlMask);
 else if(!strcmp(kind,"full")){full=!full;if(full)gtk_window_fullscreen(GTK_WINDOW(window));else gtk_window_unfullscreen(GTK_WINDOW(window));}
 else dove_message(GTK_WINDOW(window),"Terminal uses the DoveOS Linux shell.\n\nls   - list files\nclear - clear the screen\ncd   - change directory\nexit - close the terminal\n\nDrag across text to select it; Copy and Paste use the clipboard.\nCtrl+Shift+C / Ctrl+Shift+V also work.\nShift+PageUp / Shift+PageDown scroll previous output.\nUse the toolbar to toggle full screen. Ctrl+C interrupts a command.");
 dove_terminal_focus(terminal);
}
static gboolean keys(GtkWidget *w,GdkEventKey *event,gpointer data){(void)w;(void)data;if(event->keyval==GDK_F11){action(NULL,"full");return TRUE;}return FALSE;}
static void tool(GtkWidget *bar,const char *name,const char *stock,const char *kind){GtkToolItem *item=dove_toolbutton(name,stock);gtk_toolbar_insert(GTK_TOOLBAR(bar),item,-1);gtk_tool_item_set_tooltip_text(item,name);g_signal_connect(item,"clicked",G_CALLBACK(action),(gpointer)kind);}
int main(int argc,char **argv){
 gtk_init(&argc,&argv);window=dove_window("Terminal",720,430);gtk_window_set_wmclass(GTK_WINDOW(window),"DoveTerminal","DoveTerminal");gtk_window_set_icon_from_file(GTK_WINDOW(window),"/etc/doveos/classic-icons/48/utilities-terminal.png",NULL);g_signal_connect(window,"destroy",G_CALLBACK(quit),NULL);g_signal_connect(window,"key-press-event",G_CALLBACK(keys),NULL);
 GtkWidget *box=gtk_vbox_new(FALSE,0);gtk_container_add(GTK_CONTAINER(window),box);GtkWidget *bar=dove_toolbar();gtk_toolbar_set_style(GTK_TOOLBAR(bar),GTK_TOOLBAR_ICONS);gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);
 GtkToolItem *item=gtk_tool_item_new();const char *sizes[]={"Auto","Small","Medium","Large"};GtkWidget *fonts=dove_combobox(sizes,4,0);gtk_widget_set_size_request(fonts,95,-1);gtk_container_add(GTK_CONTAINER(item),fonts);gtk_toolbar_insert(GTK_TOOLBAR(bar),item,-1);dove_tooltip(fonts,"Terminal font size");gtk_toolbar_insert(GTK_TOOLBAR(bar),gtk_separator_tool_item_new(),-1);
 tool(bar,"Copy selected text",GTK_STOCK_COPY,"copy");tool(bar,"Paste clipboard",GTK_STOCK_PASTE,"paste");tool(bar,"Clear screen",GTK_STOCK_CLEAR,"clear");gtk_toolbar_insert(GTK_TOOLBAR(bar),gtk_separator_tool_item_new(),-1);tool(bar,"Full screen",GTK_STOCK_FULLSCREEN,"full");tool(bar,"Help",GTK_STOCK_HELP,"help");
 g_setenv("ENV","/media/usb0/DoveOS-v2/Terminal/shell-profile.sh",TRUE);g_setenv("PS1","\\w \\$ ",TRUE);
 const char *directory=argc>1&&g_file_test(argv[1],G_FILE_TEST_IS_DIR)?argv[1]:"/media/usb0";terminal=dove_terminal_new_with_font(directory,NULL,"More Perfect DOS VGA:antialias=false");dove_terminal_on_exit(terminal,exited,NULL);GtkWidget *frame=gtk_frame_new(NULL);gtk_frame_set_shadow_type(GTK_FRAME(frame),GTK_SHADOW_IN);gtk_container_add(GTK_CONTAINER(frame),terminal);gtk_box_pack_start(GTK_BOX(box),frame,TRUE,TRUE,0);g_signal_connect(fonts,"changed",G_CALLBACK(font_changed),NULL);gtk_widget_show_all(window);gtk_main();return 0;
}
