#ifndef DOVE_KEYBOARD_LAYOUTS_H
#define DOVE_KEYBOARD_LAYOUTS_H
#include "keyboard_settings.h"
#ifndef DOVE_LAYOUT_FILE
#define DOVE_LAYOUT_FILE "/media/usb0/DoveOS-v2/home/keyboard-layouts.txt"
#endif
#ifndef DOVE_LAYOUT_CATALOGUE
#define DOVE_LAYOUT_CATALOGUE "/tmp/doveos-theme/keyboard-layouts.tsv"
#endif
typedef struct {char id[16],badge[3],name[128];} DoveLayoutInfo;
typedef struct {int count;char ids[2][16];} DoveLayouts;
static inline int dove_layout_catalogue(DoveLayoutInfo *items,int capacity){
 FILE *f=fopen(DOVE_LAYOUT_CATALOGUE,"r");if(!f)return 0;
 char line[200];int n=0;while(n<capacity&&fgets(line,sizeof line,f)){
  char *a=strchr(line,'\t');if(!a)continue;*a++=0;char *b=strchr(a,'\t');if(!b)continue;*b++=0;b[strcspn(b,"\r\n")]=0;
  if(!line[0]||strlen(line)>=sizeof items[n].id||strlen(a)!=2||!b[0]||strlen(b)>=sizeof items[n].name)continue;
  strcpy(items[n].id,line);strcpy(items[n].badge,a);strcpy(items[n].name,b);n++;
 }fclose(f);return n;
}
static inline int dove_layout_info(const char *id,DoveLayoutInfo *info){
 FILE *f=fopen(DOVE_LAYOUT_CATALOGUE,"r");if(!f)return 0;
 char line[200];int found=0;while(fgets(line,sizeof line,f)){
  char *a=strchr(line,'\t');if(!a)continue;*a++=0;if(strcmp(line,id))continue;
  char *b=strchr(a,'\t');if(!b)break;*b++=0;b[strcspn(b,"\r\n")]=0;
  if(strlen(line)>=sizeof info->id||strlen(a)!=2||strlen(b)>=sizeof info->name)break;
  strcpy(info->id,line);strcpy(info->badge,a);strcpy(info->name,b);found=1;break;
 }fclose(f);return found;
}
static inline DoveLayouts dove_layout_legacy(void){
 DoveKeyboard k=dove_keyboard_load();DoveLayouts l={.count=k.layout==2?2:1};
 strcpy(l.ids[0],k.layout==1?"ara":"us");if(l.count==2)strcpy(l.ids[1],"ara");return l;
}
static inline int dove_layout_valid(const DoveLayouts *l){
 if(l->count<1||l->count>2)return 0;
 for(int i=0;i<l->count;i++){DoveLayoutInfo info;if(!dove_layout_info(l->ids[i],&info))return 0;}
 return l->count!=2||strcmp(l->ids[0],l->ids[1]);
}
static inline DoveLayouts dove_layout_load(void){
 DoveLayouts l={0};FILE *f=fopen(DOVE_LAYOUT_FILE,"r");if(!f)return dove_layout_legacy();
 char tag[32];if(!fgets(tag,sizeof tag,f)||strcmp(tag,"DOVE_LAYOUTS1\n")){fclose(f);return dove_layout_legacy();}
 char id[32];while(fgets(id,sizeof id,f)){id[strcspn(id,"\r\n")]=0;if(l.count==2||!id[0]||strlen(id)>=sizeof l.ids[0]){fclose(f);return dove_layout_legacy();}strcpy(l.ids[l.count++],id);}
 fclose(f);return dove_layout_valid(&l)?l:dove_layout_legacy();
}
static inline int dove_layout_save(const DoveLayouts *l){
 if(!dove_layout_valid(l))return -1;
 char path[200];snprintf(path,sizeof path,"%s.%ld.new",DOVE_LAYOUT_FILE,(long)getpid());FILE *f=fopen(path,"w");if(!f)return -1;
 int bad=fprintf(f,"DOVE_LAYOUTS1\n")<0;for(int i=0;i<l->count;i++)bad|=fprintf(f,"%s\n",l->ids[i])<0;
 bad|=fflush(f)!=0;bad|=fsync(fileno(f))!=0;bad|=fclose(f)!=0;
 if(!bad&&!rename(path,DOVE_LAYOUT_FILE))return 0;
 unlink(path);return -1;
}
#endif
