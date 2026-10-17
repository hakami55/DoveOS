/* Verified eight-byte BLE keyboard reports to DoveOS X11; no UHID or input logging. */
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
static unsigned reports;static unsigned char old[256];
static int result_code;
static const unsigned char map[256]={
 [4]=30,[5]=48,[6]=46,[7]=32,[8]=18,[9]=33,[10]=34,[11]=35,[12]=23,[13]=36,[14]=37,[15]=38,
 [16]=50,[17]=49,[18]=24,[19]=25,[20]=16,[21]=19,[22]=31,[23]=20,[24]=22,[25]=47,[26]=17,[27]=45,[28]=21,[29]=44,
 [30]=2,[31]=3,[32]=4,[33]=5,[34]=6,[35]=7,[36]=8,[37]=9,[38]=10,[39]=11,
 [40]=28,[41]=1,[42]=14,[43]=15,[44]=57,[45]=12,[46]=13,[47]=26,[48]=27,[49]=43,[50]=43,
 [51]=39,[52]=40,[53]=41,[54]=51,[55]=52,[56]=53,[57]=58,
 [58]=59,[59]=60,[60]=61,[61]=62,[62]=63,[63]=64,[64]=65,[65]=66,[66]=67,[67]=68,[68]=87,[69]=88,
 [70]=99,[71]=70,[72]=119,[73]=110,[74]=102,[75]=104,[76]=111,[77]=107,[78]=109,[79]=106,[80]=105,[81]=108,[82]=103,
 [83]=69,[84]=98,[85]=55,[86]=74,[87]=78,[88]=96,[89]=79,[90]=80,[91]=81,[92]=75,[93]=76,[94]=77,[95]=71,[96]=72,[97]=73,[98]=82,[99]=83,[100]=86,[101]=127,
 [224]=29,[225]=42,[226]=56,[227]=125,[228]=97,[229]=54,[230]=100,[231]=126
};
static void key(Display*d,unsigned code,int down){if(code)XTestFakeKeyEvent(d,code+8,down,CurrentTime);}
static int ismod(unsigned code){for(unsigned i=224;i<232;i++)if(map[i]==code)return 1;return 0;}

