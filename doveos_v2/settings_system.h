#ifndef DOVEOS_SETTINGS_SYSTEM_H
#define DOVEOS_SETTINGS_SYSTEM_H
#include <sys/utsname.h>
#include <sys/statvfs.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
static GtkWidget *system_values[12];
static const char *system_names[]={"Device","Kernel","Processor","Memory available","Swap used","Desktop renderer","Desktop resolution","System USB space","Wi-Fi address","Ethernet address","Uptime","Build"};
static void system_value(int row,const char *s){gtk_label_set_text(GTK_LABEL(system_values[row]),s&&*s?s:"Unavailable");gtk_widget_set_tooltip_text(system_values[row],s&&*s?s:"Unavailable");}
static void system_refresh(GtkWidget *widget,gpointer data){
 (void)widget;(void)data;if(!system_values[0])return;
 char s[512],line[512];struct utsname u;
 system_value(0,"HP Elite x3 Lap Dock");
 if(!uname(&u)){snprintf(s,sizeof s,"%s %s (%s)",u.sysname,u.release,u.machine);system_value(1,s);}else system_value(1,NULL);
 FILE *f=fopen("/proc/cpuinfo","r");s[0]=0;
 if(f){while(fgets(line,sizeof line,f))if(!strncmp(line,"model name",10)||!strncmp(line,"Processor",9)){char *v=strchr(line,':');if(v){v++;while(*v==' '||*v=='\t')v++;v[strcspn(v,"\r\n")]=0;snprintf(s,sizeof s,"%s",v);break;}}fclose(f);}
 if(!s[0])snprintf(s,sizeof s,"%s",!uname(&u)?u.machine:"Unavailable");
 long cores=sysconf(_SC_NPROCESSORS_ONLN);char cpu[600];snprintf(cpu,sizeof cpu,"%s | %ld online core%s",s,cores,cores==1?"":"s");system_value(2,cpu);
 unsigned long long total=0,available=0,free_mem=0,buffers=0,cached=0,reclaim=0,shmem=0,swap=0,swapfree=0;
 f=fopen("/proc/meminfo","r");
 if(f){while(fgets(line,sizeof line,f)){char key[64];unsigned long long n;if(sscanf(line,"%63s %llu",key,&n)!=2)continue;
  if(!strcmp(key,"MemTotal:"))total=n;else if(!strcmp(key,"MemAvailable:"))available=n;else if(!strcmp(key,"MemFree:"))free_mem=n;else if(!strcmp(key,"Buffers:"))buffers=n;else if(!strcmp(key,"Cached:"))cached=n;else if(!strcmp(key,"SReclaimable:"))reclaim=n;else if(!strcmp(key,"Shmem:"))shmem=n;else if(!strcmp(key,"SwapTotal:"))swap=n;else if(!strcmp(key,"SwapFree:"))swapfree=n;
 }fclose(f);}
 int estimated=!available;unsigned long long pool=free_mem+buffers+cached+reclaim;if(estimated)available=pool>shmem?pool-shmem:free_mem;if(available>total)available=total;
 if(total){snprintf(s,sizeof s,"%.1f / %.1f MiB%s",available/1024.0,total/1024.0,estimated?" (estimate)":"");system_value(3,s);}else system_value(3,NULL);
 if(swap){snprintf(s,sizeof s,"%.1f / %.1f MiB",(swap>=swapfree?swap-swapfree:0)/1024.0,swap/1024.0);system_value(4,s);}else system_value(4,"Not active");
 read_line("/tmp/doveos-render-status",s,sizeof s);system_value(5,s);
 read_line(CLOCK_HOME "desktop-resolution.txt",s,sizeof s);system_value(6,s);
 struct statvfs fs;if(!statvfs("/media/usb0",&fs)){snprintf(s,sizeof s,"%.2f GiB free / %.2f GiB total",(double)fs.f_bavail*fs.f_frsize/1073741824.0,(double)fs.f_blocks*fs.f_frsize/1073741824.0);system_value(7,s);}else system_value(7,NULL);
 GString *wifi=g_string_new(""),*wired=g_string_new("");struct ifaddrs *addresses=NULL;
 if(!getifaddrs(&addresses)){for(struct ifaddrs *a=addresses;a;a=a->ifa_next){if(!a->ifa_addr||a->ifa_addr->sa_family!=AF_INET)continue;const char *name=a->ifa_name;
  GString *dest=!strncmp(name,"wl",2)?wifi:(!strncmp(name,"eth",3)||!strncmp(name,"ax",2)||!strncmp(name,"rt",2))?wired:NULL;
  if(dest){char ip[INET_ADDRSTRLEN];if(inet_ntop(AF_INET,&((struct sockaddr_in*)a->ifa_addr)->sin_addr,ip,sizeof ip)){if(dest->len)g_string_append(dest,", ");g_string_append_printf(dest,"%s (%s)",ip,name);}}
 }freeifaddrs(addresses);}system_value(8,wifi->len?wifi->str:"Not connected");system_value(9,wired->len?wired->str:"Not connected");g_string_free(wifi,TRUE);g_string_free(wired,TRUE);
 double seconds=0;f=fopen("/proc/uptime","r");if(f){if(fscanf(f,"%lf",&seconds)!=1)seconds=0;fclose(f);}snprintf(s,sizeof s,"%lu h %lu min",(unsigned long)seconds/3600,(unsigned long)seconds/60%60);system_value(10,s);
 system_value(11,"DoveOS v2 | UI update 2026-10-18");
}
static void system_copy(GtkWidget *widget,gpointer data){(void)widget;(void)data;system_refresh(NULL,NULL);GString *text=g_string_new("DoveOS system details\n");for(int i=0;i<12;i++)g_string_append_printf(text,"%s: %s\n",system_names[i],gtk_label_get_text(GTK_LABEL(system_values[i])));gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),text->str,-1);g_string_free(text,TRUE);snprintf(message,sizeof message,"System details copied.");}
static gboolean system_tick(gpointer data){(void)data;if(book&&gtk_notebook_get_current_page(GTK_NOTEBOOK(book))==9)system_refresh(NULL,NULL);return TRUE;}
static void system_build(GtkWidget *box){
 GtkWidget *table=gtk_table_new(12,2,FALSE);gtk_table_set_col_spacings(GTK_TABLE(table),14);gtk_table_set_row_spacings(GTK_TABLE(table),7);add(box,table);
 for(int i=0;i<12;i++){GtkWidget *name=gtk_label_new(system_names[i]);gtk_misc_set_alignment(GTK_MISC(name),0,0.5);gtk_table_attach(GTK_TABLE(table),name,0,1,i,i+1,GTK_FILL,GTK_FILL,0,0);system_values[i]=gtk_label_new("Unavailable");gtk_misc_set_alignment(GTK_MISC(system_values[i]),0,0.5);gtk_label_set_selectable(GTK_LABEL(system_values[i]),TRUE);gtk_label_set_ellipsize(GTK_LABEL(system_values[i]),PANGO_ELLIPSIZE_END);gtk_table_attach(GTK_TABLE(table),system_values[i],1,2,i,i+1,GTK_EXPAND|GTK_FILL,GTK_FILL,0,0);}
 GtkWidget *note=gtk_label_new("Memory is the amount reported to Linux.\nThese readings do not change hardware settings.");gtk_misc_set_alignment(GTK_MISC(note),0,0.5);add(box,note);
 GtkWidget *row=gtk_hbox_new(FALSE,8);add(box,row);GtkWidget *refresh=gtk_button_new_with_label("Refresh"),*copy=gtk_button_new_with_label("Copy details");add(row,refresh);add(row,copy);g_signal_connect(refresh,"clicked",G_CALLBACK(system_refresh),NULL);g_signal_connect(copy,"clicked",G_CALLBACK(system_copy),NULL);system_refresh(NULL,NULL);g_timeout_add_seconds(5,system_tick,NULL);
}
#endif
