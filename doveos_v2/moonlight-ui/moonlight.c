#define _GNU_SOURCE
#include "doveos.h"
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define BASE "/media/usb0/DoveOS-v2"
#define CONFIG BASE "/home/moonlight-ui.ini"
#define RUNTIME "/tmp/drmosa-usb-menu-runtime"
#define MAX_PCS 64
enum {PC_NAME,PC_IP,PC_STATE,PC_APP,PC_OPTIONS,PC_ENABLED,PC_COLUMNS};
enum {APP_FAV,APP_NAME,APP_ID,APP_COLUMNS};
typedef struct {char *ip,*name;int paired,current,chosen,hevc;gboolean remote;GtkListStore *apps;gint64 checked;} PC;
static PC pcs[MAX_PCS];static int count,selected=-1,request=-1,pending_app,exit_code;
static GtkWidget *window,*tabs,*pc_view,*app_view,*search,*summary,*pin_label,*status,*progress,*log_view,*host_label;
static GtkWidget *connect_button,*pair_button,*launch_button,*resume_button,*cancel_button,*row_launch,*unpair_button;
static GtkWidget *choices[5],*stream_info;static GtkListStore *pc_model,*app_model;static GtkTreeModel *app_filter;
static GKeyFile *config;static GPid child;static GIOChannel *channel;static guint io_watch,child_watch;
static int stdout_fd=-1;static gboolean loading;
static gboolean verifying,verify_start;static guint verification_ticks;
static const char *resolution[]={"1080p / 60 FPS","1080p / 30 FPS","720p / 60 FPS"};
static const char *bitrates[]={"3 Mbps","5 Mbps","8 Mbps","10 Mbps"};
static const char *volumes[]={"Mute","25%","50%","70%","80%","90%","100%"};
static const char *pointers[]={"Slow (40%)","Normal (60%)","Fast (80%)","Very fast (100%)"};
static const char *codecs[]={"H.264","HEVC (experimental)","Automatic (HEVC, then H.264)"};
static const int bitrate_values[]={3000,5000,8000,10000},volume_values[]={0,25,50,70,80,90,100},pointer_values[]={40,60,80,100};
static void start_request(int op);static void rebuild_pcs(void);static void update_buttons(void);
static gboolean valid_ip(const char *s){struct in_addr a;return s&&inet_pton(AF_INET,s,&a)==1&&a.s_addr!=INADDR_ANY&&a.s_addr!=INADDR_BROADCAST;}
static void message(const char *s){dove_status(status,s);}
static void append_log(const char *s){
 GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));GtkTextIter end,start;
 gtk_text_buffer_get_end_iter(b,&end);char *safe=g_utf8_validate(s,-1,NULL)?g_strdup(s):g_utf8_make_valid(s,-1);gtk_text_buffer_insert(b,&end,safe,-1);g_free(safe);
 int n=gtk_text_buffer_get_char_count(b);if(n>65536){gtk_text_buffer_get_start_iter(b,&start);gtk_text_buffer_get_iter_at_offset(b,&end,n-49152);gtk_text_buffer_delete(b,&start,&end);}
}
static gboolean save_config(void){
 gchar *data=g_key_file_to_data(config,NULL,NULL);GError *error=NULL;gboolean ok=g_file_set_contents(CONFIG,data,-1,&error);g_free(data);
 if(!ok){message(error?error->message:"Could not save settings to USB.");g_clear_error(&error);}return ok;
}
static void save_pcs(void){
 char *ips[MAX_PCS];for(int i=0;i<count;i++){ips[i]=pcs[i].ip;g_key_file_set_string(config,pcs[i].ip,"name",pcs[i].name);g_key_file_set_boolean(config,pcs[i].ip,"remote",pcs[i].remote);}
 g_key_file_set_string_list(config,"Computers","addresses",(const gchar *const*)ips,count);
 if(selected>=0)g_key_file_set_string(config,"Computers","selected",pcs[selected].ip);
 save_config();
}
static int add_pc(const char *ip,const char *name){
 if(!valid_ip(ip))return -1;
 for(int i=0;i<count;i++)if(!strcmp(pcs[i].ip,ip))return i;
 if(count==MAX_PCS){message("The saved PC list is full.");return -1;}
 int i=count++;memset(&pcs[i],0,sizeof pcs[i]);pcs[i].ip=g_strdup(ip);pcs[i].name=g_strdup(name&&*name?name:ip);pcs[i].paired=-1;pcs[i].hevc=-1;pcs[i].remote=g_key_file_get_boolean(config,ip,"remote",NULL);pcs[i].chosen=g_key_file_get_integer(config,ip,"application",NULL);return i;
}
static gboolean favorite(int id){if(selected<0)return FALSE;char key[32];g_snprintf(key,sizeof key,"favorite-%d",id);return g_key_file_get_boolean(config,pcs[selected].ip,key,NULL);}
static void rebuild_pcs(void){
 loading=TRUE;gtk_list_store_clear(pc_model);GtkTreeIter it;
 for(int i=0;i<count;i++){const char *state=pcs[i].paired==1?"Paired":pcs[i].paired==0?"Needs pairing":pcs[i].paired==-2?"Offline":"Not checked";char *display=pcs[i].remote?g_strdup_printf("%s (Remote)",pcs[i].name):g_strdup(pcs[i].name);gtk_list_store_append(pc_model,&it);gtk_list_store_set(pc_model,&it,PC_NAME,display,PC_IP,pcs[i].ip,PC_STATE,state,PC_APP,"Connect to load apps",PC_OPTIONS,pcs[i].apps,PC_ENABLED,pcs[i].paired==1&&!child,-1);g_free(display);
  if(pcs[i].apps){GtkTreeIter a;for(gboolean ok=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(pcs[i].apps),&a);ok;ok=gtk_tree_model_iter_next(GTK_TREE_MODEL(pcs[i].apps),&a)){int id;char *name;gtk_tree_model_get(GTK_TREE_MODEL(pcs[i].apps),&a,0,&name,1,&id,-1);if(id==pcs[i].chosen){char *label=g_strdup(name);gtk_list_store_set(pc_model,&it,PC_APP,label,-1);g_free(label);}g_free(name);}}}
 if(selected>=0&&selected<count){GtkTreePath *p=gtk_tree_path_new_from_indices(selected,-1);gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(pc_view)),p);gtk_tree_path_free(p);}loading=FALSE;
 update_buttons();
}
static int selected_app(void){GtkTreeModel *m;GtkTreeIter it;int id=0;if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(app_view)),&m,&it))gtk_tree_model_get(m,&it,APP_ID,&id,-1);return id;}
static void update_buttons(void){
 gboolean pc=selected>=0,ready=pc&&pcs[selected].paired==1;
 GtkTreeIter row;int i=0;for(gboolean ok=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(pc_model),&row);ok;ok=gtk_tree_model_iter_next(GTK_TREE_MODEL(pc_model),&row),i++){gboolean enabled;gtk_tree_model_get(GTK_TREE_MODEL(pc_model),&row,PC_ENABLED,&enabled,-1);gboolean next=pcs[i].paired==1&&!child;if(enabled!=next)gtk_list_store_set(pc_model,&row,PC_ENABLED,next,-1);}
 gtk_widget_set_sensitive(row_launch,ready&&!child&&pcs[selected].chosen>0);gtk_widget_set_sensitive(connect_button,pc&&!child);gtk_widget_set_sensitive(pair_button,pc&&!child&&pcs[selected].paired==0);gtk_widget_set_sensitive(unpair_button,ready&&!child);
 gtk_widget_set_sensitive(launch_button,ready&&!child&&selected_app()>0);gtk_widget_set_sensitive(resume_button,ready&&!child&&pcs[selected].current>0);gtk_widget_set_sensitive(cancel_button,child!=0);
 char *text=pc?g_strdup_printf("%s (%s) - %s",pcs[selected].name,pcs[selected].ip,ready?"Connected and paired. Choose an application from the dropdown.":pcs[selected].paired==0?"PC found. Pair it to load applications.":pcs[selected].paired==-2?"PC is unavailable. Check its network and Sunshine.":"Choose Connect to check the PC and load its applications."):g_strdup("Add a PC address or choose Find PCs to begin.");
 gtk_label_set_text(GTK_LABEL(summary),text);g_free(text);
 text=pc?g_strdup_printf("PC: %s  (%s) - %s",pcs[selected].name,pcs[selected].ip,ready?"Paired":"Not connected"):g_strdup("No PC selected");gtk_label_set_text(GTK_LABEL(host_label),text);g_free(text);
 GdkWindow *native=gtk_widget_get_window(window);if(native){GdkCursor *cursor=gdk_cursor_new_from_name(gtk_widget_get_display(window),child?"left_ptr_watch":"left_ptr");if(cursor){gdk_window_set_cursor(native,cursor);gdk_cursor_unref(cursor);}}
}
static void pc_changed(GtkTreeSelection *selection,gpointer unused){
 (void)unused;if(loading||child)return;GtkTreeModel *m;GtkTreeIter it;if(!gtk_tree_selection_get_selected(selection,&m,&it))return;
 char *ip=NULL;gtk_tree_model_get(m,&it,PC_IP,&ip,-1);int previous=selected;for(int i=0;i<count;i++)if(!strcmp(pcs[i].ip,ip))selected=i;g_free(ip);if(previous==selected)return;
 gtk_list_store_clear(app_model);gtk_label_set_text(GTK_LABEL(pin_label),"");save_pcs();update_buttons();
}
static void pc_app_changed(GtkTreeView *view,const gchar *path,GtkTreeModel *choice_model,GtkTreeIter *choice,gpointer unused){
 (void)view;(void)choice_model;(void)unused;if(child||selected<0)return;GtkTreeIter row;if(!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(pc_model),&row,path))return;
 char *ip=NULL;GtkTreeModel *options=NULL;gtk_tree_model_get(GTK_TREE_MODEL(pc_model),&row,PC_IP,&ip,PC_OPTIONS,&options,-1);
 if(options&&!strcmp(ip,pcs[selected].ip)){int id;char *name=NULL;gtk_tree_model_get(options,choice,0,&name,1,&id,-1);pcs[selected].chosen=id;char *label=g_strdup(name);gtk_list_store_set(pc_model,&row,PC_APP,label,-1);g_free(label);g_key_file_set_integer(config,ip,"application",id);save_config();g_free(name);update_buttons();}
 if(options){g_object_unref(options);}g_free(ip);
}
static void launch_row(GtkWidget *w,gpointer unused){(void)w;(void)unused;if(child||selected<0||pcs[selected].chosen<=0)return;pending_app=pcs[selected].chosen;start_request(1);}
static void app_changed(GtkTreeSelection *s,gpointer unused){(void)s;(void)unused;update_buttons();}
static gboolean filter_app(GtkTreeModel *m,GtkTreeIter *it,gpointer unused){
 (void)unused;char *name=NULL;gtk_tree_model_get(m,it,APP_NAME,&name,-1);if(!name)return FALSE;char *a=g_utf8_casefold(name,-1),*b=g_utf8_casefold(gtk_entry_get_text(GTK_ENTRY(search)),-1);gboolean show=strstr(a,b)!=NULL;g_free(name);g_free(a);g_free(b);return show;
}
static gint sort_apps(GtkTreeModel *m,GtkTreeIter *a,GtkTreeIter *b,gpointer unused){
 (void)unused;gboolean fa,fb;char *na=NULL,*nb=NULL;gtk_tree_model_get(m,a,APP_FAV,&fa,APP_NAME,&na,-1);gtk_tree_model_get(m,b,APP_FAV,&fb,APP_NAME,&nb,-1);int result=fa!=fb?(fb-fa):g_utf8_collate(na?na:"",nb?nb:"");g_free(na);g_free(nb);return result;
}
static void search_changed(GtkEditable *e,gpointer unused){(void)e;(void)unused;gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(app_filter));update_buttons();}
static void toggle_favorite(GtkCellRendererToggle *r,gchar *path,gpointer unused){
 (void)r;(void)unused;if(selected<0||child)return;GtkTreeIter it;if(!gtk_tree_model_get_iter_from_string(app_filter,&it,path))return;
 GtkTreeIter original;gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(app_filter),&original,&it);int id;gboolean fav;gtk_tree_model_get(GTK_TREE_MODEL(app_model),&original,APP_ID,&id,APP_FAV,&fav,-1);
 char key[32];g_snprintf(key,sizeof key,"favorite-%d",id);g_key_file_set_boolean(config,pcs[selected].ip,key,!fav);save_config();gtk_list_store_set(app_model,&original,APP_FAV,!fav,-1);
}
static void parse_line(const char *line){
 append_log(line);
 if(request==0&&g_str_has_prefix(line,"FOUND_PC=")){char *ip=g_strdup(line+9);g_strstrip(ip);add_pc(ip,NULL);g_free(ip);rebuild_pcs();save_pcs();return;}
 if(selected<0)return;
 PC *p=&pcs[selected];
 const char *name=strstr(line,"<hostname>");if(name){name+=10;const char *end=strstr(name,"</hostname>");if(end&&end>name){char *s=g_strndup(name,end-name);g_free(p->name);p->name=s;}}
 if(g_str_has_prefix(line,"PAIRED="))p->paired=atoi(line+7)?1:0;
 if(strstr(line,"MOONLIGHT_PAIRED=1"))p->paired=1;
 if(g_str_has_prefix(line,"CURRENT_APP="))p->current=atoi(line+12);
 if(g_str_has_prefix(line,"CODEC_HEVC="))p->hevc=atoi(line+11)?1:0;
 if(g_str_has_prefix(line,"MOONLIGHT_PIN=")){
  char pin[5]={0};memcpy(pin,line+14,MIN(strlen(line+14),(size_t)4));char *s=g_strdup_printf("Pairing PIN:  %s\nOn the PC, open Sunshine > PIN and enter these four digits.\nKeep this window open until pairing completes.",pin);gtk_label_set_text(GTK_LABEL(pin_label),s);g_free(s);
 }
 if(g_str_has_prefix(line,"APP=")){
  char *end=NULL;long id=strtol(line+4,&end,10);if(id<=0||id>G_MAXINT||!end||*end!=':')return;
  GtkTreeIter it;gboolean exists=FALSE;for(gboolean ok=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(app_model),&it);ok;ok=gtk_tree_model_iter_next(GTK_TREE_MODEL(app_model),&it)){int old;gtk_tree_model_get(GTK_TREE_MODEL(app_model),&it,APP_ID,&old,-1);if(old==id){exists=TRUE;break;}}
  if(!exists){char *title=g_strdup(end+1);g_strchomp(title);char *safe=g_utf8_make_valid(title,-1);gtk_list_store_append(app_model,&it);gtk_list_store_set(app_model,&it,APP_ID,(int)id,APP_NAME,safe,APP_FAV,favorite(id),-1);g_free(safe);g_free(title);}
 }
}
static void drain_output(void){
 if(!channel)return;
 for(;;){char *line=NULL;GError *e=NULL;GIOStatus rc=g_io_channel_read_line(channel,&line,NULL,NULL,&e);if(rc==G_IO_STATUS_NORMAL&&line){parse_line(line);g_free(line);}else{g_free(line);g_clear_error(&e);break;}}
}
static gboolean output_ready(GIOChannel *io,GIOCondition cond,gpointer unused){(void)io;(void)unused;drain_output();if(cond&(G_IO_HUP|G_IO_ERR)){io_watch=0;return FALSE;}return TRUE;}
static void finish_stream(int id){
 GtkTreeIter it;gboolean found=FALSE;for(gboolean ok=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(app_model),&it);ok;ok=gtk_tree_model_iter_next(GTK_TREE_MODEL(app_model),&it)){int app;gtk_tree_model_get(GTK_TREE_MODEL(app_model),&it,APP_ID,&app,-1);if(app==id)found=TRUE;}
 if(!found){message("This application is no longer listed by the PC. Refresh and choose another.");return;}
 int c[5];for(int i=0;i<5;i++)c[i]=gtk_combo_box_get_active(GTK_COMBO_BOX(choices[i]));
 if(c[4]==1&&pcs[selected].hevc!=1){c[4]=0;gtk_combo_box_set_active(GTK_COMBO_BOX(choices[4]),0);append_log("HEVC is not advertised by this PC. Using H.264.\n");}
 int width=c[0]==2?1280:1920,height=c[0]==2?720:1080,fps=c[0]==1?30:60;
 const char *codec=c[4]==2?(pcs[selected].hevc==1?"auto-hevc":"auto-h264"):(c[4]==1?"hevc":"h264");
 char *data=g_strdup_printf("%s %d %d %d %d %d %d %d %s %d\n",pcs[selected].ip,width,height,fps,bitrate_values[c[1]],volume_values[c[2]],pointer_values[c[3]],id,codec,gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(stream_info)));GError *error=NULL;
 if(!g_file_set_contents("/tmp/drmosa-moonlight-session",data,-1,&error)){message(error->message);g_error_free(error);g_free(data);return;}g_free(data);
 exit_code=10;gtk_widget_destroy(window);
}
static void child_done(GPid pid,gint code,gpointer unused){
 (void)unused;drain_output();if(io_watch){g_source_remove(io_watch);io_watch=0;}if(channel){g_io_channel_unref(channel);channel=NULL;}stdout_fd=-1;g_spawn_close_pid(pid);child=0;child_watch=0;
 gboolean ok=WIFEXITED(code)&&WEXITSTATUS(code)==0;int op=request;request=-1;
 gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress),0);gtk_widget_set_sensitive(pc_view,TRUE);
 if(op==0){rebuild_pcs();message(ok?"Search finished. Select a PC and choose Connect.":"No PC discovered. Add its IPv4 address manually.");}
 else if(selected>=0){
  if(!ok&&op!=2)pcs[selected].paired=-2;
  if(ok){pcs[selected].checked=g_get_monotonic_time();if(op==4)pcs[selected].paired=0;}
  rebuild_pcs();save_pcs();
  message(ok?(op==2?"Pairing completed. Loading applications...":op==3?"PC application ended. Refreshing...":op==4?"PC unpaired.":pcs[selected].paired==1?"Ready. Select an application and choose Start.":"PC found. Choose Pair to continue."):"Connection failed or cancelled. See Help & logs for details.");
  if(ok&&(op==2||op==3)){start_request(1);return;}
  if(ok&&op==1&&pcs[selected].paired==1){
   PC *p=&pcs[selected];if(p->apps)g_object_unref(p->apps);p->apps=gtk_list_store_new(2,G_TYPE_STRING,G_TYPE_INT);GtkTreeIter source,dest;gboolean found=FALSE;
   for(gboolean more=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(app_model),&source);more;more=gtk_tree_model_iter_next(GTK_TREE_MODEL(app_model),&source)){char *name;int id;gtk_tree_model_get(GTK_TREE_MODEL(app_model),&source,APP_NAME,&name,APP_ID,&id,-1);gtk_list_store_append(p->apps,&dest);gtk_list_store_set(p->apps,&dest,0,name,1,id,-1);if(id==p->chosen)found=TRUE;g_free(name);}
   if(!found&&gtk_tree_model_get_iter_first(GTK_TREE_MODEL(p->apps),&dest))gtk_tree_model_get(GTK_TREE_MODEL(p->apps),&dest,1,&p->chosen,-1);
   rebuild_pcs();GtkTreeIter it;if(gtk_tree_model_get_iter_first(app_filter,&it))gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(app_view)),&it);
   if(p->hevc==0&&gtk_combo_box_get_active(GTK_COMBO_BOX(choices[4]))==1){gtk_combo_box_set_active(GTK_COMBO_BOX(choices[4]),0);message("Ready. This PC does not advertise HEVC; H.264 selected.");}}
 }
 if(!ok&&op!=0&&selected>=0){char *failure=g_strdup_printf("Cannot connect to %s (%s). Check the address and Sunshine; try another saved PC. Details are in Help & logs.",pcs[selected].name,pcs[selected].ip);message(failure);g_free(failure);}
 int app=pending_app;pending_app=0;update_buttons();if(app&&ok&&selected>=0&&pcs[selected].paired==1)finish_stream(app);
}
static void child_setup(gpointer data){alarm(GPOINTER_TO_INT(data)==2?180:GPOINTER_TO_INT(data)==0?10:25);dup2(STDOUT_FILENO,STDERR_FILENO);}
static void start_request(int op){
 if(child||(op&&selected<0))return;
 if(op==1){pcs[selected].paired=-1;pcs[selected].current=0;pcs[selected].hevc=-1;gtk_list_store_clear(app_model);}
 gchar *argv[]={"/bin/sh","/tmp/doveos-moonlight-ui/request.sh",op==0?"discover":op==2?"pair":op==3?"stop":op==4?"unpair":"check",op?pcs[selected].ip:NULL,NULL};
 GError *error=NULL;request=op;gtk_label_set_text(GTK_LABEL(pin_label),"");
 if(!g_spawn_async_with_pipes(NULL,argv,NULL,G_SPAWN_DO_NOT_REAP_CHILD,child_setup,GINT_TO_POINTER(op),&child,NULL,&stdout_fd,NULL,&error)){child=0;request=-1;message(error->message);g_error_free(error);return;}
 channel=g_io_channel_unix_new(stdout_fd);g_io_channel_set_encoding(channel,NULL,NULL);g_io_channel_set_flags(channel,G_IO_FLAG_NONBLOCK,NULL);g_io_channel_set_close_on_unref(channel,TRUE);
 io_watch=g_io_add_watch(channel,G_IO_IN|G_IO_HUP|G_IO_ERR,output_ready,NULL);child_watch=g_child_watch_add(child,child_done,NULL);
 gtk_widget_set_sensitive(pc_view,FALSE);message(op==0?"Searching the local network...":op==2?"Connecting for pairing. Watch for the PIN below...":"Connecting to PC...");update_buttons();
}
static void action(GtkWidget *w,gpointer data){(void)w;start_request(GPOINTER_TO_INT(data));}
static void launch(GtkWidget *w,gpointer data){(void)w;int id=GPOINTER_TO_INT(data)?(selected>=0?pcs[selected].current:0):selected_app();if(child||selected<0||id<=0)return;pending_app=id;start_request(1);}
static void app_activated(GtkTreeView *v,GtkTreePath *p,GtkTreeViewColumn *c,gpointer unused){(void)v;(void)p;(void)c;(void)unused;launch(NULL,NULL);}
static void cancel_request(GtkWidget *w,gpointer unused){(void)w;(void)unused;if(child){kill(child,SIGTERM);message("Cancelling connection request...");pending_app=0;}}
static void edit_pc(GtkWidget *w,gpointer mode_ptr){
 (void)w;int mode=GPOINTER_TO_INT(mode_ptr);gboolean rename=mode==1,remote=mode==2;if(child)return;if(rename&&selected<0)return;
 GtkWidget *d=gtk_dialog_new_with_buttons(rename?"Edit PC":remote?"Add Remote PC":"Add PC",GTK_WINDOW(window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,GTK_STOCK_CANCEL,GTK_RESPONSE_CANCEL,GTK_STOCK_SAVE,GTK_RESPONSE_ACCEPT,NULL);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(d)),*name=dove_textbox(rename?pcs[selected].name:""),*ip=dove_textbox(rename?pcs[selected].ip:"");
 gtk_container_set_border_width(GTK_CONTAINER(box),12);dove_add(box,dove_label("PC name (optional)"));dove_add(box,name);dove_add(box,dove_label(remote?"PC IPv4 address from DoveOS Remote Link":"IPv4 address"));dove_add(box,ip);dove_add(box,dove_label(remote?"Use the PC's Tailscale IPv4. Keep Sunshine and Tailscale running. Pair while at home or through Tailscale.":"Use the address shown by Sunshine on your PC. Both devices must have a network connection."));gtk_widget_show_all(d);
 while(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT){const char *addr=gtk_entry_get_text(GTK_ENTRY(ip));if(!valid_ip(addr)){dove_message(GTK_WINDOW(d),"Enter a valid IPv4 address, for example 192.168.1.20.");continue;}int duplicate=-1;for(int i=0;i<count;i++)if(!strcmp(pcs[i].ip,addr))duplicate=i;
  if(rename){if(duplicate>=0&&duplicate!=selected){dove_message(GTK_WINDOW(d),"That PC is already saved.");continue;}PC *p=&pcs[selected];g_free(p->ip);g_free(p->name);p->ip=g_strdup(addr);p->name=g_strdup(*gtk_entry_get_text(GTK_ENTRY(name))?gtk_entry_get_text(GTK_ENTRY(name)):addr);p->paired=-1;}
  else{selected=add_pc(addr,gtk_entry_get_text(GTK_ENTRY(name)));if(selected>=0&&remote)pcs[selected].remote=TRUE;}
  save_pcs();rebuild_pcs();gtk_list_store_clear(app_model);break;
 }gtk_widget_destroy(d);
}
static void remove_pc(GtkWidget *w,gpointer unused){
 (void)w;(void)unused;if(child||selected<0)return;GtkWidget *d=dove_messagebox(GTK_WINDOW(window),"Remove this PC from the saved list? Its pairing on the PC will be kept.",GTK_MESSAGE_QUESTION,GTK_BUTTONS_YES_NO);int answer=gtk_dialog_run(GTK_DIALOG(d));gtk_widget_destroy(d);if(answer!=GTK_RESPONSE_YES)return;
 g_free(pcs[selected].name);g_free(pcs[selected].ip);if(pcs[selected].apps)g_object_unref(pcs[selected].apps);memmove(pcs+selected,pcs+selected+1,(count-selected-1)*sizeof(PC));count--;selected=count?MIN(selected,count-1):-1;save_pcs();gtk_list_store_clear(app_model);rebuild_pcs();
}
static void destructive(GtkWidget *w,gpointer data){
 (void)w;int op=GPOINTER_TO_INT(data);if(child||selected<0)return;GtkWidget *d=dove_messagebox(GTK_WINDOW(window),op==3?"End the current application on the PC? Unsaved work in that application could be lost.":"Remove this dock's pairing from the selected PC? You will need to pair again.",GTK_MESSAGE_QUESTION,GTK_BUTTONS_YES_NO);int answer=gtk_dialog_run(GTK_DIALOG(d));gtk_widget_destroy(d);if(answer==GTK_RESPONSE_YES)start_request(op);
}
static void save_options(GtkWidget *w,gpointer unused){
 (void)w;(void)unused;if(loading)return;for(int i=0;i<5;i++){char key[12];g_snprintf(key,sizeof key,"option%d",i);g_key_file_set_integer(config,"Stream",key,gtk_combo_box_get_active(GTK_COMBO_BOX(choices[i])));}g_key_file_set_boolean(config,"Stream","show_info",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(stream_info)));save_config();message("Streaming settings saved.");
}
static void remote_preset(GtkWidget *w,gpointer unused){
 (void)w;(void)unused;gtk_combo_box_set_active(GTK_COMBO_BOX(choices[0]),2);gtk_combo_box_set_active(GTK_COMBO_BOX(choices[1]),1);gtk_combo_box_set_active(GTK_COMBO_BOX(choices[4]),2);message("Remote preset: 720p / 60 FPS, 5 Mbps, automatic codec. Adjust after an away-from-home test.");
}
static void export_log(GtkWidget *w,gpointer unused){
 (void)w;(void)unused;GtkWidget *dialog=dove_savefiledialog(GTK_WINDOW(window),"Save connection log");gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),"Moonlight-log.txt");
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT){char *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));GtkTextIter start,end;GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));gtk_text_buffer_get_bounds(b,&start,&end);char *text=gtk_text_buffer_get_text(b,&start,&end,FALSE);GError *error=NULL;if(g_file_set_contents(path,text,-1,&error))message("Connection log saved.");else{message(error->message);g_error_free(error);}g_free(path);g_free(text);}gtk_widget_destroy(dialog);
}
static void clear_log(GtkWidget *w,gpointer unused){(void)w;(void)unused;gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view)),"",-1);}
static gboolean verify_capture(gpointer unused){
 (void)unused;if(child&&verification_ticks++<200)return TRUE;
 printf("SDK=%s PCS=%d SELECTED=%s PAIRED=%d\n",DOVEOS_SDK_VERSION,count,selected>=0?pcs[selected].ip:"none",selected>=0?pcs[selected].paired:-1);
 GtkTreeIter it;int apps=0;for(gboolean ok=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(app_model),&it);ok;ok=gtk_tree_model_iter_next(GTK_TREE_MODEL(app_model),&it)){int id;char *name;gtk_tree_model_get(GTK_TREE_MODEL(app_model),&it,APP_ID,&id,APP_NAME,&name,-1);printf("VERIFIED_APP=%d:%s\n",id,name);g_free(name);apps++;}
 printf("APPS=%d GTK_UI_RESPONSIVE=1 VISIBLE_TABS=%d SHOW_STREAM_INFO=%d\n",apps,gtk_notebook_get_n_pages(GTK_NOTEBOOK(tabs)),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(stream_info)));fflush(stdout);
 if(selected>=0&&pcs[selected].apps){GtkTreeIter choice;char *path=g_strdup_printf("%d",selected);if(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(pcs[selected].apps),&choice)){GtkTreeIter next=choice;if(gtk_tree_model_iter_next(GTK_TREE_MODEL(pcs[selected].apps),&next))pc_app_changed(GTK_TREE_VIEW(pc_view),path,GTK_TREE_MODEL(pcs[selected].apps),&next,NULL);pc_app_changed(GTK_TREE_VIEW(pc_view),path,GTK_TREE_MODEL(pcs[selected].apps),&choice,NULL);printf("COMPUTER_DROPDOWN_SELECTED=%d\n",pcs[selected].chosen);}g_free(path);}

 gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs),0);while(gtk_events_pending())gtk_main_iteration();
 GdkWindow *native=gtk_widget_get_window(window);int width,height;gdk_drawable_get_size(GDK_DRAWABLE(native),&width,&height);
 GdkPixbuf *image=gdk_pixbuf_get_from_drawable(NULL,GDK_DRAWABLE(native),NULL,0,0,0,0,width,height);if(image){gdk_pixbuf_save(image,"/tmp/moonlight-computers.png","png",NULL,NULL);g_object_unref(image);}
 gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs),1);while(gtk_events_pending())gtk_main_iteration();
 image=gdk_pixbuf_get_from_drawable(NULL,GDK_DRAWABLE(native),NULL,0,0,0,0,width,height);if(image){gdk_pixbuf_save(image,"/tmp/moonlight-applications.png","png",NULL,NULL);g_object_unref(image);}
 if(verify_start&&selected>=0&&pcs[selected].paired==1&&apps>0){
  for(gboolean ok=gtk_tree_model_get_iter_first(app_filter,&it);ok;ok=gtk_tree_model_iter_next(app_filter,&it)){char *name=NULL;gtk_tree_model_get(app_filter,&it,APP_NAME,&name,-1);gboolean desktop=name&&!g_ascii_strcasecmp(name,"Desktop");g_free(name);if(desktop){gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(app_view)),&it);launch(NULL,NULL);return FALSE;}}
 }
 exit_code=selected>=0&&pcs[selected].paired==1&&apps>0?0:1;gtk_widget_destroy(window);return FALSE;
}
static gboolean pulse(gpointer unused){(void)unused;if(child)gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress));return TRUE;}
static gboolean destroy_requested(GtkWidget *w,GdkEvent *e,gpointer unused){(void)w;(void)e;(void)unused;if(child){cancel_request(NULL,NULL);message("Cancelling request. Close again when it finishes.");return TRUE;}return FALSE;}
static void destroyed(GtkWidget *w,gpointer unused){(void)w;(void)unused;gtk_main_quit();}
static GtkWidget *button(GtkWidget *box,const char *text,GCallback cb,gpointer data){GtkWidget *b=dove_button(text);gtk_box_pack_start(GTK_BOX(box),b,FALSE,FALSE,0);g_signal_connect(b,"clicked",cb,data);return b;}
static GtkWidget *page(const char *name){GtkWidget *box=dove_page();dove_tab_add(tabs,dove_scroll(box),name);return box;}
static void load_config(void){
 config=g_key_file_new();g_key_file_load_from_file(config,CONFIG,G_KEY_FILE_NONE,NULL);gsize n=0;char **ips=g_key_file_get_string_list(config,"Computers","addresses",&n,NULL);
 if(ips){for(gsize i=0;i<n;i++){char *name=g_key_file_get_string(config,ips[i],"name",NULL);add_pc(ips[i],name);g_free(name);}g_strfreev(ips);}
 else{add_pc("169.254.244.136","Direct Ethernet PC");gchar *old=NULL;if(g_file_get_contents("/media/usb0/DrMoSa/moonlight-hosts.txt",&old,NULL,NULL)){char **lines=g_strsplit(old,"\n",-1);for(int i=0;lines[i];i++)add_pc(g_strstrip(lines[i]),NULL);g_strfreev(lines);g_free(old);}}
 char *last=g_key_file_get_string(config,"Computers","selected",NULL);selected=count?0:-1;for(int i=0;i<count;i++)if(last&&!strcmp(last,pcs[i].ip))selected=i;g_free(last);
}
int main(int argc,char **argv){
 for(int i=1;i<argc;i++){if(!strcmp(argv[i],"--verify"))verifying=TRUE;if(!strcmp(argv[i],"--verify-start"))verifying=verify_start=TRUE;}
 gtk_init(&argc,&argv);window=dove_form("Moonlight",920,600);dove_window_fit(window,920,600);gtk_window_set_wmclass(GTK_WINDOW(window),"doveos-control-x","doveos-control-x");gtk_window_set_icon_from_file(GTK_WINDOW(window),"/media/usb0/DoveOS-v2/ClassicApps/icons/moonlight.png",NULL);
 g_signal_connect(window,"delete-event",G_CALLBACK(destroy_requested),NULL);g_signal_connect(window,"destroy",G_CALLBACK(destroyed),NULL);
 GtkWidget *outer=dove_panel(FALSE,6);gtk_container_set_border_width(GTK_CONTAINER(outer),8);gtk_container_add(GTK_CONTAINER(window),outer);
 GdkPixbuf *moonlight_header_icon=gdk_pixbuf_new_from_file_at_scale("/media/usb0/DoveOS-v2/ClassicApps/icons/moonlight.png",48,48,TRUE,NULL);
 GtkWidget *header=dove_panel(TRUE,8);gtk_box_pack_start(GTK_BOX(header),dove_image_stable(gtk_image_new_from_pixbuf(moonlight_header_icon)),FALSE,FALSE,0);if(moonlight_header_icon)g_object_unref(moonlight_header_icon);dove_add(header,dove_label("Moonlight\nYour PCs, desktops and games"));host_label=dove_label("");gtk_label_set_line_wrap(GTK_LABEL(host_label),FALSE);gtk_box_pack_end(GTK_BOX(header),host_label,FALSE,FALSE,0);dove_add(outer,header);
 tabs=dove_tabs();gtk_box_pack_start(GTK_BOX(outer),tabs,TRUE,TRUE,0);
 GtkWidget *box=page("Computers"),*row=dove_panel(TRUE,6);dove_add(box,row);button(row,"Find PCs",G_CALLBACK(action),GINT_TO_POINTER(0));button(row,"Add PC...",G_CALLBACK(edit_pc),NULL);button(row,"Add Remote PC...",G_CALLBACK(edit_pc),GINT_TO_POINTER(2));button(row,"Edit...",G_CALLBACK(edit_pc),GINT_TO_POINTER(1));button(row,"Remove",G_CALLBACK(remove_pc),NULL);
 pc_model=gtk_list_store_new(PC_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,GTK_TYPE_TREE_MODEL,G_TYPE_BOOLEAN);const char *pt[]={"Computer","Address","Status"};pc_view=dove_listview(GTK_TREE_MODEL(pc_model),pt,3);gtk_box_pack_start(GTK_BOX(box),dove_scrolled(pc_view,-1,180),TRUE,TRUE,0);
 GtkTreeViewColumn *app_column=dove_table_dropdown(pc_view,"Application",PC_APP,PC_OPTIONS,PC_ENABLED,0,pc_app_changed,NULL);gtk_tree_view_column_set_min_width(app_column,200);dove_tooltip(pc_view,"Click the application dropdown of a connected PC, choose an app, then Start application.");
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(pc_view)),"changed",G_CALLBACK(pc_changed),NULL);
 gtk_tree_view_column_set_min_width(gtk_tree_view_get_column(GTK_TREE_VIEW(pc_view),0),200);gtk_tree_view_column_set_expand(gtk_tree_view_get_column(GTK_TREE_VIEW(pc_view),0),TRUE);gtk_tree_view_column_set_min_width(gtk_tree_view_get_column(GTK_TREE_VIEW(pc_view),1),150);gtk_tree_view_column_set_min_width(gtk_tree_view_get_column(GTK_TREE_VIEW(pc_view),2),130);
 summary=dove_label("");gtk_label_set_line_wrap(GTK_LABEL(summary),FALSE);dove_add(box,summary);pin_label=dove_label("");gtk_label_set_selectable(GTK_LABEL(pin_label),TRUE);dove_add(box,pin_label);
 row=dove_panel(TRUE,6);dove_add(box,row);connect_button=button(row,"Connect & load apps",G_CALLBACK(action),GINT_TO_POINTER(1));pair_button=button(row,"Pair PC",G_CALLBACK(action),GINT_TO_POINTER(2));row_launch=button(row,"Start Streaming",G_CALLBACK(launch_row),NULL);unpair_button=dove_button("Unpair PC");gtk_box_pack_end(GTK_BOX(row),unpair_button,FALSE,FALSE,0);g_signal_connect(unpair_button,"clicked",G_CALLBACK(destructive),GINT_TO_POINTER(4));
 box=page("Applications");row=dove_panel(TRUE,6);dove_add(box,row);gtk_box_pack_start(GTK_BOX(row),dove_label("Search"),FALSE,FALSE,0);search=dove_textbox("");gtk_box_pack_start(GTK_BOX(row),search,TRUE,TRUE,0);button(row,"Refresh apps",G_CALLBACK(action),GINT_TO_POINTER(1));g_signal_connect(search,"changed",G_CALLBACK(search_changed),NULL);
 app_model=gtk_list_store_new(APP_COLUMNS,G_TYPE_BOOLEAN,G_TYPE_STRING,G_TYPE_INT);gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(app_model),APP_NAME,sort_apps,NULL,NULL);gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(app_model),APP_NAME,GTK_SORT_ASCENDING);app_filter=gtk_tree_model_filter_new(GTK_TREE_MODEL(app_model),NULL);gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(app_filter),filter_app,NULL,NULL);app_view=gtk_tree_view_new_with_model(app_filter);gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(app_view),TRUE);
 GtkCellRenderer *toggle=gtk_cell_renderer_toggle_new();gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(app_view),-1,"Favorite",toggle,"active",APP_FAV,NULL);g_signal_connect(toggle,"toggled",G_CALLBACK(toggle_favorite),NULL);
 GtkCellRenderer *text=gtk_cell_renderer_text_new();gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(app_view),-1,"Application / game",text,"text",APP_NAME,NULL);gtk_tree_view_column_set_expand(gtk_tree_view_get_column(GTK_TREE_VIEW(app_view),1),TRUE);
 gtk_box_pack_start(GTK_BOX(box),dove_scrolled(app_view,-1,220),TRUE,TRUE,0);g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(app_view)),"changed",G_CALLBACK(app_changed),NULL);g_signal_connect(app_view,"row-activated",G_CALLBACK(app_activated),NULL);
 row=dove_panel(TRUE,6);dove_add(box,row);launch_button=button(row,"Start selected",G_CALLBACK(launch),NULL);resume_button=button(row,"Resume PC session",G_CALLBACK(launch),GINT_TO_POINTER(1));button(row,"End PC app...",G_CALLBACK(destructive),GINT_TO_POINTER(3));dove_add(box,dove_label("Double-click an application to start. Ctrl+Alt+Shift+Q ends streaming and returns to DoveOS."));
 box=page("Streaming");GtkWidget *table=gtk_table_new(5,2,FALSE);gtk_table_set_row_spacings(GTK_TABLE(table),10);gtk_table_set_col_spacings(GTK_TABLE(table),18);dove_add(box,table);
 const char *labels[]={"Resolution and frame rate","Video bitrate","Stream volume","Pointer speed","Video codec"};const char *const *items[]={resolution,bitrates,volumes,pointers,codecs};int lengths[]={3,4,7,4,3};
 for(int i=0;i<5;i++){choices[i]=dove_combobox(items[i],lengths[i],0);gtk_table_attach(GTK_TABLE(table),dove_label(labels[i]),0,1,i,i+1,GTK_FILL,GTK_FILL,0,0);gtk_table_attach(GTK_TABLE(table),choices[i],1,2,i,i+1,GTK_EXPAND|GTK_FILL,GTK_FILL,0,0);}
 button(box,"Use remote preset",G_CALLBACK(remote_preset),NULL);stream_info=gtk_check_button_new_with_label("Show streaming information");dove_add(box,stream_info);
 dove_tooltip(stream_info,"Shows live stream statistics in the top-left corner. Updates once per second; frame rates can fall on a still desktop.");
 dove_add(box,dove_line(TRUE));dove_add(box,dove_label("720p / 60 FPS uses less video bandwidth. 1080p / 60 FPS offers a sharper image.\nAutomatic prefers HEVC when supported, then uses H.264 if HEVC cannot start.\nStreaming is full-screen. These settings do not change the DoveOS desktop resolution."));
 box=page("Help & logs");GtkWidget *help=dove_label("Local: run Sunshine, Find PCs or add its local IPv4, Connect, then Start Streaming.\n\nRemote: connect the dock to the Internet. Keep Sunshine and Tailscale running on the home PC. DoveOS starts its Tailscale tunnel when Moonlight opens. Use the PC's private IPv4 from DoveOS Remote Link in Add Remote PC. Choose Use remote preset, Connect & load apps, then Start Streaming.\n\nKeep the home PC awake. Direct public IP access may need router forwarding; the tested away-home route is Tailscale.\n\nKeyboard and touchpad are forwarded. Ctrl+Alt+Shift+Q returns to DoveOS. Check Sunshine and the PC firewall if connecting fails.");dove_add(box,help);
 row=dove_panel(TRUE,6);dove_add(box,row);button(row,"Save log...",G_CALLBACK(export_log),NULL);button(row,"Clear log",G_CALLBACK(clear_log),NULL);
 log_view=dove_textarea("");gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view),FALSE);gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_view),FALSE);gtk_box_pack_start(GTK_BOX(box),dove_scrolled(log_view,-1,90),TRUE,TRUE,0);
 progress=dove_progressbar(0);gtk_widget_set_size_request(progress,-1,8);dove_add(outer,progress);row=dove_panel(TRUE,6);dove_add(outer,row);status=dove_statusbar();gtk_box_pack_start(GTK_BOX(row),status,TRUE,TRUE,0);cancel_button=button(row,"Cancel request",G_CALLBACK(cancel_request),NULL);
 load_config();loading=TRUE;int defaults[]={0,2,3,1,0};char *old=NULL;if(!g_key_file_has_group(config,"Stream")&&g_file_get_contents("/media/usb0/DrMoSa/moonlight-settings.txt",&old,NULL,NULL)){sscanf(old,"%d %d %d %d %d",&defaults[0],&defaults[1],&defaults[2],&defaults[3],&defaults[4]);g_free(old);}
 defaults[4]=2;
 for(int i=0;i<5;i++){char key[12];g_snprintf(key,sizeof key,"option%d",i);int value=g_key_file_has_key(config,"Stream",key,NULL)?g_key_file_get_integer(config,"Stream",key,NULL):defaults[i];gtk_combo_box_set_active(GTK_COMBO_BOX(choices[i]),CLAMP(value,0,lengths[i]-1));g_signal_connect(choices[i],"changed",G_CALLBACK(save_options),NULL);}
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(stream_info),g_key_file_get_boolean(config,"Stream","show_info",NULL));g_signal_connect(stream_info,"toggled",G_CALLBACK(save_options),NULL);loading=FALSE;
 GtkWidget *application_tools=gtk_notebook_get_nth_page(GTK_NOTEBOOK(tabs),1);g_object_ref(application_tools);gtk_notebook_remove_page(GTK_NOTEBOOK(tabs),1);
 rebuild_pcs();gtk_widget_show_all(window);message("Choose a PC and Connect, or Find PCs to search your network.");guint timer=dove_timer(150,pulse,NULL);
 if(verifying){rebuild_pcs();start_request(1);dove_timer(150,verify_capture,NULL);}else if(selected>=0&&!pcs[selected].remote)start_request(1);
 gtk_main();g_source_remove(timer);gtk_widget_destroy(application_tools);g_object_unref(application_tools);
 if(child){kill(child,SIGTERM);waitpid(child,NULL,0);}if(channel)g_io_channel_unref(channel);g_key_file_free(config);for(int i=0;i<count;i++){g_free(pcs[i].ip);g_free(pcs[i].name);if(pcs[i].apps)g_object_unref(pcs[i].apps);}return exit_code;
}
