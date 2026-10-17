#ifndef DOVE_TASK_H
#define DOVE_TASK_H
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
/* All callbacks run on GTK's main context. No worker threads/services. */
typedef struct DoveTask DoveTask;
typedef void (*DoveTaskDone)(const char *output,const char *errors,gint wait_status,gboolean cancelled,gpointer data);
struct DoveTask {
 GtkWidget *owner;GPid pid;GIOChannel *pipes[2];guint watches[2],child_watch,timeout,kill_timer;
 GString *text[2];gboolean cancelled,owner_destroyed,truncated[2];gulong destroy_signal;
 DoveTaskDone done;gpointer data;GDestroyNotify free_data;
};
enum { DOVE_TASK_OUTPUT_LIMIT=65536 };
static inline void dove_busy_change(GtkWidget *owner,gint delta) {
 gint count=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(owner),"dove-busy-count"));count=MAX(0,count+delta);
 g_object_set_data(G_OBJECT(owner),"dove-busy-count",GINT_TO_POINTER(count));
 GdkWindow *window=gtk_widget_get_window(owner);if(!window)return;
 GdkCursor *cursor=gdk_cursor_new_from_name(gtk_widget_get_display(owner),count?"left_ptr_watch":"left_ptr");
 if(!cursor)cursor=gdk_cursor_new(count?GDK_WATCH:GDK_LEFT_PTR);
 gdk_window_set_cursor(window,cursor);gdk_cursor_unref(cursor);
}
static inline void dove_task_drain(DoveTask *task,gint index) {
 gchar buffer[4096];gsize count;GIOStatus status;guint chunks=0;
 do {
  count=0;status=g_io_channel_read_chars(task->pipes[index],buffer,sizeof buffer,&count,NULL);
  gsize available=DOVE_TASK_OUTPUT_LIMIT-task->text[index]->len;
  if(count>available)task->truncated[index]=TRUE;
  if(count)g_string_append_len(task->text[index],buffer,MIN(count,available));
 }while(status==G_IO_STATUS_NORMAL&&count&&++chunks<16);
}
static inline gboolean dove_task_read(GIOChannel *channel,GIOCondition condition,gpointer data) {
 DoveTask *task=data;gint index=channel==task->pipes[0]?0:1;dove_task_drain(task,index);
 if(condition&(G_IO_HUP|G_IO_ERR|G_IO_NVAL)){task->watches[index]=0;return FALSE;}return TRUE;
}
static inline gboolean dove_task_kill(gpointer data) {
 DoveTask *task=data;task->kill_timer=0;kill(task->pid,SIGKILL);return FALSE;
}
static inline void dove_task_cancel(DoveTask *task) {
 if(!task||task->cancelled)return;
 task->cancelled=TRUE;kill(task->pid,SIGTERM);
 task->kill_timer=g_timeout_add(500,dove_task_kill,task);
}
static inline gboolean dove_task_timeout(gpointer data) {
 DoveTask *task=data;task->timeout=0;dove_task_cancel(task);return FALSE;
}
static inline void dove_task_owner_destroy(GtkWidget *owner,gpointer data) {
 (void)owner;DoveTask *task=data;task->owner_destroyed=TRUE;dove_task_cancel(task);
}
static inline void dove_task_exit(GPid pid,gint status,gpointer data) {
 DoveTask *task=data;task->child_watch=0;
 if(task->timeout)g_source_remove(task->timeout);
 if(task->kill_timer)g_source_remove(task->kill_timer);
 for(gint i=0;i<2;i++) {
  dove_task_drain(task,i);if(task->watches[i])g_source_remove(task->watches[i]);g_io_channel_unref(task->pipes[i]);
  if(task->truncated[i])g_string_append(task->text[i],"\n[Output truncated]\n");
 }
 g_spawn_close_pid(pid);
 if(g_signal_handler_is_connected(task->owner,task->destroy_signal))g_signal_handler_disconnect(task->owner,task->destroy_signal);
 if(!task->owner_destroyed) {
  dove_busy_change(task->owner,-1);
  if(task->done)task->done(task->text[0]->str,task->text[1]->str,status,task->cancelled,task->data);
 }
 if(task->free_data)task->free_data(task->data);
 for(gint i=0;i<2;i++)g_string_free(task->text[i],TRUE);
 g_object_unref(task->owner);g_free(task);
}
/* Borrowed handle expires at completion. Pass argv, never interpolated shell text.
 * Strings in callback are borrowed byte streams; validate UTF-8 before display.
 * On startup failure, caller retains data; on success free_data runs once. */
static inline DoveTask *dove_task_start(GtkWidget *owner,const char *const *argv,guint timeout_ms,DoveTaskDone done,gpointer data,GDestroyNotify free_data,GError **error) {
 gint out=-1,err=-1;GPid pid;
 if(!owner||!argv||!argv[0]||!g_path_is_absolute(argv[0])) {
  g_set_error_literal(error,G_SPAWN_ERROR,G_SPAWN_ERROR_FAILED,"Task needs an owner and an absolute executable path");return NULL;
 }
 if(!g_spawn_async_with_pipes(NULL,(gchar**)argv,NULL,G_SPAWN_DO_NOT_REAP_CHILD,NULL,NULL,&pid,NULL,&out,&err,error))return NULL;
 DoveTask *task=g_new0(DoveTask,1);task->owner=g_object_ref(owner);task->pid=pid;task->done=done;task->data=data;task->free_data=free_data;
 gint fds[]={out,err};
 for(gint i=0;i<2;i++) {
  task->text[i]=g_string_new(NULL);task->pipes[i]=g_io_channel_unix_new(fds[i]);
  g_io_channel_set_close_on_unref(task->pipes[i],TRUE);g_io_channel_set_encoding(task->pipes[i],NULL,NULL);
  g_io_channel_set_buffered(task->pipes[i],FALSE);
  g_io_channel_set_flags(task->pipes[i],g_io_channel_get_flags(task->pipes[i])|G_IO_FLAG_NONBLOCK,NULL);
  task->watches[i]=g_io_add_watch(task->pipes[i],G_IO_IN|G_IO_HUP|G_IO_ERR|G_IO_NVAL,dove_task_read,task);
 }
 task->destroy_signal=g_signal_connect(owner,"destroy",G_CALLBACK(dove_task_owner_destroy),task);
 task->child_watch=g_child_watch_add(pid,dove_task_exit,task);
 task->timeout=g_timeout_add(timeout_ms?timeout_ms:30000,dove_task_timeout,task);
 dove_busy_change(owner,1);return task;
}
static inline void dove_shortcut(GtkWidget *window,GtkWidget *target,const char *signal,const char *accelerator) {
 GtkAccelGroup *group=g_object_get_data(G_OBJECT(window),"dove-shortcuts");
 if(!group){group=gtk_accel_group_new();gtk_window_add_accel_group(GTK_WINDOW(window),group);g_object_set_data_full(G_OBJECT(window),"dove-shortcuts",group,g_object_unref);}
 guint key;GdkModifierType mods;gtk_accelerator_parse(accelerator,&key,&mods);
 if(key)gtk_widget_add_accelerator(target,signal,group,key,mods,GTK_ACCEL_VISIBLE);
}
#endif
