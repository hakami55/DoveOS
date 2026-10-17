#include <gtk/gtk.h>
/* Dialog events only: no pointer, paint or hover hooks. */
static gboolean mapped(GSignalInvocationHint *hint,guint n,const GValue *values,gpointer data){
 (void)hint;(void)data;if(!n)return TRUE;
 GObject *object=g_value_get_object(values);if(!GTK_IS_MESSAGE_DIALOG(object))return TRUE;
 GtkMessageType type;g_object_get(object,"message-type",&type,NULL);
 const char *kind=type==GTK_MESSAGE_ERROR?"error":type==GTK_MESSAGE_WARNING?"warning":type==GTK_MESSAGE_QUESTION?"question":"information";
 gchar *args[]={"/bin/sh","/usr/bin/doveos-settings-proxy","system-sound-event",(gchar*)kind,NULL};
 g_spawn_async(NULL,args,NULL,G_SPAWN_STDOUT_TO_DEV_NULL|G_SPAWN_STDERR_TO_DEV_NULL,NULL,NULL,NULL,NULL);return TRUE;
}
G_MODULE_EXPORT void gtk_module_init(gint *argc,gchar ***argv){
 (void)argc;(void)argv;g_type_class_ref(GTK_TYPE_WIDGET);
 guint signal=g_signal_lookup("map",GTK_TYPE_WIDGET);if(signal)g_signal_add_emission_hook(signal,0,mapped,NULL,NULL);
}
