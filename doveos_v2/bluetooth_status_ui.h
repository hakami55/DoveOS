/* Read-only asynchronous BlueZ status. No radio scan or connection requests. */
#include <gio/gio.h>
static GDBusConnection *bt_bus;
static gboolean bt_status_busy,bt_status_valid,bt_status_attempted;
static struct BtState {char addr[18],name[90];gboolean paired,connected,le,audio;unsigned transport,input_state;} bt_states[128];
static unsigned bt_state_count;
static void bt_refresh(const char *selected);
static void bt_buttons_refresh(void);
static int bt_state_index(const char *addr) {
    for(unsigned i=0;i<bt_state_count;i++)if(!g_ascii_strcasecmp(addr,bt_states[i].addr))return (int)i;
    return -1;
}
static gboolean bt_is_saved(const char *addr) {
    int i=bt_state_index(addr);return i>=0&&bt_states[i].paired;
}
static unsigned bt_input_state(const char *address) {
    const char *kinds[]={"keyboard","mouse"};
    for(unsigned i=0;i<G_N_ELEMENTS(kinds);i++){
        char path[180],selected[32]={0},state[100]={0};
        snprintf(path,sizeof path,"/proc/1/root/tmp/doveos-bluetooth/input-services/%s/desired",kinds[i]);
        FILE *file=fopen(path,"r");if(!file)continue;
        if(fgets(selected,sizeof selected,file))selected[strcspn(selected,"\r\n")]=0;
        fclose(file);if(g_ascii_strcasecmp(selected,address))continue;
        snprintf(path,sizeof path,"/proc/1/root/tmp/doveos-bluetooth/input-services/%s/state",kinds[i]);
        file=fopen(path,"r");if(file){if(fgets(state,sizeof state,file))state[strcspn(state,"\r\n")]=0;fclose(file);}
        return g_str_has_prefix(state,"Ready ")&&!g_ascii_strcasecmp(state+6,address)?2:1;
    }
    return 0;
}
static const char *bt_state_text(const char *addr) {
    if(!bt_status_valid)return bt_status_attempted?"Unavailable":"Checking...";
    int i=bt_state_index(addr);
    if(i<0)return "Not paired";
    if(bt_states[i].paired&&bt_states[i].input_state==2)return "Paired, input ready";
    if(bt_states[i].paired&&bt_states[i].input_state==1)return "Paired, reconnecting";
    return bt_states[i].connected?(bt_states[i].paired?"Paired, connected":"Connected"):
        bt_states[i].paired?"Paired":"Not paired";
}
static gboolean bt_type_address(const char *address) {
    if(strlen(address)!=17)return FALSE;
    for(unsigned i=0;i<17;i++)if(i%3==2?address[i]!=':':!g_ascii_isxdigit(address[i]))return FALSE;
    return TRUE;
}
static unsigned bt_stored_transport(const char *address) {
    /* Read only the General transport property; stop before key sections. */
    if(!bt_type_address(address))return 0;
    const char *root="/proc/1/root/var/lib/bluetooth";DIR *dir=opendir(root);
    if(!dir)return 0;
    struct dirent *entry;unsigned flags=0;
    while((entry=readdir(dir))){
        if(!bt_type_address(entry->d_name))continue;
        char path[180];snprintf(path,sizeof path,"%s/%.17s/%.17s/info",root,entry->d_name,address);
        FILE *file=fopen(path,"r");if(!file)continue;
        char line[256];gboolean general=FALSE;
        while(fgets(line,sizeof line,file)){
            if(line[0]=='['){if(general)break;general=!strncmp(line,"[General]",9);}
            if(general&&g_str_has_prefix(line,"SupportedTechnologies=")){
                if(strstr(line,"BR/EDR"))flags|=1;
                if(strstr(line,"LE"))flags|=2;
                break;
            }
        }
        fclose(file);if(flags)break;
    }
    closedir(dir);return flags;
}
static const char *bt_type_text(unsigned transport) {
    return transport==3?"Classic / LE":transport==2?"LE":transport==1?"Classic":"Unknown";
}
static int bt_state_compare(const void *left,const void *right) {
    const struct BtState *a=left,*b=right;
    if(a->connected!=b->connected)return b->connected-a->connected;
    int rc=g_ascii_strcasecmp(a->name,b->name);return rc?rc:strcmp(a->addr,b->addr);
}
static void bt_status_received(GObject *object,GAsyncResult *result,gpointer data) {
    (void)data;GError *error=NULL;
    GVariant *reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),result,&error);
    gboolean changed=!bt_status_attempted||bt_status_valid!=(reply!=NULL);
    bt_status_busy=FALSE;bt_status_valid=reply!=NULL;bt_status_attempted=TRUE;
    if(reply){
        struct BtState previous[G_N_ELEMENTS(bt_states)];memcpy(previous,bt_states,sizeof previous);
        unsigned old_count=bt_state_count;bt_state_count=0;memset(bt_states,0,sizeof bt_states);
        GVariant *objects=g_variant_get_child_value(reply,0),*interfaces;
        GVariantIter iter;const char *path;
        g_variant_iter_init(&iter,objects);
        while(g_variant_iter_next(&iter,"{&o@a{sa{sv}}}",&path,&interfaces)){
            GVariant *props=g_variant_lookup_value(interfaces,"org.bluez.Device1",G_VARIANT_TYPE("a{sv}"));
            if(props){
                const char *addr=NULL,*name=NULL,*address_type=NULL;
                if(g_variant_lookup(props,"Address","&s",&addr)&&strlen(addr)==17&&bt_state_count<G_N_ELEMENTS(bt_states)){
                    struct BtState *state=&bt_states[bt_state_count++];
                    g_strlcpy(state->addr,addr,sizeof state->addr);
                    g_variant_lookup(props,"Paired","b",&state->paired);
                    g_variant_lookup(props,"Connected","b",&state->connected);
                    if(!g_variant_lookup(props,"Alias","&s",&name)||!name||!*name)g_variant_lookup(props,"Name","&s",&name);
                    g_strlcpy(state->name,name&&*name?name:addr,sizeof state->name);
                    const char *bad=NULL;if(!g_utf8_validate(state->name,-1,&bad))*(char*)bad=0;
                    if(g_variant_lookup(props,"AddressType","&s",&address_type)&&!strcmp(address_type,"random"))state->le=TRUE;
                    GVariant *uuids=g_variant_lookup_value(props,"UUIDs",G_VARIANT_TYPE("as"));
                    if(uuids){
                        GVariantIter u;const char *uuid;g_variant_iter_init(&u,uuids);
                        while(g_variant_iter_next(&u,"&s",&uuid)){
                            if(g_str_has_prefix(uuid,"00001800-")||g_str_has_prefix(uuid,"00001812-"))state->le=TRUE;
                            if(g_str_has_prefix(uuid,"0000110b-"))state->audio=TRUE;
                        }
                        g_variant_unref(uuids);
                    }
                    state->input_state=bt_input_state(addr);
                    state->transport=bt_stored_transport(addr);
                    if(!state->transport){
                        if(state->le)state->transport|=2;
                        if(state->audio)state->transport|=1;
                    }
                }
                g_variant_unref(props);
            }
            g_variant_unref(interfaces);
        }
        qsort(bt_states,bt_state_count,sizeof bt_states[0],bt_state_compare);
        changed=changed||old_count!=bt_state_count||memcmp(previous,bt_states,sizeof previous)!=0;
        g_variant_unref(objects);g_variant_unref(reply);
    }
    g_clear_error(&error);
    if(changed)bt_refresh(NULL);
    bt_buttons_refresh();
}
static void bt_status_request(void) {
    g_dbus_connection_call(bt_bus,"org.bluez","/","org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects",NULL,G_VARIANT_TYPE("(a{oa{sa{sv}}})"),G_DBUS_CALL_FLAGS_NONE,
        1500,NULL,bt_status_received,NULL);
}
static void bt_bus_ready(GObject *object,GAsyncResult *result,gpointer data) {
    (void)object;(void)data;GError *error=NULL;
    bt_bus=g_dbus_connection_new_for_address_finish(result,&error);g_clear_error(&error);
    if(bt_bus)bt_status_request();else{
        bt_status_busy=FALSE;bt_status_valid=FALSE;bt_status_attempted=TRUE;bt_refresh(NULL);
    }
}
static gboolean bt_status_poll(gpointer data) {
    (void)data;
    if(bt_status_busy)return TRUE;
    if(bt_bus&&g_dbus_connection_is_closed(bt_bus))g_clear_object(&bt_bus);
    bt_status_busy=TRUE;
    if(bt_bus)bt_status_request();else g_dbus_connection_new_for_address(
        "unix:path=/proc/1/root/var/run/dbus/system_bus_socket",
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT|G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
        NULL,NULL,bt_bus_ready,NULL);
    return TRUE;
}
