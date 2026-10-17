/* Verified six-byte BLE mouse reports to DoveOS X11; no UHID or input logging. */
#define _POSIX_C_SOURCE 200809L
#include <gio/gio.h>
#include <glib-unix.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
static GDBusConnection *bus;
static GMainLoop *loop;
static Display *display;
static char device[128],report_path[256];
static gboolean polling,starting,ready;
static gint64 began;
static unsigned held,reports;
static int result_code;
static const unsigned buttons[8]={1,3,2,8,9,10,11,12};
static const char *state_file="/tmp/doveos-bluetooth/hid/state";
static const char *ready_file="/tmp/doveos-bluetooth/hid/ready";
static void state(const char *text){FILE *f=fopen(state_file,"w");if(f){fprintf(f,"%s\n",text);fclose(f);}puts(text);}
static void release_buttons(void){for(unsigned i=0;i<8;i++)if(held&(1u<<i))XTestFakeButtonEvent(display,buttons[i],False,0);held=0;XFlush(display);}
static void finish(const char *text,int code){state(text);result_code=code;g_main_loop_quit(loop);}
static gboolean stop_service(gpointer data){(void)data;finish("Mouse input stopped.",0);return G_SOURCE_REMOVE;}
static gboolean blocked(void){return access("/tmp/doveos-gpu-display-paused",F_OK)==0||access("/tmp/doveos-direct-display",F_OK)==0;}
static void activity(void){static time_t last;time_t at=time(NULL);if(at==last)return;last=at;int fd=open("/tmp/doveos-power-wake",O_WRONLY|O_CREAT|O_CLOEXEC,0600);if(fd>=0)close(fd);}
static void wheel(int value,unsigned positive,unsigned negative){unsigned button=value>0?positive:negative;unsigned n=value<0?(unsigned)-value:(unsigned)value;while(n--){XTestFakeButtonEvent(display,button,True,0);XTestFakeButtonEvent(display,button,False,0);}}
static void notification(GDBusConnection *connection,const gchar *sender,const gchar *path,const gchar *interface,const gchar *signal,GVariant *args,gpointer data){
 (void)connection;(void)sender;(void)interface;(void)signal;(void)data;
 const char *name;GVariant *props,*invalid;g_variant_get(args,"(&s@a{sv}@as)",&name,&props,&invalid);
 if(!strcmp(path,device)&&!strcmp(name,"org.bluez.Device1")){
  gboolean connected=TRUE,paired=TRUE;
  if((g_variant_lookup(props,"Connected","b",&connected)&&!connected&&ready)||(g_variant_lookup(props,"Paired","b",&paired)&&!paired))finish("Mouse disconnected. Wake it and press Reconnect.",0);
 }else if(ready&&!strcmp(path,report_path)&&!strcmp(name,"org.bluez.GattCharacteristic1")){
  GVariant *value=g_variant_lookup_value(props,"Value",G_VARIANT_TYPE("ay"));
  if(value){
   gsize n=0;const guint8 *v=g_variant_get_fixed_array(value,&n,1);
   if(n==6){
    activity();
    if(blocked())release_buttons();
    else{
     int dx=v[1]|((v[2]&15)<<8),dy=(v[2]>>4)|(v[3]<<4);if(dx&0x800)dx-=4096;if(dy&0x800)dy-=4096;
     if(dx||dy)XTestFakeRelativeMotionEvent(display,dx,dy,0);
     for(unsigned i=0;i<8;i++)if((held^v[0])&(1u<<i))XTestFakeButtonEvent(display,buttons[i],!!(v[0]&(1u<<i)),0);
     held=v[0];wheel((gint8)v[4],4,5);wheel((gint8)v[5],7,6);XFlush(display);reports++;
     if(reports==1)puts("MOUSE_INPUT_FORWARDED");
    }
   }
   g_variant_unref(value);
  }
 }
 g_variant_unref(props);g_variant_unref(invalid);
}
static GVariant *read_value(const char *path,const char *interface){
 return g_dbus_connection_call_sync(bus,"org.bluez",path,interface,"ReadValue",g_variant_new("(a{sv})",NULL),G_VARIANT_TYPE("(ay)"),G_DBUS_CALL_FLAGS_NONE,1500,NULL,NULL);
}
static void started(GObject *object,GAsyncResult *async,gpointer data){
 (void)data;GError *error=NULL;GVariant *reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),async,&error);starting=FALSE;
 if(!reply){g_clear_error(&error);return;}
 g_variant_unref(reply);ready=TRUE;
 FILE *file=fopen(ready_file,"w");if(file){fputs("ready\n",file);fclose(file);}
 state("Bluetooth mouse connected. Desktop input ready.");
}
static void objects(GObject *object,GAsyncResult *async,gpointer data){
 (void)data;GError *error=NULL;GVariant *reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),async,&error);polling=FALSE;
 if(!reply){g_clear_error(&error);return;}
 GVariant *all=g_variant_get_child_value(reply,0);GVariantIter iter;GVariant *interfaces;const char *path;
 gboolean connected=FALSE,resolved=FALSE,paired=FALSE;char map[256]={0};
 g_variant_iter_init(&iter,all);
 while(g_variant_iter_next(&iter,"{&o@a{sa{sv}}}",&path,&interfaces)){
  if(!strcmp(path,device)){
   GVariant *props=g_variant_lookup_value(interfaces,"org.bluez.Device1",G_VARIANT_TYPE("a{sv}"));
   if(props){g_variant_lookup(props,"Connected","b",&connected);g_variant_lookup(props,"ServicesResolved","b",&resolved);g_variant_lookup(props,"Paired","b",&paired);g_variant_unref(props);}
  }
  g_variant_unref(interfaces);
 }
 if(connected&&resolved&&paired&&!starting&&!ready){
  size_t prefix=strlen(device);g_variant_iter_init(&iter,all);
  while(g_variant_iter_next(&iter,"{&o@a{sa{sv}}}",&path,&interfaces)){
   if(!strncmp(path,device,prefix)&&path[prefix]=='/'){
    GVariant *props=g_variant_lookup_value(interfaces,"org.bluez.GattCharacteristic1",G_VARIANT_TYPE("a{sv}"));
    const char *uuid=NULL;if(props){if(g_variant_lookup(props,"UUID","&s",&uuid)&&!strcmp(uuid,"00002a4b-0000-1000-8000-00805f9b34fb"))g_strlcpy(map,path,sizeof map);g_variant_unref(props);}
   }
   g_variant_unref(interfaces);
  }
  if(*map){
   GVariant *read=read_value(map,"org.bluez.GattCharacteristic1");gboolean supported=FALSE;
   if(read){GVariant *value=g_variant_get_child_value(read,0);gsize n=0;const guint8 *v=g_variant_get_fixed_array(value,&n,1);char *hash=g_compute_checksum_for_data(G_CHECKSUM_SHA256,v,n);supported=n==92&&!strcmp(hash,"8b931c618cd3a6a6454d813d70799a43356d6ac86920598ad12cff0a7d3402ae");g_free(hash);g_variant_unref(value);g_variant_unref(read);}
   if(!supported){if(read)finish("This mouse report layout is not supported by the verified input helper.",4);goto done;}
   g_variant_iter_init(&iter,all);
   while(g_variant_iter_next(&iter,"{&o@a{sa{sv}}}",&path,&interfaces)){
    if(!strncmp(path,device,prefix)&&path[prefix]=='/'){
     GVariant *props=g_variant_lookup_value(interfaces,"org.bluez.GattDescriptor1",G_VARIANT_TYPE("a{sv}"));
     const char *uuid=NULL,*characteristic=NULL;
     if(props){
      if(g_variant_lookup(props,"UUID","&s",&uuid)&&!strcmp(uuid,"00002908-0000-1000-8000-00805f9b34fb")&&g_variant_lookup(props,"Characteristic","&o",&characteristic)){
       GVariant *reference=read_value(path,"org.bluez.GattDescriptor1");
       if(reference){GVariant *value=g_variant_get_child_value(reference,0);gsize n=0;const guint8 *v=g_variant_get_fixed_array(value,&n,1);if(n==2&&v[0]==1&&v[1]==1)g_strlcpy(report_path,characteristic,sizeof report_path);g_variant_unref(value);g_variant_unref(reference);}
      }
      g_variant_unref(props);
     }
    }
    g_variant_unref(interfaces);
   }
   if(*report_path){starting=TRUE;g_dbus_connection_call(bus,"org.bluez",report_path,"org.bluez.GattCharacteristic1","StartNotify",NULL,NULL,G_DBUS_CALL_FLAGS_NONE,2000,NULL,started,NULL);}
  }
 }
