#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#define CLOCK_HOME "/media/usb0/DoveOS-v2/home/"
static inline void clock_zone(void) {
 char zone[32]="AST-3"; FILE *f=fopen(CLOCK_HOME "time-zone.txt","r");
 if(f){if(!fgets(zone,sizeof zone,f))strcpy(zone,"AST-3");fclose(f);zone[strcspn(zone,"\r\n")]=0;}
 if(strcmp(zone,"AST-3")&&strcmp(zone,"UTC0"))strcpy(zone,"AST-3");
 setenv("TZ",zone,1);tzset();
}
static inline int clock_24h(void) {FILE *f=fopen(CLOCK_HOME "clock-format.txt","r");int n=f?fgetc(f):0;if(f)fclose(f);return n=='2';}
static inline const char *clock_zone_label(void) {const char *zone=getenv("TZ");return zone&&!strcmp(zone,"UTC0")?"UTC":"Riyadh (UTC+3)";}
static inline int clock_save(const char *name,const char *value) {
 char path[180],tmp[210];snprintf(path,sizeof path,CLOCK_HOME "%s",name);snprintf(tmp,sizeof tmp,"%s.new.%ld",path,(long)getpid());
 FILE *f=fopen(tmp,"w");if(!f)return -1;int failed=fprintf(f,"%s\n",value)<0;failed|=fflush(f)!=0;failed|=fsync(fileno(f))!=0;failed|=fclose(f)!=0;
 if(failed||rename(tmp,path))return -1;
 return 0;
}