static const char *state_file="/tmp/doveos-bluetooth/keyboard-hid/state";
static const char *ready_file="/tmp/doveos-bluetooth/keyboard-hid/ready";
static void state(const char *text){FILE *f=fopen(state_file,"w");if(f){fprintf(f,"%s\n",text);fclose(f);}puts(text);}
static void release_keys(void){for(unsigned i=1;i<256;i++)if(old[i])key(display,i,0);memset(old,0,sizeof old);XFlush(display);}
static void finish(const char *text,int code){state(text);result_code=code;g_main_loop_quit(loop);}
static gboolean stop_service(gpointer data){(void)data;finish("Keyboard input stopped.",0);return G_SOURCE_REMOVE;}
static gboolean blocked(void){return access("/tmp/doveos-gpu-display-paused",F_OK)==0||access("/tmp/doveos-direct-display",F_OK)==0;}
static void activity(void){static time_t last;time_t at=time(NULL);if(at==last)return;last=at;int fd=open("/tmp/doveos-power-wake",O_WRONLY|O_CREAT|O_CLOEXEC,0600);if(fd>=0)close(fd);}
static void notification(GDBusConnection *connection,const gchar *sender,const gchar *path,const gchar *interface,const gchar *signal,GVariant *args,gpointer data){
 (void)connection;(void)sender;(void)interface;(void)signal;(void)data;
 const char *name;GVariant *props,*invalid;g_variant_get(args,"(&s@a{sv}@as)",&name,&props,&invalid);
 if(!strcmp(path,device)&&!strcmp(name,"org.bluez.Device1")){
  gboolean connected=TRUE,paired=TRUE;
  if((g_variant_lookup(props,"Connected","b",&connected)&&!connected&&ready)||(g_variant_lookup(props,"Paired","b",&paired)&&!paired))finish("Keyboard disconnected. Wake it and press Reconnect.",0);
 }else if(ready&&!strcmp(path,report_path)&&!strcmp(name,"org.bluez.GattCharacteristic1")){
  GVariant *value=g_variant_lookup_value(props,"Value",G_VARIANT_TYPE("ay"));
  if(value){
   gsize n=0;const guint8 *v=g_variant_get_fixed_array(value,&n,1);
   if(n==8){
    activity();
    if(blocked())release_keys();
    else{
     int rollover=0;for(unsigned i=2;i<8;i++)if(v[i]>=1&&v[i]<=3)rollover=1;
     if(!rollover){
      unsigned char next[256]={0};for(unsigned i=0;i<8;i++)if(v[0]&(1u<<i))next[map[224+i]]=1;
      for(unsigned i=2;i<8;i++)if(map[v[i]])next[map[v[i]]]=1;
      for(unsigned i=1;i<256;i++)if(old[i]&&!next[i]&&!ismod(i))key(display,i,0);
      for(unsigned i=224;i<232;i++)if(old[map[i]]&&!next[map[i]])key(display,map[i],0);
      for(unsigned i=224;i<232;i++)if(!old[map[i]]&&next[map[i]])key(display,map[i],1);
      for(unsigned i=1;i<256;i++)if(!old[i]&&next[i]&&!ismod(i))key(display,i,1);
      memcpy(old,next,sizeof old);XFlush(display);reports++;if(reports==1)puts("KEYBOARD_INPUT_FORWARDED");
     }
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
 state("Bluetooth keyboard connected. Desktop input ready.");
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
   if(read){GVariant *value=g_variant_get_child_value(read,0);gsize n=0;const guint8 *v=g_variant_get_fixed_array(value,&n,1);char *hash=g_compute_checksum_for_data(G_CHECKSUM_SHA256,v,n);supported=n==256&&!strcmp(hash,"39cd6605709d2e8d53f0549455209f8f24764f5f918c7ae5ab7104aa109f27bd");g_free(hash);g_variant_unref(value);g_variant_unref(read);}
   if(!supported){if(read)finish("This keyboard report layout is not supported by the verified input helper.",4);goto done;}
   g_variant_iter_init(&iter,all);
   while(g_variant_iter_next(&iter,"{&o@a{sa{sv}}}",&path,&interfaces)){
    if(!strncmp(path,device,prefix)&&path[prefix]=='/'){
     GVariant *props=g_variant_lookup_value(interfaces,"org.bluez.GattDescriptor1",G_VARIANT_TYPE("a{sv}"));
     const char *uuid=NULL,*characteristic=NULL;
     if(props){
      if(g_variant_lookup(props,"UUID","&s",&uuid)&&!strcmp(uuid,"00002908-0000-1000-8000-00805f9b34fb")&&g_variant_lookup(props,"Characteristic","&o",&characteristic)){
       GVariant *reference=read_value(path,"org.bluez.GattDescriptor1");
       if(reference){GVariant *value=g_variant_get_child_value(reference,0);gsize n=0;const guint8 *v=g_variant_get_fixed_array(value,&n,1);if(n==2&&v[0]==2&&v[1]==1)g_strlcpy(report_path,characteristic,sizeof report_path);g_variant_unref(value);g_variant_unref(reference);}
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
 if(blocked())release_keys();
 if(!ready&&g_get_monotonic_time()-began>20000000){finish("Keyboard did not reconnect. Keep it awake on the saved Bluetooth channel and retry.",2);return G_SOURCE_REMOVE;}
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
 state("Connecting Bluetooth keyboard input...");tick(NULL);g_timeout_add_seconds(1,tick,NULL);g_main_loop_run(loop);
 release_keys();unlink(ready_file);XCloseDisplay(display);g_object_unref(bus);printf("KEYBOARD_INPUT_ENDED reports=%u buttons_released=1\n",reports);return result_code;
}