done:
 g_variant_unref(all);g_variant_unref(reply);
}
static gboolean tick(gpointer data){
 (void)data;
 if(blocked()&&held)release_buttons();
 if(!ready&&g_get_monotonic_time()-began>20000000){finish("Mouse did not reconnect. Keep it awake on the saved Bluetooth channel and retry.",2);return G_SOURCE_REMOVE;}
 if(!ready&&!polling&&!starting){polling=TRUE;g_dbus_connection_call(bus,"org.bluez","/","org.freedesktop.DBus.ObjectManager","GetManagedObjects",NULL,G_VARIANT_TYPE("(a{oa{sa{sv}}})"),G_DBUS_CALL_FLAGS_NONE,1500,NULL,objects,NULL);}
 return G_SOURCE_CONTINUE;
}
static void owner_changed(GDBusConnection *c,const gchar *s,const gchar *p,const gchar *i,const gchar *n,GVariant *args,gpointer data){(void)c;(void)s;(void)p;(void)i;(void)n;(void)args;(void)data;finish("Bluetooth service changed. Press Reconnect again.",2);}
int main(int argc,char **argv){
 if(argc!=2||strlen(argv[1])!=17)return 2;
 strcpy(device,"/org/bluez/hci0/dev_");size_t at=strlen(device);
 for(unsigned i=0;i<17;i++){char c=argv[1][i];if(i%3==2){if(c!=':')return 2;c='_';}else if(!g_ascii_isxdigit(c))return 2;device[at+i]=g_ascii_toupper(c);}device[at+17]=0;
 setvbuf(stdout,NULL,_IOLBF,0);GError *error=NULL;bus=g_bus_get_sync(G_BUS_TYPE_SYSTEM,NULL,&error);
 if(!bus){g_clear_error(&error);return 3;}
 display=XOpenDisplay(":7");if(!display)return 3;
 int e,b,major,minor;if(!XTestQueryExtension(display,&e,&b,&major,&minor)){XCloseDisplay(display);return 3;}
 loop=g_main_loop_new(NULL,FALSE);began=g_get_monotonic_time();
 g_unix_signal_add(SIGTERM,stop_service,NULL);g_unix_signal_add(SIGINT,stop_service,NULL);g_unix_signal_add(SIGHUP,stop_service,NULL);
 g_dbus_connection_signal_subscribe(bus,"org.bluez","org.freedesktop.DBus.Properties","PropertiesChanged",NULL,NULL,G_DBUS_SIGNAL_FLAGS_NONE,notification,NULL,NULL);
 g_dbus_connection_signal_subscribe(bus,"org.freedesktop.DBus","org.freedesktop.DBus","NameOwnerChanged","/org/freedesktop/DBus","org.bluez",G_DBUS_SIGNAL_FLAGS_NONE,owner_changed,NULL,NULL);
 state("Connecting Bluetooth mouse input...");tick(NULL);g_timeout_add_seconds(1,tick,NULL);g_main_loop_run(loop);
 release_buttons();unlink(ready_file);XCloseDisplay(display);g_object_unref(bus);printf("MOUSE_INPUT_ENDED reports=%u buttons_released=1\n",reports);return result_code;
}
