#ifndef DOVE_PREFERENCES_H
#define DOVE_PREFERENCES_H
#include <glib/gstdio.h>
#include <errno.h>
typedef struct { GKeyFile *values;gchar *path; } DovePreferences;
static inline void dove_preferences_free(DovePreferences *prefs) {
 if(prefs){g_key_file_free(prefs->values);g_free(prefs->path);g_free(prefs);}
}
/* Override root for tests/portable apps. NULL uses persistent HOME/.config. */
static inline DovePreferences *dove_preferences_open(const char *app_id,const char *root,GError **error) {
 if(!app_id||!*app_id) {g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Application ID is empty");return NULL;}
 for(const char *p=app_id;*p;p++)if(!g_ascii_isalnum(*p)&&*p!='-'&&*p!='_'){
  g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Application ID must use letters, digits, - or _");return NULL;
 }
 DovePreferences *prefs=g_new0(DovePreferences,1);prefs->values=g_key_file_new();
 gchar *name=g_strconcat(app_id,".ini",NULL);prefs->path=g_build_filename(root?root:g_get_user_config_dir(),"doveos",name,NULL);g_free(name);
 GError *local=NULL;
 if(!g_key_file_load_from_file(prefs->values,prefs->path,G_KEY_FILE_KEEP_COMMENTS,&local)) {
  if(!g_error_matches(local,G_FILE_ERROR,G_FILE_ERROR_NOENT)) {g_propagate_error(error,local);dove_preferences_free(prefs);return NULL;}
  g_clear_error(&local);
 }return prefs;
}
static inline gboolean dove_preferences_save(DovePreferences *prefs,GError **error) {
 gchar *folder=g_path_get_dirname(prefs->path);
 if(g_mkdir_with_parents(folder,0700)!=0){g_set_error(error,G_FILE_ERROR,g_file_error_from_errno(errno),"Could not create settings directory: %s",g_strerror(errno));g_free(folder);return FALSE;}
 g_free(folder);gsize size;gchar *text=g_key_file_to_data(prefs->values,&size,error);if(!text)return FALSE;
 gboolean ok=g_file_set_contents(prefs->path,text,size,error);g_free(text);return ok;
}
static inline gint dove_preferences_int(DovePreferences *prefs,const char *group,const char *key,gint fallback,gint min,gint max) {
 GError *error=NULL;gint value=g_key_file_get_integer(prefs->values,group,key,&error);
 if(error){g_error_free(error);return fallback;}return CLAMP(value,min,max);
}
static inline gboolean dove_preferences_bool(DovePreferences *prefs,const char *group,const char *key,gboolean fallback) {
 GError *error=NULL;gboolean value=g_key_file_get_boolean(prefs->values,group,key,&error);
 if(error){g_error_free(error);return fallback;}return value;
}
#endif
