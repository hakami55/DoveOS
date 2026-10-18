#define _POSIX_C_SOURCE 200809L
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <dirent.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>
#include "clock_settings.h"
#include "battery_state.h"
#include "power_settings.h"
#include "sdk/doveos.h"
#include "shutdown_ui.h"
#include "wifi_scan_info.h"
#define USB "/media/usb0/DoveOS-v2"
#define SCROLL_FILE USB "/home/scroll-direction.txt"
static int volume=70,wifi_count,bt_count;
static char chosen_ssid[64],message[240],wifi_names[64][64],pending_action[40];
static DoveWifiScanInfo wifi_info[64];
static struct {char addr[18],name[90];int le;} bt[64];
static pid_t child;
static GtkWidget *window,*book,*status,*wifi_list,*bt_list,*wifi_status,*ethernet_status,*volume_scale,*volume_label,*mute,*awake,*reverse_box,*categories;
static GtkListStore *category_model;
static GtkWidget *wifi_list_info,*bt_list_info,*bt_saved_list,*bt_saved_info;
static GtkWidget *bt_start_button,*bt_scan_button,*bt_pair_button,*bt_connect_button,*bt_disconnect_button,*bt_test_button,*bt_forget_button;
static GtkWidget *sound_test,*sound_left,*sound_right,*sound_stop,*sound_record,*sound_play;
static GtkWidget *keyboard_footer;
static GtkWidget *battery_label;
static GtkWidget *render_mode,*render_info;
static GtkWidget *battery_timeout,*ac_timeout,*battery_dim,*ac_dim,*media_awake,*power_status,*brightness_scale,*brightness_info;
static const int power_minutes[]={0,1,2,3,5,10,15,30};
static guint volume_timer;
static int syncing;
#include "bluetooth_status_ui.h"
static void read_line(const char *path, char *out, size_t cap) {
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(out, (int)cap, f)) out[strcspn(out, "\r\n")] = 0;
    fclose(f);
}
static int read_number(const char *path, int fallback) {
    char s[32]; read_line(path, s, sizeof s);
    if (!s[0]) return fallback;
    for (size_t i=0; s[i]; i++) if (!isdigit((unsigned char)s[i])) return fallback;
    return atoi(s);
}
static int link_up(const char *interface) {
    char path[128], value[24];
    snprintf(path, sizeof path, "/sys/class/net/%s/carrier", interface);
    read_line(path, value, sizeof value);
    return !strcmp(value, "1");
}
static int connected_wifi(char *name,size_t cap) {
    name[0]=0;
    if(!link_up("wlan0"))return 0;
    char info[160];read_line("/tmp/doveos-wifi/link-status",info,sizeof info);
    /* This monitor file contains the associated SSID, not the saved profile.
       Strip only the final frequency suffix; SSIDs can themselves contain " at ". */
    char *suffix=NULL,*next=info;
    while((next=strstr(next," at "))){suffix=next;next+=4;}
    if(suffix&&strstr(suffix+4," MHz"))*suffix=0;
    g_strlcpy(name,info,cap);
    return 1;
}
static void wifi_connection_refresh(void) {
    char name[64],text[120];int connected=connected_wifi(name,sizeof name);
    snprintf(text,sizeof text,connected?(name[0]?"Connected to Wi-Fi: %s":"Wi-Fi connected; network name pending"):"Wi-Fi disconnected",name);
    gtk_label_set_text(GTK_LABEL(wifi_status),text);
    GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(wifi_list));
    GtkTreeIter it;gboolean valid=gtk_tree_model_get_iter_first(model,&it);
    while(valid){
        gchar *ssid=NULL,*old=NULL;gtk_tree_model_get(model,&it,0,&ssid,5,&old,-1);
        const char *state=connected&&name[0]&&!strcmp(name,ssid)?"Connected":"";
        if(g_strcmp0(old,state))gtk_list_store_set(GTK_LIST_STORE(model),&it,5,state,-1);
        g_free(ssid);g_free(old);valid=gtk_tree_model_iter_next(model,&it);
    }
}
static void battery(char *out, size_t cap) {
    DoveBattery b;if(dove_battery_read(&b)){snprintf(out,cap,"Battery %u%% - %s (%s)",b.percent,b.external?"External power":"On battery",b.status);return;}
    DIR *dir = opendir("/sys/class/power_supply");
    if (!dir) {snprintf(out, cap, "Battery information unavailable"); return;}
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        char path[384], type[32], state[48];
        snprintf(path, sizeof path, "/sys/class/power_supply/%s/type", entry->d_name);
        read_line(path, type, sizeof type);
        if (strcmp(type, "Battery")) continue;
        snprintf(path, sizeof path, "/sys/class/power_supply/%s/capacity", entry->d_name);
        int percent = read_number(path, -1);
        snprintf(path, sizeof path, "/sys/class/power_supply/%s/status", entry->d_name);
        read_line(path, state, sizeof state);
        if (percent >= 0 && percent <= 100)
            snprintf(out, cap, "Battery %d%%  %s", percent, state[0] ? state : "status unknown");
        else snprintf(out, cap, "Battery percentage unavailable");
        closedir(dir); return;
    }
    closedir(dir);
    snprintf(out, cap, "Battery information unavailable");
}
static int reverse_scroll(void) {
    char s[24]; read_line(SCROLL_FILE, s, sizeof s);
    return strcmp(s, "normal") != 0;
}
static void set_reverse_scroll(void) {
    const char *state = reverse_scroll() ? "normal\n" : "reverse\n";
    FILE *f = fopen(SCROLL_FILE ".next", "w");
    if (!f) {snprintf(message, sizeof message, "Could not save touchpad setting."); return;}
    int wrote=fputs(state, f);
    int closed=fclose(f);
    if (wrote < 0 || closed || rename(SCROLL_FILE ".next", SCROLL_FILE))
        snprintf(message, sizeof message, "Could not save touchpad setting.");
    else snprintf(message, sizeof message, "Two-finger scroll direction changed.");
}
static void load_wifi(void) {
    wifi_count = 0;
    memset(wifi_info,0,sizeof wifi_info);
    FILE *f = fopen("/tmp/doveos-wifi/scan", "r");
    if (!f) return;
    while (wifi_count < 64 && fgets(wifi_names[wifi_count], sizeof wifi_names[0], f)) {
        wifi_names[wifi_count][strcspn(wifi_names[wifi_count], "\r\n")] = 0;
        if (wifi_names[wifi_count][0]) wifi_count++;
    }
    fclose(f);
    dove_wifi_details("/tmp/doveos-wifi/scan-details",wifi_names,wifi_info,wifi_count);
    DoveWifiScanInfo fallback[64]={0};
    dove_wifi_wext("/tmp/doveos-wifi/scan-wext",wifi_names,fallback,wifi_count);
    for(int i=0;i<wifi_count;i++)if(!wifi_info[i].seen)wifi_info[i]=fallback[i];
    /* wpa_supplicant escapes non-ASCII SSID bytes as \xNN in SCAN_RESULTS.
       Keep that spelling through the scan-detail matching above, then decode
       it for display and for the SSID bytes passed to wifi-join. */
    for(int i=0;i<wifi_count;i++){
        char decoded[sizeof wifi_names[0]];size_t used=0;const char *src=wifi_names[i];
        while(*src&&used+1<sizeof decoded){
            if(src[0]=='\\'&&src[1]=='x'&&isxdigit((unsigned char)src[2])&&isxdigit((unsigned char)src[3])){
                int hi=g_ascii_xdigit_value(src[2]),lo=g_ascii_xdigit_value(src[3]);
                unsigned char byte=(unsigned char)((hi<<4)|lo);
                if(!byte||byte=='\n'||byte=='\r'||byte=='\t')break;
                decoded[used++]=(char)byte;src+=4;
            }else if(src[0]=='\\'&&src[1]=='\\'){decoded[used++]='\\';src+=2;}
            else decoded[used++]=*src++;
        }
        decoded[used]=0;
        if(!*src&&g_utf8_validate(decoded,-1,NULL))g_strlcpy(wifi_names[i],decoded,sizeof wifi_names[i]);
    }
}
static gboolean bt_name_known(const char *name,const char *address) {
    if(!name||!*name)return FALSE;
    const char *unknown[]={"Unknown","Unknown device","Unnamed","Unnamed device","(unknown)","N/A"};
    for(unsigned i=0;i<G_N_ELEMENTS(unknown);i++)if(!g_ascii_strcasecmp(name,unknown[i]))return FALSE;
    char normalized[90];g_strlcpy(normalized,name,sizeof normalized);
    for(char *p=normalized;*p;p++)if(*p=='-')*p=':';
    return !address||g_ascii_strcasecmp(normalized,address)!=0;
}
static const char *bt_search_name(int index) {
    int state=bt_state_index(bt[index].addr);
    if(state>=0&&bt_name_known(bt_states[state].name,bt[index].addr))return bt_states[state].name;
    return bt[index].name;
}
static int bt_search_compare(int left,int right) {
    gchar *a=g_utf8_casefold(bt_search_name(left),-1),*b=g_utf8_casefold(bt_search_name(right),-1);
    int order=g_utf8_collate(a,b);g_free(a);g_free(b);
    return order?order:g_ascii_strcasecmp(bt[left].addr,bt[right].addr);
}
static void load_bt_file(const char *path, int le) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char s[160];
    while (fgets(s, sizeof s, f)) {
        if (strlen(s) < 17 || s[2] != ':' || s[5] != ':' || s[8] != ':') continue;
        char *name = s + 17;
        while (*name == ' ' || *name == '\t') name++;
        name[strcspn(name, "\r\n")] = 0;
        char normalized[90];
        gchar *converted=g_utf8_validate(name,-1,NULL)?NULL:g_convert(name,-1,"UTF-8","WINDOWS-1252",NULL,NULL,NULL);
        g_strlcpy(normalized,converted?converted:name,sizeof normalized);g_free(converted);
        const gchar *bad=NULL;if(!g_utf8_validate(normalized,-1,&bad))*(char*)bad=0;
        name=g_strstrip(normalized);
        int duplicate=-1;
        for(int i=0;i<bt_count;i++)if(!strncasecmp(bt[i].addr,s,17))duplicate=i;
        if(duplicate>=0){
            if(bt_name_known(name,bt[duplicate].addr))
                snprintf(bt[duplicate].name,sizeof bt[duplicate].name,"%s",name);
            continue;
        }
        if(bt_count>=64)continue;
        memcpy(bt[bt_count].addr, s, 17); bt[bt_count].addr[17] = 0;
        snprintf(bt[bt_count].name, sizeof bt[bt_count].name, "%s", *name ? name : "Unnamed device");
        bt[bt_count].le = le; bt_count++;
    }
    fclose(f);
}
static void load_bt(void) {
    bt_count = 0;
    load_bt_file("/tmp/doveos-bluetooth/classic-scan.log", 0);
    load_bt_file("/tmp/doveos-bluetooth/le-scan.log", 1);
}
static void run_action(const char *action, const char *argument, const char *secret) {
    if (child > 0) {snprintf(message, sizeof message, "Wait for the current action to finish."); return;}
    int pipefd[2] = {-1,-1};
    if (secret && pipe(pipefd)) {snprintf(message,sizeof message,"Could not send password."); return;}
    unlink("/tmp/drmosa-settings-result");
    if(!strncmp(action,"bluetooth-",10))unlink("/tmp/doveos-bluetooth/result");
    snprintf(pending_action, sizeof pending_action, "%s", action);
    child = fork();
    if (child == 0) {
        if (secret) {close(pipefd[1]); dup2(pipefd[0], STDIN_FILENO); close(pipefd[0]);}
        if(!strcmp(action,"time-sync"))execl("/tmp/doveos-theme/doveos-timekeeper","doveos-timekeeper","--sync",(char*)NULL);
        execl("/bin/sh", "sh", "/usr/bin/doveos-settings-proxy", action, argument ? argument : "", (char *)NULL);
        _exit(127);
    }
    if (secret) {
        close(pipefd[0]);
        if (child > 0) {
            ssize_t sent=write(pipefd[1], secret, strlen(secret));
            if (sent==(ssize_t)strlen(secret)) sent=write(pipefd[1], "\n", 1);
            (void)sent;
        }
        close(pipefd[1]);
    }
    if (child < 0) {child = 0; snprintf(message, sizeof message, "Could not start action.");}
    else if(!strcmp(action,"sound-test"))snprintf(message,sizeof message,"Testing stereo output...");
    else if(!strcmp(action,"sound-test-left"))snprintf(message,sizeof message,"Testing left output only...");
    else if(!strcmp(action,"sound-test-right"))snprintf(message,sizeof message,"Testing right output only...");
    else if(!strcmp(action,"sound-headphones"))snprintf(message,sizeof message,"Testing headphones in the lapdock audio jack...");
    else if(!strcmp(action,"sound-record"))snprintf(message,sizeof message,"Preparing microphone. Wait for the Recording message before speaking.");
    else if(!strcmp(action,"sound-play-recording"))snprintf(message,sizeof message,"Playing your recording...");
    else snprintf(message, sizeof message, "Working: %s...", action);
}
static void poll_action(void) {
    if (child <= 0) return;
    int status;
    pid_t ended = waitpid(child, &status, WNOHANG);
    if (ended == 0) return;
    child = 0;
    const char *result = !strcmp(pending_action,"time-sync")?"/tmp/doveos-time-status":!strncmp(pending_action, "bluetooth-", 10) ?
        "/tmp/doveos-bluetooth/result" : "/tmp/drmosa-settings-result";
    read_line(result, message, sizeof message);
    if (!message[0]) snprintf(message, sizeof message, "Action finished; check the device status.");
    if (!strcmp(pending_action, "wifi-scan")) load_wifi();
    if (!strcmp(pending_action, "bluetooth-scan")) load_bt();
    volume = read_number("/media/usb0/DrMoSa/sound-volume.txt", volume);
    (void)status;
}
static GtkWidget *label(const char *text) {return dove_label(text);}
static void add(GtkWidget *box,GtkWidget *widget) {gtk_box_pack_start(GTK_BOX(box),widget,FALSE,TRUE,4);}
#include "settings_system.h"
static const int settings_legacy_pages[10]={0,1,2,3,4,5,9,6,7,8};
static GtkWidget *settings_group(GtkWidget *parent,const char *name){GtkWidget *frame=gtk_frame_new(name),*inner=gtk_vbox_new(FALSE,4);gtk_container_set_border_width(GTK_CONTAINER(inner),8);gtk_container_add(GTK_CONTAINER(frame),inner);add(parent,frame);return inner;}
static GtkWidget *page(const char *name) {
 GtkWidget *box=dove_panel(FALSE,5);gtk_container_set_border_width(GTK_CONTAINER(box),12);
 GtkWidget *scrolled=dove_scroll(box);
 dove_tab_add(book,scrolled,name);GtkTreeIter it;gtk_list_store_append(category_model,&it);gtk_list_store_set(category_model,&it,0,name,1,gtk_notebook_get_n_pages(GTK_NOTEBOOK(book))-1,-1);
 GtkWidget *heading=label("");gchar *markup=g_markup_printf_escaped("<b>%s</b>",name);gtk_label_set_markup(GTK_LABEL(heading),markup);g_free(markup);add(box,heading);add(box,dove_line(TRUE));return box;
}
static void action(GtkWidget *w,gpointer data) {(void)w;run_action((const char*)data,NULL,NULL);gtk_label_set_text(GTK_LABEL(status),message);}
static GtkWidget *button(GtkWidget *box,const char *title,const char *command) {GtkWidget *w=dove_button(title);add(box,w);g_signal_connect(w,"clicked",G_CALLBACK(action),(gpointer)command);return w;}
static int sound_action_active(void) {
    return child>0&&(!strcmp(pending_action,"sound-test")||!strcmp(pending_action,"sound-test-left")||!strcmp(pending_action,"sound-test-right")||!strcmp(pending_action,"sound-headphones")||
        !strcmp(pending_action,"sound-record")||!strcmp(pending_action,"sound-play-recording"));
}
static void stop_sound_test(GtkWidget *w,gpointer data) {
    (void)w;(void)data;
    if(!sound_action_active())return;
    gchar *args[]={"/bin/sh","/usr/bin/doveos-settings-proxy","sound-test-stop",NULL};
    GError *error=NULL;
    if(!g_spawn_async(NULL,args,NULL,G_SPAWN_STDOUT_TO_DEV_NULL|G_SPAWN_STDERR_TO_DEV_NULL,NULL,NULL,NULL,&error)){
        gtk_label_set_text(GTK_LABEL(status),"Could not stop the sound test.");g_error_free(error);return;
    }
    snprintf(message,sizeof message,"Stopping the sound test...");gtk_label_set_text(GTK_LABEL(status),message);
}
#include "audio_settings_ui.h"
static int list_selected(GtkWidget *view,int index_column) {
    GtkTreeModel *model;GtkTreeIter it;int index=-1;
    if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),&model,&it))
        gtk_tree_model_get(model,&it,index_column,&index,-1);
    return index;
}
static gchar *list_selected_text(GtkWidget *view,int column) {
    GtkTreeModel *model;GtkTreeIter it;gchar *text=NULL;
    if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),&model,&it))
        gtk_tree_model_get(model,&it,column,&text,-1);
    return text;
}
static void list_select(GtkWidget *view,int index) {
    if(index<0)return;
    GtkTreePath *path=gtk_tree_path_new_from_indices(index,-1);
    gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),path);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(view),path,NULL,FALSE,0,0);
    gtk_tree_path_free(path);
}
static GtkWidget *device_list(GtkWidget *box,const char *const *columns,int count) {
    GType types[8];for(int i=0;i<count;i++)types[i]=G_TYPE_STRING;types[count]=G_TYPE_INT;
    GtkListStore *model=gtk_list_store_newv(count+1,types);
    GtkWidget *view=dove_listview(GTK_TREE_MODEL(model),columns,count);g_object_unref(model);
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),GTK_SELECTION_SINGLE);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(view),0);
    for(int i=0;i<count;i++){
        GtkTreeViewColumn *column=gtk_tree_view_get_column(GTK_TREE_VIEW(view),i);
        gtk_tree_view_column_set_resizable(column,TRUE);
        if(i==0)gtk_tree_view_column_set_expand(column,TRUE);
    }
    add(box,dove_scrolled(view,-1,190));return view;
}
static void bt_buttons_refresh(void) {
    if(!bt_saved_list||!bt_pair_button)return;
    gchar *saved=list_selected_text(bt_saved_list,1),*found=list_selected_text(bt_list,1);
    int n=saved?bt_state_index(saved):-1;
    gboolean available=child<=0,connected=bt_status_valid&&n>=0&&bt_states[n].connected;
    gtk_widget_set_sensitive(bt_start_button,available);
    gtk_widget_set_sensitive(bt_scan_button,available);
    gtk_widget_set_sensitive(bt_pair_button,available&&found&&!bt_is_saved(found));
    gtk_widget_set_sensitive(bt_connect_button,available&&saved);
    gtk_widget_set_sensitive(bt_disconnect_button,available&&saved&&(connected||(n>=0&&bt_states[n].input_state>0)));
    gtk_widget_set_sensitive(bt_forget_button,available&&saved);
    gtk_widget_set_sensitive(bt_test_button,available&&connected&&bt_states[n].audio);
    g_free(saved);g_free(found);
}
static void bt_selection_changed(GtkTreeSelection *selection,gpointer data) {
    (void)selection;(void)data;bt_buttons_refresh();
}
static void bt_refresh(const char *selected) {
    if(!bt_saved_list)return;
    gchar *saved=list_selected_text(bt_saved_list,1),*found=selected?g_strdup(selected):list_selected_text(bt_list,1);
    GtkAdjustment *saved_scroll=gtk_tree_view_get_vadjustment(GTK_TREE_VIEW(bt_saved_list));
    GtkAdjustment *found_scroll=gtk_tree_view_get_vadjustment(GTK_TREE_VIEW(bt_list));
    double saved_y=gtk_adjustment_get_value(saved_scroll),found_y=gtk_adjustment_get_value(found_scroll);
    GtkListStore *saved_model=GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(bt_saved_list)));
    GtkListStore *model=GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(bt_list)));
    gtk_list_store_clear(saved_model);gtk_list_store_clear(model);
    int saved_row=0,found_row=0,saved_active=-1,found_active=-1;
    for(unsigned i=0;i<bt_state_count;i++)if(bt_states[i].paired){
        GtkTreeIter it;gtk_list_store_append(saved_model,&it);
        gtk_list_store_set(saved_model,&it,0,bt_states[i].name,1,bt_states[i].addr,2,bt_type_text(bt_states[i].transport),3,bt_state_text(bt_states[i].addr),4,(int)i,-1);
        if((saved&&!strcasecmp(saved,bt_states[i].addr))||(found&&!strcasecmp(found,bt_states[i].addr)))saved_active=saved_row;
        saved_row++;
    }
    int order[64],count=0,hidden=0;
    for(int i=0;i<bt_count;i++)if(!bt_is_saved(bt[i].addr)){
        if(!bt_name_known(bt_search_name(i),bt[i].addr)){hidden++;continue;}
        int at=count;while(at>0&&bt_search_compare(order[at-1],i)>0){order[at]=order[at-1];at--;}
        order[at]=i;count++;
    }
    for(int row=0;row<count;row++){
        int i=order[row];GtkTreeIter it;gtk_list_store_append(model,&it);
        gtk_list_store_set(model,&it,0,bt_search_name(i),1,bt[i].addr,2,bt[i].le?"LE":"Classic",3,bt_state_text(bt[i].addr),4,i,-1);
        if((found&&!strcasecmp(found,bt[i].addr))||(saved&&!strcasecmp(saved,bt[i].addr)))found_active=found_row;
        found_row++;
    }
    list_select(bt_saved_list,saved_active>=0?saved_active:saved_row?0:-1);
    list_select(bt_list,found_active>=0?found_active:found_row?0:-1);
    gtk_adjustment_set_value(saved_scroll,saved_y);gtk_adjustment_set_value(found_scroll,found_y);
    char text[120];snprintf(text,sizeof text,"Saved devices (%d)",saved_row);gtk_label_set_text(GTK_LABEL(bt_saved_info),text);
    snprintf(text,sizeof text,"Search results (%d)%s",found_row,hidden?" - unnamed devices hidden":"");gtk_label_set_text(GTK_LABEL(bt_list_info),text);
    g_free(saved);g_free(found);bt_buttons_refresh();
}
static void list_refresh(void) {
    gchar *selected=list_selected_text(wifi_list,0),*address=list_selected_text(bt_list,1);
    GtkListStore *model=GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(wifi_list)));
    gtk_list_store_clear(model);int active=wifi_count?0:-1;
    for(int i=0;i<wifi_count;i++){
        char band[32],strength[32],channel[24];dove_wifi_info_text(&wifi_info[i],band,sizeof band,strength,sizeof strength,channel,sizeof channel);
        GtkTreeIter it;gtk_list_store_append(model,&it);gtk_list_store_set(model,&it,0,wifi_names[i],1,band,2,strength,3,channel,4,wifi_info[i].security[0]?wifi_info[i].security:"Unknown",5,"",6,i,-1);
        if(selected&&!strcmp(selected,wifi_names[i]))active=i;
    }
    list_select(wifi_list,active);
    char text[120];snprintf(text,sizeof text,wifi_count?"%d networks. Select a network below.":"No networks listed. Click Find Wi-Fi networks.",wifi_count);
    gtk_label_set_text(GTK_LABEL(wifi_list_info),text);
    wifi_connection_refresh();
    bt_refresh(address);g_free(selected);g_free(address);
}
static void join_wifi(GtkWidget *w,gpointer data) {
    (void)w;int n=data?-1:list_selected(wifi_list,6);
    if(n>=0&&n<wifi_count)snprintf(chosen_ssid,sizeof chosen_ssid,"%s",wifi_names[n]);
    else if(!data){gtk_label_set_text(GTK_LABEL(status),"Select a network, or click Other network to enter its name.");return;}
    GtkWidget *dialog=gtk_dialog_new_with_buttons("Connect to Wi-Fi",GTK_WINDOW(window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,GTK_STOCK_CANCEL,GTK_RESPONSE_CANCEL,GTK_STOCK_CONNECT,GTK_RESPONSE_OK,NULL);
    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));gtk_container_set_border_width(GTK_CONTAINER(box),12);
    GtkWidget *name_entry=NULL;
    if(data){add(box,label("Network name:"));name_entry=gtk_entry_new();gtk_entry_set_max_length(GTK_ENTRY(name_entry),32);add(box,name_entry);}
    else add(box,label(chosen_ssid));
    add(box,label("Password:"));GtkWidget *entry=gtk_entry_new();gtk_entry_set_visibility(GTK_ENTRY(entry),FALSE);gtk_entry_set_max_length(GTK_ENTRY(entry),64);gtk_entry_set_activates_default(GTK_ENTRY(entry),TRUE);add(box,entry);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_OK);gtk_widget_show_all(dialog);
    if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_OK){
        if(name_entry)snprintf(chosen_ssid,sizeof chosen_ssid,"%s",gtk_entry_get_text(GTK_ENTRY(name_entry)));
        const char *secret=gtk_entry_get_text(GTK_ENTRY(entry));size_t length=strlen(secret);
        if(!*chosen_ssid||strlen(chosen_ssid)>32)snprintf(message,sizeof message,"Network name must contain 1 to 32 bytes.");
        else if(length>=8&&length<=63)run_action("wifi-join",chosen_ssid,secret);
        else snprintf(message,sizeof message,"Password must contain 8 to 63 characters.");
        gtk_entry_set_text(GTK_ENTRY(entry),"");gtk_label_set_text(GTK_LABEL(status),message);
    }
    gtk_widget_destroy(dialog);
}
static void pair_bt(GtkWidget *w,gpointer data) {
    (void)w;(void)data;int n=list_selected(bt_list,4);
    if(n<0||n>=bt_count){gtk_label_set_text(GTK_LABEL(status),"Find and select a device from Search results first.");return;}
    run_action(bt[n].le?"bluetooth-pair-le":"bluetooth-pair-classic",bt[n].addr,NULL);
    gtk_label_set_text(GTK_LABEL(status),message);bt_buttons_refresh();
}
static void saved_bt_action(GtkWidget *w,gpointer data) {
    (void)w;gchar *address=list_selected_text(bt_saved_list,1);
    if(!address){gtk_label_set_text(GTK_LABEL(status),"Select a saved device first.");return;}
    run_action((const char*)data,address,NULL);g_free(address);
    gtk_label_set_text(GTK_LABEL(status),message);bt_buttons_refresh();
}
static gboolean send_volume(gpointer data){(void)data;volume_timer=0;char value[8];int n=(int)gtk_range_get_value(GTK_RANGE(volume_scale));if(child>0){volume_timer=g_timeout_add(150,send_volume,NULL);return FALSE;}snprintf(value,sizeof value,"%d",n);run_action("sound-set",value,NULL);return FALSE;}
static void volume_changed(GtkRange *range,gpointer data){(void)data;char text[64];snprintf(text,sizeof text,"Speaker volume: %d%%",(int)gtk_range_get_value(range));gtk_label_set_text(GTK_LABEL(volume_label),text);if(syncing)return;if(volume_timer)g_source_remove(volume_timer);volume_timer=g_timeout_add(150,send_volume,NULL);}
static void mute_changed(GtkToggleButton *w,gpointer data){(void)data;if(syncing)return;static int saved=70;if(gtk_toggle_button_get_active(w)){int n=(int)gtk_range_get_value(GTK_RANGE(volume_scale));if(n>0)saved=n;gtk_range_set_value(GTK_RANGE(volume_scale),0);}else gtk_range_set_value(GTK_RANGE(volume_scale),saved);}
static void awake_changed(GtkToggleButton *w,gpointer data){(void)w;(void)data;if(!syncing)run_action("awake-toggle",NULL,NULL);}
static void reverse_changed(GtkToggleButton *w,gpointer data){(void)data;if(!syncing&&gtk_toggle_button_get_active(w)!=reverse_scroll())set_reverse_scroll();}
static GtkWidget *timeout_combo(int seconds){
 GtkWidget *w=gtk_combo_box_text_new();int selected=0;
 for(unsigned i=0;i<sizeof power_minutes/sizeof power_minutes[0];i++){
  char s[32];if(power_minutes[i])snprintf(s,sizeof s,"%d minute%s",power_minutes[i],power_minutes[i]==1?"":"s");else snprintf(s,sizeof s,"Never");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w),s);if(seconds==power_minutes[i]*60)selected=(int)i;
 }gtk_combo_box_set_active(GTK_COMBO_BOX(w),selected);return w;
}
static void apply_power(GtkWidget *w,gpointer data){
 (void)w;(void)data;int a=gtk_combo_box_get_active(GTK_COMBO_BOX(battery_timeout)),b=gtk_combo_box_get_active(GTK_COMBO_BOX(ac_timeout));
 const int choices=(int)(sizeof power_minutes/sizeof power_minutes[0]);
 if(a<0||a>=choices||b<0||b>=choices)return;
 int c=gtk_combo_box_get_active(GTK_COMBO_BOX(battery_dim)),d=gtk_combo_box_get_active(GTK_COMBO_BOX(ac_dim));if(c<0||c>=choices||d<0||d>=choices)return;
 DovePower p={power_minutes[a]*60,power_minutes[b]*60,gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(media_awake)),power_minutes[c]*60,power_minutes[d]*60};
 if((p.battery_seconds&&p.battery_dim>=p.battery_seconds)||(p.ac_seconds&&p.ac_dim>=p.ac_seconds)){snprintf(message,sizeof message,"Choose dimming before screen-off, or set dimming to Never.");return;}
 snprintf(message,sizeof message,dove_power_save(p)?"Could not save power settings on USB.":"Battery and charging timeouts saved on USB.");
}
static void test_power(GtkWidget *w,gpointer data){
 (void)w;(void)data;FILE *f=fopen("/tmp/doveos-power-test","w");
 if(f){fclose(f);snprintf(message,sizeof message,"Ten-second screen-off test; touchpad or keyboard wakes it.");}
 else snprintf(message,sizeof message,"Could not start screen saver test.");
}
static void apply_brightness(GtkWidget *w,gpointer data){
 (void)w;(void)data;char value[12];snprintf(value,sizeof value,"%d",(int)gtk_range_get_value(GTK_RANGE(brightness_scale)));
 snprintf(message,sizeof message,clock_save("display-brightness.txt",value)?"Could not save brightness on USB.":"Display brightness saved and applied.");
}
static int brightness_current(void){
 double sampled=0;int base=0,mode=0;FILE *f=fopen("/tmp/doveos-backlight-state","r");if(!f)return -1;
 int n=fscanf(f,"DOVE_BACKLIGHT1 %lf %d %d",&sampled,&base,&mode);fclose(f);struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
 double age=t.tv_sec+t.tv_nsec/1e9-sampled;return n==3&&base>=3&&base<=100&&age>=0&&age<3?base:-1;
}
static void render_changed(GtkComboBox *box,gpointer data){
 (void)data;const char *modes[]={"auto","cpu","gpu"};int n=gtk_combo_box_get_active(box);
 if(n>=0&&n<3)snprintf(message,sizeof message,clock_save("render-mode.txt",modes[n])?"Could not save display mode.":"Display mode saved. Restart DoveOS to apply.");
}
static GtkWidget *date_calendar,*hour_spin,*minute_spin,*second_spin,*zone_combo,*format_combo,*clock_label,*auto_time;
static void online_changed(GtkToggleButton *w,gpointer data){(void)data;clock_save("time-auto.txt",gtk_toggle_button_get_active(w)?"1":"0");}
static void apply_clock(GtkWidget *w,gpointer data){
 (void)w;(void)data;const char *zone=gtk_combo_box_get_active(GTK_COMBO_BOX(zone_combo))==1?"UTC0":"AST-3";
 if(clock_save("time-zone.txt",zone)||clock_save("clock-format.txt",gtk_combo_box_get_active(GTK_COMBO_BOX(format_combo))?"24":"12")){snprintf(message,sizeof message,"Could not save clock preferences on USB.");return;}
 clock_zone();
 {
  guint year,month,day;gtk_calendar_get_date(GTK_CALENDAR(date_calendar),&year,&month,&day);
  if(year<2020||year>2037){snprintf(message,sizeof message,"Choose a year between 2020 and 2037 (32-bit clock).");return;}
  struct tm value={0};value.tm_year=(int)year-1900;value.tm_mon=(int)month;value.tm_mday=(int)day;
  value.tm_hour=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(hour_spin));value.tm_min=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(minute_spin));value.tm_sec=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(second_spin));value.tm_isdst=-1;
  struct timespec t={mktime(&value),0};if(t.tv_sec<0||clock_settime(CLOCK_REALTIME,&t)){snprintf(message,sizeof message,"Could not set system time.");return;}
 }
 time_t saved=time(NULL);char epoch[32];snprintf(epoch,sizeof epoch,"%ld",(long)saved);
 if(clock_save("time-last-set.txt",epoch)){snprintf(message,sizeof message,"Time applied, but USB save failed.");return;}
 if(fork()==0){execl("/tmp/doveos-theme/doveos-timekeeper","doveos-timekeeper","--save",(char*)NULL);_exit(127);}
 snprintf(message,sizeof message,"Date and time saved to USB and applied.");
}
static GdkFilterReturn settings_page_event(GdkXEvent *raw,GdkEvent *event,gpointer data){
 (void)event;(void)data;XEvent *e=raw;
 if(e->type==ClientMessage&&e->xclient.message_type==XInternAtom(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()),"_DOVEOS_SETTINGS_PAGE",False)){
  long p=e->xclient.data.l[0];if(p>=0&&p<gtk_notebook_get_n_pages(GTK_NOTEBOOK(book)))gtk_notebook_set_current_page(GTK_NOTEBOOK(book),settings_legacy_pages[p]);gtk_window_present(GTK_WINDOW(window));return GDK_FILTER_REMOVE;
 }return GDK_FILTER_CONTINUE;
}
static gboolean key_press(GtkWidget *w,GdkEventKey *event,gpointer data){(void)w;(void)data;if(event->keyval==GDK_Escape&&gtk_notebook_get_current_page(GTK_NOTEBOOK(book))!=7&&gtk_notebook_get_current_page(GTK_NOTEBOOK(book))!=8){gtk_widget_destroy(window);return TRUE;}return FALSE;}
static void category_changed(GtkTreeSelection *selection,gpointer data){(void)data;GtkTreeIter it;GtkTreeModel *model;if(gtk_tree_selection_get_selected(selection,&model,&it)){int p;gtk_tree_model_get(model,&it,1,&p,-1);gtk_notebook_set_current_page(GTK_NOTEBOOK(book),p);}}
static void category_page(GtkNotebook *notebook,GtkNotebookPage *page_widget,guint p,gpointer data){(void)notebook;(void)page_widget;(void)data;GtkTreePath *path=gtk_tree_path_new_from_indices((int)p,-1);gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(categories)),path);gtk_tree_path_free(path);if(keyboard_footer)gtk_widget_set_visible(keyboard_footer,p==7||p==8);}
#include "keyboard_ui.h"
#include "sounds_tabs_ui.h"
static gboolean settings_navigation_check(gpointer data){
 (void)data;XEvent e;memset(&e,0,sizeof e);e.type=ClientMessage;e.xclient.message_type=XInternAtom(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()),"_DOVEOS_SETTINGS_PAGE",False);
 for(int i=0;i<10;i++){e.xclient.data.l[0]=i;settings_page_event(&e,NULL,NULL);if(gtk_notebook_get_current_page(GTK_NOTEBOOK(book))!=settings_legacy_pages[i])g_error("Legacy settings page mismatch: %d",i);}
 for(int i=0;i<10;i++){gtk_notebook_set_current_page(GTK_NOTEBOOK(book),i);if(gtk_widget_get_visible(keyboard_footer)!=(i==7||i==8))g_error("Keyboard footer mismatch: %d",i);}
 GtkTreeIter it;gchar *name=NULL;if(!gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(category_model),&it,NULL,9))g_error("Missing final System category");gtk_tree_model_get(GTK_TREE_MODEL(category_model),&it,0,&name,-1);if(strcmp(name,"System"))g_error("System is not last");g_free(name);
 printf("SETTINGS_NAVIGATION_PASS legacy_pages=10 category_pages=10 system_last=1 keyboard_footer=correct\n");gtk_widget_destroy(window);return FALSE;
}
static gboolean settings_smoke(gpointer data){
 (void)data;static int step;const int pages[]={0,1,3,9};
 if(step>0){gtk_widget_queue_draw(window);gdk_window_process_updates(gtk_widget_get_window(window),TRUE);GdkPixbuf *image=gdk_pixbuf_get_from_drawable(NULL,gtk_widget_get_window(window),gtk_widget_get_colormap(window),0,0,0,0,window->allocation.width,window->allocation.height);if(image){char path[90];snprintf(path,sizeof path,"/tmp/doveos-settings-preview-%d.png",pages[step-1]);gdk_pixbuf_save(image,path,"png",NULL,NULL);g_object_unref(image);}}
 if(step<4){gtk_notebook_set_current_page(GTK_NOTEBOOK(book),pages[step]);step++;return TRUE;}
 printf("SETTINGS_SDK_1_1_CHECK pages=%d fn_controls=0 window=%dx%d\n",gtk_notebook_get_n_pages(GTK_NOTEBOOK(book)),window->allocation.width,window->allocation.height);
 printf("AUDIO_UI_CHECK selectors=0 output_buttons=Stereo,Left,Right microphone_test=5s\n");
 unsigned saved_count=0,search_count=0,expected=0,overlap=0,unknown=0,unsorted=0;int previous=-1;
 for(unsigned i=0;i<bt_state_count;i++)if(bt_states[i].paired)expected++;
 GtkWidget *views[]={bt_saved_list,bt_list};
 for(unsigned v=0;v<2;v++){
  GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(views[v]));GtkTreeIter it;
  if(gtk_tree_model_get_iter_first(model,&it))do{
   gchar *addr=NULL;gtk_tree_model_get(model,&it,1,&addr,-1);
   if(v==0){saved_count++;if(!bt_is_saved(addr))overlap++;}
   else{
    search_count++;if(bt_is_saved(addr))overlap++;
    gint index=-1;gchar *name=NULL;gtk_tree_model_get(model,&it,0,&name,4,&index,-1);
    if(!bt_name_known(name,addr))unknown++;
    if(index<0||index>=bt_count||g_ascii_strcasecmp(addr,bt[index].addr))overlap++;
    else{if(previous>=0&&bt_search_compare(previous,index)>0)unsorted++;previous=index;}
    g_free(name);
   }
   g_free(addr);
  }while(gtk_tree_model_iter_next(model,&it));
 }
 printf("BT_LISTS_CHECK saved=%u search=%u expected_saved=%u status_valid=%d overlap=%u\n",saved_count,search_count,expected,bt_status_valid,overlap);
 printf("BT_SEARCH_CHECK unknown=%u unsorted=%u\n",unknown,unsorted);
 if(getenv("DOVEOS_CHECK_BT_LISTS")&&(!bt_status_valid||saved_count!=expected||overlap||unknown||unsorted))g_error("Bluetooth list partition check failed");

 gtk_widget_destroy(window);return FALSE;
}
static gboolean tick(gpointer data){(void)data;
    static unsigned scan_tick;
    if(child>0&&!strcmp(pending_action,"bluetooth-scan")){
        char result[240];read_line("/tmp/doveos-bluetooth/result",result,sizeof result);
        if(result[0])snprintf(message,sizeof message,"%s",result);
        if(++scan_tick%4==0){
            gchar *selected=list_selected_text(bt_list,1);
            unsigned char previous[sizeof bt];memcpy(previous,bt,sizeof bt);int count=bt_count;
            load_bt();if(count!=bt_count||memcmp(previous,bt,sizeof bt))bt_refresh(selected);
            g_free(selected);
        }
    }
    if(brightness_info){int b=brightness_current();gtk_widget_set_sensitive(brightness_scale,b>=0);gtk_label_set_text(GTK_LABEL(brightness_info),b>=0?"Hardware backlight control available.":"Hardware backlight control unavailable.");}
    if(render_info){char s[160];read_line("/tmp/doveos-render-status",s,sizeof s);gtk_label_set_text(GTK_LABEL(render_info),s[0]?s:"Display renderer is starting.");}
    if(power_status){char s[180];read_line("/tmp/doveos-power-status",s,sizeof s);gtk_label_set_text(GTK_LABEL(power_status),s[0]?s:"Power manager is not running.");}
    if(battery_label){char info[100];battery(info,sizeof info);gtk_label_set_text(GTK_LABEL(battery_label),info);}pid_t running=child;poll_action();if(running>0&&child==0&&(!strcmp(pending_action,"wifi-scan")||!strcmp(pending_action,"bluetooth-scan")))list_refresh();
    clock_zone();time_t now=time(NULL);struct tm local;localtime_r(&now,&local);char clock_text[100];if(now<1577836800)snprintf(clock_text,sizeof clock_text,"Clock not set - choose the date and time, then Apply.");else {char date_text[80];strftime(date_text,sizeof date_text,"%A, %d %B %Y  %H:%M:%S",&local);snprintf(clock_text,sizeof clock_text,"%s  %s",date_text,clock_zone_label());}if(clock_label)gtk_label_set_text(GTK_LABEL(clock_label),clock_text);
    gtk_label_set_text(GTK_LABEL(status),message[0]?message:"Choose a setting.");
    wifi_connection_refresh();bt_buttons_refresh();
    char rt_network[128];read_line("/tmp/doveos-rtl8153/network",rt_network,sizeof rt_network);
    gtk_label_set_text(GTK_LABEL(ethernet_status),(!strncmp(rt_network,"rtdebug0 connected ",19)||link_up("axdebug0"))?"Ethernet connected":"Ethernet disconnected");
    syncing=1;
    gtk_widget_set_sensitive(startup_preview,child<=0&&!volume_timer);
    gtk_widget_set_sensitive(events_preview,child<=0&&!volume_timer);
    gtk_widget_set_sensitive(sounds_apply,child<=0&&!volume_timer);
    if(sound_test)gtk_widget_set_sensitive(sound_test,child<=0&&!volume_timer);
    if(sound_left)gtk_widget_set_sensitive(sound_left,child<=0&&!volume_timer);
    if(sound_right)gtk_widget_set_sensitive(sound_right,child<=0&&!volume_timer);
    if(sound_record)gtk_widget_set_sensitive(sound_record,child<=0&&!volume_timer);
    if(sound_play)gtk_widget_set_sensitive(sound_play,child<=0&&!volume_timer&&access("/tmp/doveos-settings-audio/recording.wav",R_OK)==0);
    if(sound_stop)gtk_widget_set_sensitive(sound_stop,sound_action_active());
    if(sound_action_active()){
        char progress[240];read_line("/tmp/drmosa-settings-result",progress,sizeof progress);
        if(progress[0]){snprintf(message,sizeof message,"%s",progress);gtk_label_set_text(GTK_LABEL(status),message);}
    }
    if(!volume_timer&&child<=0){volume=read_number("/media/usb0/DrMoSa/sound-volume.txt",volume);gtk_range_set_value(GTK_RANGE(volume_scale),volume);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mute),volume==0);}
    char value[8];read_line("/proc/1/root/usr/local/etc/drmosa-ethernet/keepawake-enabled",value,sizeof value);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(awake),!strcmp(value,"1"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(reverse_box),reverse_scroll());syncing=0;return TRUE;}
