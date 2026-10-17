/* Read scan results only. No radio commands or saved-profile changes. */
#ifndef DOVE_WIFI_SCAN_INFO_H
#define DOVE_WIFI_SCAN_INFO_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
typedef struct {
    unsigned bands;
    int frequency,dbm,quality,score,seen,has_dbm,has_quality;
    char security[24];
} DoveWifiScanInfo;
static int dove_wifi_channel(int frequency) {
    if(frequency==2484)return 14;
    if(frequency>=2412&&frequency<=2472&&(frequency-2407)%5==0)return (frequency-2407)/5;
    if(frequency>=5000&&frequency<5925&&frequency%5==0)return (frequency-5000)/5;
    if(frequency==5935)return 2;
    if(frequency>=5955&&frequency<=7115&&(frequency-5950)%5==0)return (frequency-5950)/5;
    return 0;
}
static unsigned dove_wifi_band(int frequency) {
    return frequency>=2400&&frequency<=2500?1:frequency>=4900&&frequency<5925?2:
        frequency>=5925&&frequency<=7125?4:0;
}
static int dove_wifi_integer(const char *text,int *value) {
    char *end;long n=strtol(text,&end,10);
    if(end==text||*end||n<-10000||n>10000)return 0;
    *value=(int)n;return 1;
}
static const char *dove_wifi_security(const char *flags) {
    int sae=strstr(flags,"SAE")!=NULL||strstr(flags,"WPA3")!=NULL,psk=strstr(flags,"PSK")!=NULL;
    int rsn=strstr(flags,"WPA2")!=NULL||strstr(flags,"RSN")!=NULL;
    if(sae)return psk?"WPA2/WPA3":"WPA3";
    if(strstr(flags,"OWE"))return "Enhanced Open";
    if(rsn)return strstr(flags,"EAP")?"WPA2 Enterprise":"WPA2";
    if(strstr(flags,"WPA-"))return strstr(flags,"EAP")?"WPA Enterprise":"WPA";
    if(strstr(flags,"WEP"))return "WEP";
    return strstr(flags,"ESS")?"Open":"Unknown";
}
static void dove_wifi_observation(DoveWifiScanInfo *info,int frequency,int dbm,
                                 int quality,const char *security) {
    int has_dbm=dbm>=-120&&dbm<0,has_quality=quality>=0&&quality<=100;
    int score=has_dbm?2*(dbm+100):has_quality?quality:-1;
    if(score>100)score=100;
    if(has_dbm&&score<0)score=0;
    info->bands|=dove_wifi_band(frequency);
    if(!info->seen||(has_dbm&&info->has_dbm?dbm>info->dbm:
                    score>info->score||(score==info->score&&has_dbm&&!info->has_dbm))){
        info->seen=1;info->score=score;info->frequency=frequency;
        info->dbm=dbm;info->quality=quality;info->has_dbm=has_dbm;info->has_quality=has_quality;
        snprintf(info->security,sizeof info->security,"%s",security);
    }
}
static void dove_wifi_details(const char *path,char names[][64],DoveWifiScanInfo *info,int count) {
    FILE *f=fopen(path,"r");if(!f)return;
    char line[2048];
    while(fgets(line,sizeof line,f)){
        line[strcspn(line,"\r\n")]=0;
        char *fields[5],*next=line;int complete=1;
        for(int k=0;k<4;k++){
            fields[k]=next;char *tab=strchr(next,'\t');
            if(!tab){complete=0;break;}*tab=0;next=tab+1;
        }
        if(!complete)continue;
        fields[4]=next;
        if(strlen(fields[0])!=17)continue;
        int valid=1;
        for(int k=0;k<17;k++)if(k%3==2?fields[0][k]!=':':!isxdigit((unsigned char)fields[0][k]))valid=0;
        int frequency,dbm;
        if(!valid||!dove_wifi_integer(fields[1],&frequency)||!dove_wifi_integer(fields[2],&dbm))continue;
        for(int k=0;k<count;k++)if(!strcmp(names[k],fields[4])){
            dove_wifi_observation(&info[k],frequency,dbm,-1,dove_wifi_security(fields[3]));break;
        }
    }
    fclose(f);
}
static void dove_wifi_wext_cell(char names[][64],DoveWifiScanInfo *info,int count,
                               const char *name,int frequency,int dbm,int quality,const char *security) {
    for(int k=0;k<count;k++)if(!strcmp(names[k],name)){
        dove_wifi_observation(&info[k],frequency,dbm,quality,security);break;
    }
}
static void dove_wifi_wext(const char *path,char names[][64],DoveWifiScanInfo *info,int count) {
    FILE *f=fopen(path,"r");if(!f)return;
    char line[512],name[64]="",security[24]="Unknown";int frequency=0,dbm=0,quality=-1;
    while(fgets(line,sizeof line,f)){
        if(strstr(line,"Cell ")&&strstr(line,"Address:")){
            dove_wifi_wext_cell(names,info,count,name,frequency,dbm,quality,security);
            name[0]=0;strcpy(security,"Unknown");frequency=dbm=0;quality=-1;
        }
        char *p=strstr(line,"ESSID:\"");if(p){p+=7;char *end=strrchr(p,'"');if(end){*end=0;snprintf(name,sizeof name,"%s",p);}}
        p=strstr(line,"Frequency:");if(p){double ghz=0;if(sscanf(p+10,"%lf GHz",&ghz)==1&&ghz>=2&&ghz<8)frequency=(int)(ghz*1000+0.5);}
        p=strstr(line,"Signal level=");if(p&&strstr(p,"dBm")){int n;if(sscanf(p+13,"%d",&n)==1)dbm=n;}
        p=strstr(line,"Quality=");if(p){int n,max;if(sscanf(p+8,"%d/%d",&n,&max)==2&&n>=0&&max>0&&n<=max)quality=(int)(100.0*n/max);}
        if(strstr(line,"Encryption key:off"))strcpy(security,"Open");
        if(strstr(line,"Encryption key:on"))strcpy(security,"Encrypted");
        if(strstr(line,"WPA Version"))strcpy(security,"WPA");
        if(strstr(line,"WPA2")||strstr(line,"802.11i"))strcpy(security,"WPA2");
    }
    dove_wifi_wext_cell(names,info,count,name,frequency,dbm,quality,security);fclose(f);
}
static void dove_wifi_info_text(const DoveWifiScanInfo *info,char *band,size_t band_size,
                                char *strength,size_t strength_size,char *channel,size_t channel_size) {
    const char *bands[]={"Unknown","2.4 GHz","5 GHz","2.4 / 5 GHz","6 GHz","2.4 / 6 GHz","5 / 6 GHz","2.4 / 5 / 6 GHz"};
    snprintf(band,band_size,"%s",bands[info->bands&7]);
    if(info->has_dbm)snprintf(strength,strength_size,"%d dBm",info->dbm);
    else if(info->has_quality)snprintf(strength,strength_size,"%d%%",info->quality);
    else snprintf(strength,strength_size,"Unknown");
    int n=dove_wifi_channel(info->frequency);
    if(n)snprintf(channel,channel_size,"%d",n);else snprintf(channel,channel_size,"Unknown");
}
#endif
