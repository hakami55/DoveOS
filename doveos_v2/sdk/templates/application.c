#include "doveos.h"
static GtkWidget *window;
static void about(GtkWidget *button,gpointer data) {
 (void)button;(void)data;dove_message(GTK_WINDOW(window),"Built with DoveOS SDK " DOVEOS_SDK_VERSION);
}
static void close_window(GtkWidget *widget,gpointer data){(void)widget;(void)data;gtk_main_quit();}
int main(int argc,char **argv) {
 gtk_init(&argc,&argv);window=dove_form("@APP_NAME@",720,480);dove_window_fit(window,720,480);
 gtk_window_set_wmclass(GTK_WINDOW(window),"@APP_ID@","@APP_ID@");
 DoveApp *app=dove_app_layout(window);dove_pack(app->header,dove_label("@APP_NAME@"),FALSE);
 GtkWidget *page=dove_page();dove_pack(page,dove_label("Your application content goes here."),FALSE);
 dove_pack(app->content,dove_scroll(page),TRUE);
 dove_action(app->actions,"_About",G_CALLBACK(about),NULL);
 dove_action(app->actions,"_Close",G_CALLBACK(gtk_widget_destroy),window);
 dove_status(app->status,"Ready");g_signal_connect(window,"destroy",G_CALLBACK(close_window),NULL);
 gtk_widget_show_all(window);gtk_main();return 0;
}