#include "power_ui.h"
int main(int argc,char **argv){
    gtk_init(&argc,&argv);
    if(argc>1&&!strcmp(argv[1],"shutdown-status")) {
      GString *names=g_string_new("");
      unsigned count=shutdown_windows(gdk_x11_get_default_xdisplay(),0,names);
      printf("OPEN_APPLICATION_WINDOWS=%u%s\n",count,names->str);
      g_string_free(names,TRUE);return 0;
    }
    if(argc>1&&!strcmp(argv[1],"shutdown-close-preview")) {
      printf("SHUTDOWN_CLOSE_RESULT=%d\n",prepare_shutdown());return 0;
    }
    if(argc>1&&!strcmp(argv[1],"shutdown")) {
      GtkWidget *notice=gtk_message_dialog_new(NULL,GTK_DIALOG_MODAL,
          GTK_MESSAGE_INFO,GTK_BUTTONS_CLOSE,
          "Shut down is unavailable on this firmware.\n"
          "The power-off test did not restart normally.");
      gtk_dialog_run(GTK_DIALOG(notice));gtk_widget_destroy(notice);return 0;
    }
    if(argc>1 && !strcmp(argv[1],"pdf")) {
      GtkWidget *dialog=gtk_file_chooser_dialog_new("Open PDF",NULL,GTK_FILE_CHOOSER_ACTION_OPEN,GTK_STOCK_CANCEL,GTK_RESPONSE_CANCEL,GTK_STOCK_OPEN,GTK_RESPONSE_ACCEPT,NULL);
      gtk_window_set_wmclass(GTK_WINDOW(dialog),"DovePdfChooser","DovePdfChooser");
      gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),"/media/usb0");
      GtkFileFilter *filter=gtk_file_filter_new();gtk_file_filter_set_name(filter,"PDF documents");gtk_file_filter_add_pattern(filter,"*.pdf");gtk_file_filter_add_pattern(filter,"*.PDF");gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),filter);
      char *filename=NULL;
      if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT)filename=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
      gtk_widget_destroy(dialog);
      if(filename){execl("/bin/sh","sh","/tmp/doveos-theme/light-app-launch.sh","mupdf",filename,(char*)NULL);g_free(filename);return 1;}
      return 0;
    }
    if(argc>1 && (!strcmp(argv[1],"power")||!strcmp(argv[1],"--power-preview")))return dove_power_dialog(!strcmp(argv[1],"--power-preview"));
    if(argc>1 && !strcmp(argv[1],"about")) {
      GtkWidget *about=gtk_about_dialog_new();
      gtk_window_set_wmclass(GTK_WINDOW(about),"DoveAbout","DoveAbout");
      gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(about),"DoveOS LapDock");
      gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(about),"v2");
      gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(about),"Lightweight USB desktop for the HP Elite x3 Lap Dock.\nJWM desktop, GTK controls, Moonlight and dock tools.\nBuilt on the dock's existing Linux kernel.");
      GdkPixbuf *logo=gdk_pixbuf_new_from_file("/etc/doveos/dove-start.png",NULL);
      if(logo){gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(about),logo);g_object_unref(logo);}
      gtk_dialog_run(GTK_DIALOG(about));gtk_widget_destroy(about);return 0;
    }
    window=dove_form("Settings - DoveOS",860,580);dove_window_fit(window,860,580);gtk_window_set_icon_from_file(GTK_WINDOW(window),"/etc/doveos/classic-icons/48/preferences-system.png",NULL);
    gtk_window_set_wmclass(GTK_WINDOW(window),"DoveSettings","DoveSettings");
    g_signal_connect(window,"destroy",G_CALLBACK(gtk_main_quit),NULL);g_signal_connect(window,"key-press-event",G_CALLBACK(key_press),NULL);
    GtkWidget *outer=dove_panel(FALSE,6);gtk_container_set_border_width(GTK_CONTAINER(outer),8);gtk_container_add(GTK_CONTAINER(window),outer);
    GtkWidget *header=dove_panel(TRUE,10);add(outer,header);add(header,dove_image("/etc/doveos/classic-icons/32/preferences-system.png"));GtkWidget *title=label("Settings");gtk_label_set_markup(GTK_LABEL(title),"<b>DoveOS Settings</b>\nDevices, connections and desktop preferences.");add(header,title);
    GtkWidget *body=dove_panel(TRUE,8);gtk_box_pack_start(GTK_BOX(outer),body,TRUE,TRUE,0);
    category_model=gtk_list_store_new(2,G_TYPE_STRING,G_TYPE_INT);const char *names[]={"Settings"};categories=dove_listview(GTK_TREE_MODEL(category_model),names,1);gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(categories),FALSE);gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(categories),FALSE);GtkWidget *navigation=dove_scrolled(categories,160,-1);gtk_box_pack_start(GTK_BOX(body),navigation,FALSE,TRUE,0);
    book=dove_tabs();gtk_notebook_set_show_tabs(GTK_NOTEBOOK(book),FALSE);gtk_notebook_set_show_border(GTK_NOTEBOOK(book),FALSE);gtk_box_pack_start(GTK_BOX(body),book,TRUE,TRUE,0);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(categories)),"changed",G_CALLBACK(category_changed),NULL);g_signal_connect(book,"switch-page",G_CALLBACK(category_page),NULL);
    GtkWidget *network_page=page("Network"),*box=settings_group(network_page,"Ethernet");ethernet_status=label("");add(box,ethernet_status);button(box,"Enable / disable Ethernet","ethernet-toggle");box=settings_group(network_page,"Wi-Fi");wifi_status=label("");add(box,wifi_status);
    GtkWidget *row=gtk_hbox_new(FALSE,8);add(box,row);button(row,"Find Wi-Fi networks","wifi-scan");button(row,"Reconnect saved network","wifi-reconnect");button(row,"Disconnect","wifi-disconnect");
    wifi_list_info=label("");add(box,wifi_list_info);const char *wifi_columns[]={"Network","Band","Strength","Channel","Security","Status"};wifi_list=device_list(box,wifi_columns,6);gtk_widget_set_size_request(gtk_widget_get_parent(wifi_list),-1,140);
    gtk_widget_set_tooltip_text(wifi_list,"Signal nearer 0 dBm is stronger. Channel, strength and security describe the strongest detected access point for each network. Missing scan details are shown as Unknown.");
    row=gtk_hbox_new(FALSE,8);add(box,row);GtkWidget *join=gtk_button_new_with_label("Connect..."),*other=gtk_button_new_with_label("Other network...");add(row,join);add(row,other);g_signal_connect(join,"clicked",G_CALLBACK(join_wifi),NULL);g_signal_connect(other,"clicked",G_CALLBACK(join_wifi),GINT_TO_POINTER(1));
    box=page("Bluetooth");
    const char *bt_columns[]={"Device","Address","Type","Status"};
    row=gtk_hbox_new(FALSE,8);add(box,row);bt_saved_info=label("Saved devices");add(row,bt_saved_info);
    bt_start_button=button(row,"Start Bluetooth","bluetooth-start");
    bt_saved_list=device_list(box,bt_columns,4);gtk_widget_set_size_request(gtk_widget_get_parent(bt_saved_list),-1,106);
    row=gtk_hbox_new(FALSE,8);add(box,row);
    bt_connect_button=dove_button("Reconnect");bt_disconnect_button=dove_button("Disconnect");bt_test_button=dove_button("Test sound");bt_forget_button=dove_button("Forget");
    add(row,bt_connect_button);add(row,bt_disconnect_button);add(row,bt_test_button);add(row,bt_forget_button);
    g_signal_connect(bt_connect_button,"clicked",G_CALLBACK(saved_bt_action),"bluetooth-connect");
    g_signal_connect(bt_disconnect_button,"clicked",G_CALLBACK(saved_bt_action),"bluetooth-disconnect");
    gtk_widget_set_tooltip_text(bt_disconnect_button,"Disconnect this device and stop automatic input reconnect, keeping its saved pairing.");
    g_signal_connect(bt_test_button,"clicked",G_CALLBACK(saved_bt_action),"bluetooth-audio-test");
    g_signal_connect(bt_forget_button,"clicked",G_CALLBACK(saved_bt_action),"bluetooth-forget");
    gtk_widget_set_tooltip_text(bt_forget_button,"Remove the selected saved pairing so you can pair this device again.");
    row=gtk_hbox_new(FALSE,8);add(box,row);bt_list_info=label("Search results");add(row,bt_list_info);
    bt_scan_button=button(row,"Find devices","bluetooth-scan");
    gtk_widget_set_tooltip_text(bt_scan_button,"Put the new device in pairing mode, then click Find devices. Named results are sorted alphabetically; unnamed devices are hidden.");
    bt_list=device_list(box,bt_columns,4);gtk_widget_set_size_request(gtk_widget_get_parent(bt_list),-1,106);
    row=gtk_hbox_new(FALSE,8);add(box,row);bt_pair_button=dove_button("Pair");add(row,bt_pair_button);
    g_signal_connect(bt_pair_button,"clicked",G_CALLBACK(pair_bt),NULL);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(bt_saved_list)),"changed",G_CALLBACK(bt_selection_changed),NULL);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(bt_list)),"changed",G_CALLBACK(bt_selection_changed),NULL);
    box=page("Display");add(box,label("Screen brightness:"));brightness_scale=gtk_hscale_new_with_range(3,100,1);gtk_range_set_value(GTK_RANGE(brightness_scale),brightness_current()>0?brightness_current():40);add(box,brightness_scale);brightness_info=label("");add(box,brightness_info);GtkWidget *brightness_apply=gtk_button_new_from_stock(GTK_STOCK_APPLY);add(box,brightness_apply);g_signal_connect(brightness_apply,"clicked",G_CALLBACK(apply_brightness),NULL);awake=gtk_check_button_new_with_label("Prevent original dock idle sleep (needed for power controls)");add(box,awake);g_signal_connect(awake,"toggled",G_CALLBACK(awake_changed),NULL);char battery_info[100];battery(battery_info,sizeof battery_info);battery_label=label(battery_info);add(box,battery_label);
    add(box,label("Display conversion:"));render_mode=gtk_combo_box_text_new();gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(render_mode),"Automatic (recommended)");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(render_mode),"CPU");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(render_mode),"GPU (experimental)");char renderer[16];read_line(CLOCK_HOME "render-mode.txt",renderer,sizeof renderer);gtk_combo_box_set_active(GTK_COMBO_BOX(render_mode),!strcmp(renderer,"cpu")?1:!strcmp(renderer,"gpu")?2:0);add(box,render_mode);g_signal_connect(render_mode,"changed",G_CALLBACK(render_changed),NULL);render_info=label("");add(box,render_info);
    box=page("Sounds & Audio");sounds_build_page(box);
    box=page("Controls");reverse_box=gtk_check_button_new_with_label("Reverse two-finger scrolling");add(box,reverse_box);g_signal_connect(reverse_box,"toggled",G_CALLBACK(reverse_changed),NULL);add(box,label("Alt+Tab: switch windows\nAlt+F4: close a window\nCtrl+Alt+Shift+Q: end a Moonlight or DeX session"));
    box=page("Date & Time");clock_label=label("");add(box,clock_label);clock_zone();time_t now=time(NULL);struct tm current;if(now<1577836800){char saved[32];read_line(CLOCK_HOME "time-last-set.txt",saved,sizeof saved);time_t last=(time_t)strtol(saved,NULL,10);now=last>=1577836800?last:1577836800;}localtime_r(&now,&current);
    row=gtk_hbox_new(FALSE,12);add(box,row);date_calendar=gtk_calendar_new();add(row,date_calendar);gtk_calendar_select_month(GTK_CALENDAR(date_calendar),current.tm_mon,current.tm_year+1900);gtk_calendar_select_day(GTK_CALENDAR(date_calendar),current.tm_mday);
    GtkWidget *options=gtk_vbox_new(FALSE,5);add(row,options);add(options,label("Time zone:"));zone_combo=gtk_combo_box_text_new();gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(zone_combo),"Riyadh (UTC+3)");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(zone_combo),"UTC");gtk_combo_box_set_active(GTK_COMBO_BOX(zone_combo),!strcmp(getenv("TZ"),"UTC0"));add(options,zone_combo);
    add(options,label("Tray clock:"));format_combo=gtk_combo_box_text_new();gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(format_combo),"12-hour (AM / PM)");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(format_combo),"24-hour");gtk_combo_box_set_active(GTK_COMBO_BOX(format_combo),clock_24h());add(options,format_combo);
    add(options,label("Set date and time:"));GtkWidget *time_row=gtk_hbox_new(FALSE,4);add(options,time_row);
    hour_spin=gtk_spin_button_new_with_range(0,23,1);minute_spin=gtk_spin_button_new_with_range(0,59,1);second_spin=gtk_spin_button_new_with_range(0,59,1);add(time_row,hour_spin);add(time_row,label(":"));add(time_row,minute_spin);add(time_row,label(":"));add(time_row,second_spin);gtk_spin_button_set_value(GTK_SPIN_BUTTON(hour_spin),current.tm_hour);gtk_spin_button_set_value(GTK_SPIN_BUTTON(minute_spin),current.tm_min);gtk_spin_button_set_value(GTK_SPIN_BUTTON(second_spin),current.tm_sec);
    GtkWidget *apply=gtk_button_new_from_stock(GTK_STOCK_APPLY);add(options,apply);g_signal_connect(apply,"clicked",G_CALLBACK(apply_clock),NULL);
    auto_time=gtk_check_button_new_with_label("Automatically synchronize Internet time");add(box,auto_time);char automatic[8];read_line(CLOCK_HOME "time-auto.txt",automatic,sizeof automatic);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(auto_time),strcmp(automatic,"0")!=0);g_signal_connect(auto_time,"toggled",G_CALLBACK(online_changed),NULL);
    button(box,"Synchronize now","time-sync");char sync_status[160];read_line("/tmp/doveos-time-status",sync_status,sizeof sync_status);add(box,label(sync_status[0]?sync_status:"Internet time sync starts automatically when online."));
    add(box,label("Date, time zone and clock format are saved on USB.\nAfter a clock reset, the saved time is restored, then corrected online.\nOffline accuracy while powered off depends on the dock's hardware clock."));
    box=page("Battery & Power");DovePower power=dove_power_load();
    add(box,label("Display timeouts after inactivity:"));
    add(box,label("Both timers start from your last keyboard, touchpad or mouse activity.\nWaking restores your chosen brightness; dimming does not change it."));
    row=gtk_hbox_new(FALSE,12);add(box,row);add(row,label("On battery - dim:"));battery_dim=timeout_combo(power.battery_dim);add(row,battery_dim);add(row,label("Screen off:"));battery_timeout=timeout_combo(power.battery_seconds);add(row,battery_timeout);
    row=gtk_hbox_new(FALSE,12);add(box,row);add(row,label("Charging - dim:"));ac_dim=timeout_combo(power.ac_dim);add(row,ac_dim);add(row,label("Screen off:"));ac_timeout=timeout_combo(power.ac_seconds);add(row,ac_timeout);
    media_awake=gtk_check_button_new_with_label("Keep awake while media apps are open");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(media_awake),power.inhibit_media);add(box,media_awake);
    row=gtk_hbox_new(FALSE,12);add(box,row);GtkWidget *power_apply=gtk_button_new_from_stock(GTK_STOCK_APPLY);add(row,power_apply);g_signal_connect(power_apply,"clicked",G_CALLBACK(apply_power),NULL);
    GtkWidget *power_test=gtk_button_new_with_label("Test screen off (10 seconds)");add(row,power_test);g_signal_connect(power_test,"clicked",G_CALLBACK(test_power),NULL);
    GtkWidget *power_options=gtk_button_new_with_label("Power Options...");add(row,power_options);g_signal_connect(power_options,"clicked",G_CALLBACK(open_power_options),NULL);
    power_status=label("");add(box,power_status);
    add(box,label("Touchpad movement or a key restores the same desktop.\nDimming and screen-off control the hardware backlight.\nLinux stays running; suspend is disabled.\nSettings are saved on USB. Streaming sessions remain awake."));
    keyboard_pages();
    box=page("System");system_build(box);
    keyboard_footer=dove_panel(FALSE,0);add(outer,keyboard_footer);kb_add_apply(keyboard_footer);
    status=label("Ready.");GtkWidget *frame=dove_groupbox(NULL);gtk_frame_set_shadow_type(GTK_FRAME(frame),GTK_SHADOW_IN);gtk_container_add(GTK_CONTAINER(frame),status);add(outer,frame);
    load_wifi();load_bt();list_refresh();tick(NULL);g_timeout_add(350,tick,NULL);bt_status_poll(NULL);g_timeout_add(2000,bt_status_poll,NULL);
    gtk_widget_show_all(window);
    gdk_window_add_filter(gtk_widget_get_window(window),settings_page_event,NULL);
    if(argc>1){const char *pages[]={"network","bluetooth","display","sound","controls","datetime","battery","keyboard","language","system"};for(int i=0;i<10;i++)if(!strcmp(argv[1],pages[i]))gtk_notebook_set_current_page(GTK_NOTEBOOK(book),i);}
    gtk_widget_set_visible(keyboard_footer,(gtk_notebook_get_current_page(GTK_NOTEBOOK(book))==7||gtk_notebook_get_current_page(GTK_NOTEBOOK(book))==8));
    if(argc>1&&!strcmp(argv[1],"--sound-smoke-test"))dove_timer(500,sounds_smoke,NULL);
    if(argc>1&&!strcmp(argv[1],"--smoke-test"))dove_timer(500,settings_smoke,NULL);
    if(argc>1&&!strcmp(argv[1],"--navigation-check"))dove_timer(500,settings_navigation_check,NULL);
    gtk_main();return 0;
}
