#include "doveos.h"
#include "dove_terminal.h"
static GtkWidget *window;
static void quit(GtkWidget *w,gpointer data){(void)w;(void)data;gtk_main_quit();}
static void exited(GtkWidget *w,gint status,gpointer data){(void)status;(void)data;const char *error=dove_terminal_error(w);if(error)dove_message(GTK_WINDOW(window),error);gtk_widget_destroy(window);}
int main(int argc,char **argv){gtk_init(&argc,&argv);window=dove_window("SDK Terminal",700,400);GtkWidget *terminal=dove_terminal_new("/media/usb0",NULL);dove_terminal_on_exit(terminal,exited,NULL);gtk_container_add(GTK_CONTAINER(window),terminal);g_signal_connect(window,"destroy",G_CALLBACK(quit),NULL);gtk_widget_show_all(window);gtk_main();return 0;}
