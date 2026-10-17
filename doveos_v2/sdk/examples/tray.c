#include "doveos.h"
#include <X11/Xlib.h>
#include <stdio.h>
#include <string.h>
static GtkWidget *window,*menu;
static GtkStatusIcon *icon;
static int result;
static void restore(GtkStatusIcon *sender,gpointer data){(void)sender;(void)data;gtk_widget_show(window);gtk_window_present(GTK_WINDOW(window));}
static void hide(GtkWidget *sender,gpointer data){(void)sender;(void)data;gtk_widget_hide(window);}
static void quit(GtkWidget *sender,gpointer data){(void)sender;(void)data;gtk_main_quit();}
static void popup(GtkStatusIcon *sender,guint button,guint timestamp,gpointer data){(void)data;dove_trayicon_popup(sender,menu,button,timestamp);}
static gboolean check(gpointer data){
 (void)data;GdkScreen *screen=NULL;GdkRectangle area={0};GtkOrientation orientation;
 gboolean geometry=gtk_status_icon_get_geometry(icon,&screen,&area,&orientation);
 gtk_widget_hide(window);g_signal_emit_by_name(icon,"activate");
 gboolean restored=gtk_widget_get_visible(window);
 int sw=gdk_screen_get_width(gdk_screen_get_default()),sh=gdk_screen_get_height(gdk_screen_get_default());
 gboolean beside_clock=geometry&&area.x>sw-350&&area.y>=sh-40&&area.x+area.width<=sw;
 gboolean pass=dove_trayicon_embedded(icon)&&restored&&beside_clock;
 printf("SDK_TRAY embedded=%d geometry=%d x=%d y=%d width=%d height=%d restore=%d beside_clock=%d pass=%d\n",dove_trayicon_embedded(icon),geometry,area.x,area.y,area.width,area.height,restored,beside_clock,pass);
 result=pass?0:1;gtk_main_quit();return FALSE;
}
int main(int argc,char **argv){
 gtk_init(&argc,&argv);
 window=dove_form("SDK Tray Example",340,140);
 GtkWidget *panel=dove_panel(FALSE,6);gtk_container_set_border_width(GTK_CONTAINER(panel),12);gtk_container_add(GTK_CONTAINER(window),panel);
 dove_add(panel,dove_label("Your app icon appears in the system tray beside the clock. Click it to restore this window; right-click for its menu."));
 GtkWidget *button=dove_button("Hide to tray");dove_add(panel,button);g_signal_connect(button,"clicked",G_CALLBACK(hide),NULL);
 icon=dove_trayicon("/etc/doveos/dove-start.png","SDK Tray Example - click to restore");
 g_signal_connect(icon,"activate",G_CALLBACK(restore),NULL);g_signal_connect(icon,"popup-menu",G_CALLBACK(popup),NULL);
 menu=dove_contextmenu();GtkWidget *item=dove_menuitem("Show window");gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",G_CALLBACK(restore),NULL);
 item=dove_menuitem("Exit");gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",G_CALLBACK(quit),NULL);
 g_signal_connect(window,"destroy",G_CALLBACK(quit),NULL);gtk_widget_show_all(window);
 if(argc>1&&!strcmp(argv[1],"--smoke-test"))dove_timer(2500,check,NULL);
 gtk_main();dove_trayicon_destroy(icon);gtk_widget_destroy(menu);return result;
}
