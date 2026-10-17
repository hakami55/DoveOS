#ifndef DOVEOS_POWER_SETTINGS_H
#define DOVEOS_POWER_SETTINGS_H
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#define DOVE_POWER_FILE "/media/usb0/DoveOS-v2/home/power-settings.txt"
typedef struct { int battery_seconds, ac_seconds, inhibit_media, battery_dim, ac_dim; } DovePower;
static int dove_power_timeout(int v) { return v==0||v==60||v==120||v==180||v==300||v==600||v==900||v==1800; }
static DovePower dove_power_load(void) {
 DovePower p={180,600,1,60,180},q=p;FILE *f=fopen(DOVE_POWER_FILE,"r");if(!f)return p;
 char tag[20];int n=fscanf(f,"%19s %d %d %d %d %d",tag,&q.battery_seconds,&q.ac_seconds,&q.inhibit_media,&q.battery_dim,&q.ac_dim);fclose(f);
 if(((n==6&&!strcmp(tag,"DOVE_POWER2"))||(n==4&&!strcmp(tag,"DOVE_POWER1")))&&dove_power_timeout(q.battery_seconds)&&dove_power_timeout(q.ac_seconds)&&dove_power_timeout(q.battery_dim)&&dove_power_timeout(q.ac_dim)&&(q.inhibit_media==0||q.inhibit_media==1))p=q;
 return p;
}
static inline int dove_power_save(DovePower p) {
 char temp[180];snprintf(temp,sizeof temp,"%s.%ld.tmp",DOVE_POWER_FILE,(long)getpid());
 FILE *f=fopen(temp,"w");if(!f)return -1;
 int failed=fprintf(f,"DOVE_POWER2 %d %d %d %d %d\n",p.battery_seconds,p.ac_seconds,p.inhibit_media,p.battery_dim,p.ac_dim)<0;
 if(fflush(f)||fsync(fileno(f)))failed=1;
 if(fclose(f))failed=1;
 if(!failed&&rename(temp,DOVE_POWER_FILE)==0)return 0;
 unlink(temp);return -1;
}
#endif
