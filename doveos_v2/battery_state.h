#ifndef DOVEOS_BATTERY_STATE_H
#define DOVEOS_BATTERY_STATE_H
#include <stdio.h>
#include <time.h>
typedef struct { unsigned percent; int external; char status[40]; } DoveBattery;
/* One reader owns ITE requests; GUI programs consume its atomic RAM snapshot. */
static int dove_battery_read(DoveBattery *out) {
 FILE *f=fopen("/tmp/doveos-battery-state","r");if(!f)return 0;
 double sampled=0;unsigned pct=0,voltage=0,flags=0,ac=0;int current=0;
 int n=fscanf(f,"DOVE_BATTERY1 %lf %u %u %d %u %u",&sampled,&pct,&voltage,&current,&flags,&ac);fclose(f);
 struct timespec mono;if(clock_gettime(CLOCK_MONOTONIC,&mono))return 0;
 double age=mono.tv_sec+mono.tv_nsec/1e9-sampled;
 if(n!=6 || pct>100 || ac>1 || !(age>=0 && age<=12) || !(flags&0x80))return 0;
 out->percent=pct;out->external=(int)ac;
 const char *state=!ac?"Discharging":current>0?"Charging":current<0?"Battery supplying power":(flags&0x20)||pct==100?"Full":"Not charging";
 snprintf(out->status,sizeof out->status,"%s",state);return 1;
}
#endif
