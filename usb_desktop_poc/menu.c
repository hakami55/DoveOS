#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "font.h"
#include "file_types.h"
#ifdef DOVEOS_XWINDOW
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "moonlight_icon.h"
struct dove_text_command {int x,y,scale;uint32_t color;char value[161];};
static struct dove_text_command dove_text[160];
static int dove_text_count;
#endif

#ifndef USB_ROOT
#define USB_ROOT "/media/usb0"
#endif
#ifndef MENU_DEFAULT_SECONDS
#define MENU_DEFAULT_SECONDS 3600
#endif
#define MENU_STOP_PATH "/tmp/drmosa-usb-menu.stop"

#define WIDTH 1280
#define HEIGHT 720
#define MAX_FILES 1024
#define MAX_NOTES 8192
#define MAX_VIEW 16384
#define FG 0x00e8f3f2u
#define MUTED 0x0099b5b8u
#define BG 0x000d1820u
#define CARD 0x001a3038u
#define ACTIVE 0x00215b65u
#define CYAN 0x0056d6d5u
#define GREEN 0x0079df9bu
#define GOLD 0x00efc873u
#define ORANGE 0x00f2a66eu

enum page { HOME, FILES, NOTES, VIEWER, SETTINGS, NETWORK, BLUETOOTH, DISPLAY, SOUND, MOONLIGHT, WIFI };
typedef struct {
    uint8_t *data; uint32_t physical, bytes, width, height, stride, aligned_height;
    uint32_t magic1, ion_handle, magic2, allocated, magic3, magic4;
} vo_buffer;
_Static_assert(sizeof(vo_buffer)==52,"Vendor VOUT ABI");
#ifndef DOVEOS_XWINDOW
static void *vo_library;
static vo_buffer vo_frame;
static int (*vo_render)(vo_buffer*),(*vo_stop)(void),(*vo_deinit)(void),(*vo_free)(vo_buffer*);
static int vo_ready;
#endif
struct app;
static void present(struct app *a);
struct entry { char name[256]; int directory; };
struct ml_app { int id; char name[60]; };
struct bt_device { char address[18], name[64]; int le; };
struct app {
    uint32_t *pixels, *saved;
    size_t fb_size;
    int stride, kb, tp, cursor_x, cursor_y, page, previous_page, home_selected, settings_selected, network_selected, display_level, sound_level, launcher_mode;
    int shift, caps, ctrl, touching, moved, last_x, last_y, have_x, have_y;
    struct timespec touched;
    char root[512], cwd[1024], launcher_root[1024], status[160], view_title[256];
    struct entry files[MAX_FILES]; int file_count, selected, file_top;
    char notes[MAX_NOTES], view[MAX_VIEW];
    size_t notes_len, note_cursor; int dirty, note_top, launch_moonlight, launch_hdmi, launch_media, launch_game;
    char media_path[1400], game_path[1400];
    char hosts[5][40], host_input[40], ml_pin[5], ml_feedback[160];
    char ml_host_name[64];
    char wifi_names[16][33], wifi_password[64], wifi_state[160];
    int wifi_count, wifi_selected, wifi_edit;
    struct bt_device bt_devices[16];
    int bt_count, bt_selected;
    char bt_state[160];
    pid_t bt_child;
    int bt_scan_pending, bt_start_pending;
    int host_count, host_selected, ml_selected, ml_edit, ml_paired;
    int ml_resolution, ml_bitrate, ml_volume, ml_pointer, ml_codec, ml_settings_open, ml_apps_open;
    struct ml_app apps[16]; int app_count, app_selected, ml_request;
    pid_t ml_child;
};
static volatile sig_atomic_t quit;
#ifdef DOVEOS_XWINDOW
#define SETTINGS_HELPER "/usr/bin/doveos-settings-proxy"
#else
#define SETTINGS_HELPER "/tmp/drmosa-usb-menu-runtime/settings.sh"
#endif
static void on_signal(int signum) { (void)signum; quit=1; }
static void message(struct app *a, const char *value) {
    snprintf(a->status,sizeof a->status,"%s",value);
}
static uint32_t *pixel(struct app *a,int x,int y) {
    return a->pixels+(size_t)y*a->stride+x;
}
static void rect(struct app *a,int x,int y,int w,int h,uint32_t color) {
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
    if(x+w>WIDTH)w=WIDTH-x;
    if(y+h>HEIGHT)h=HEIGHT-y;
    if(w<=0||h<=0)return;
    for(int row=0;row<h;row++) {
        uint32_t *line=pixel(a,x,y+row);
        for(int col=0;col<w;col++)line[col]=color;
    }
}
#ifdef DOVEOS_XWINDOW
static void moon_icon(struct app *a,const unsigned char *rgb,int size,int x,int y){
    for(int row=0;row<size;row++)for(int col=0;col<size;col++){
        const unsigned char *p=rgb+(row*size+col)*3;
        rect(a,x+col,y+row,1,1,((uint32_t)p[0]<<16)|((uint32_t)p[1]<<8)|p[2]);
    }
}
static void moon_icon_2x(struct app *a,const unsigned char *rgb,int size,int x,int y){
    for(int row=0;row<size;row++)for(int col=0;col<size;col++){
        const unsigned char *p=rgb+(row*size+col)*3;
        rect(a,x+col*2,y+row*2,2,2,((uint32_t)p[0]<<16)|((uint32_t)p[1]<<8)|p[2]);
    }
}
static void moon_art(struct app *a,const unsigned char *rgb,int x,int y){
    for(int row=0;row<256;row++)for(int col=0;col<192;col++){
        const unsigned char *p=rgb+(row*192+col)*3;
        *pixel(a,x+col,y+row)=((uint32_t)p[0]<<16)|((uint32_t)p[1]<<8)|p[2];
    }
}
#endif
static void glyph(struct app *a,int x,int y,unsigned char code,uint32_t color,int scale) {
    if(code>=128)code='?';
    for(int row=0;row<16;row++) {
        unsigned char bits=font8x16[code][row];
        for(int col=0;col<8;col++)if(bits&(0x80u>>col))
            rect(a,x+col*scale,y+row*scale,scale,scale,color);
    }
}
static void label(struct app *a,int x,int y,const char *value,uint32_t color,int scale,int max_chars) {
#ifdef DOVEOS_XWINDOW
    (void)a;
    if(dove_text_count>=160)return;
    struct dove_text_command *cmd=&dove_text[dove_text_count++];
    cmd->x=x;cmd->y=y;cmd->color=color;cmd->scale=scale;
    int i=0;while(value[i]&&value[i]!='\n'&&value[i]!='\r'&&i<max_chars&&i<160){cmd->value[i]=value[i];i++;}
    cmd->value[i]=0;
#else
    for(int i=0;value[i]&&i<max_chars;i++) {
        unsigned char c=(unsigned char)value[i];
        if(c=='\n'||c=='\r')break;
        glyph(a,x+i*8*scale,y,c,color,scale);
    }
#endif
}
static void button(struct app *a,int x,int y,int w,const char *title,int selected) {
    rect(a,x,y,w,48,selected?ACTIVE:CARD);
    label(a,x+16,y+8,title,selected?FG:MUTED,2,20);
}
static int file_compare(const void *one,const void *two) {
    const struct entry *a=one,*b=two;
    if(a->directory!=b->directory)return b->directory-a->directory;
    return strcasecmp(a->name,b->name);
}
static void list_files(struct app *a) {
    a->file_count=0; a->selected=0; a->file_top=0;
    DIR *dir=opendir(a->cwd);
    if(!dir){message(a,"USB files unavailable. Keep the drive inserted.");return;}
    struct dirent *item;
    while((item=readdir(dir))&&a->file_count<MAX_FILES) {
        if(item->d_name[0]=='.')continue;
        struct entry *target=&a->files[a->file_count];
        snprintf(target->name,sizeof target->name,"%s",item->d_name);
        char path[1400];
        if(snprintf(path,sizeof path,"%s/%s",a->cwd,target->name)>=(int)sizeof path)continue;
        struct stat st;
        if(lstat(path,&st)||S_ISLNK(st.st_mode))continue;
        target->directory=S_ISDIR(st.st_mode);
        a->file_count++;
    }
    closedir(dir);
    qsort(a->files,(size_t)a->file_count,sizeof a->files[0],file_compare);
    message(a,a->launcher_mode?"Select a system, then a ROM. F10 returns to home.":"Colored files open. White files are unsupported. Esc goes back.");
}
static int note_path(struct app *a,char *out,size_t capacity) {
    return snprintf(out,capacity,"%s/DrMoSa/notes.txt",a->root)<(int)capacity;
}
static void load_notes(struct app *a) {
    char path[1024];
    if(!note_path(a,path,sizeof path))return;
    FILE *file=fopen(path,"rb");
    if(!file){
        int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
        if(fd>=0)close(fd);
        a->notes[0]=0;a->notes_len=0;a->note_cursor=0;return;
    }
    a->notes_len=fread(a->notes,1,MAX_NOTES-1,file);
    fclose(file); a->notes[a->notes_len]=0; a->note_cursor=a->notes_len;
}
static int save_notes(struct app *a) {
    char path[1024],temp[1040];
    if(!note_path(a,path,sizeof path)||snprintf(temp,sizeof temp,"%s.tmp",path)>=(int)sizeof temp)return -1;
    int fd=open(temp,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
    if(fd<0)return -1;
    size_t done=0;
    while(done<a->notes_len) {
        ssize_t n=write(fd,a->notes+done,a->notes_len-done);
        if(n<1){close(fd);unlink(temp);return -1;}done+=(size_t)n;
    }
    int synced=fsync(fd);
    int closed=close(fd);
    if(synced||closed||rename(temp,path)){unlink(temp);return -1;}
    a->dirty=0; message(a,"Notes saved on USB.");return 0;
}
static void open_entry(struct app *a,int selected) {
    if(selected<0||selected>=a->file_count)return;
    struct entry *entry=&a->files[selected];
    char path[1400];
    if(snprintf(path,sizeof path,"%s/%s",a->cwd,entry->name)>=(int)sizeof path)return;
    if(entry->directory) {
        if(strlen(path)>=sizeof a->cwd)return;
        strcpy(a->cwd,path);list_files(a);return;
    }
    if(a->launcher_mode){
        const char *dot=strrchr(entry->name,'.');
        size_t base=strlen(a->launcher_root);
        if(!strncmp(a->cwd,a->launcher_root,base)&&!strncmp(a->cwd+base,"/NES",4)&&
           (a->cwd[base+4]=='/'||a->cwd[base+4]==0)&&dot&&!strcasecmp(dot,".nes")){
            snprintf(a->game_path,sizeof a->game_path,"%s",path);
            a->launch_game=2;quit=1;return;
        }
        message(a,"This system's emulator core is pending. NES .nes files run now.");
        return;
    }
    enum dove_file_type kind=dove_file_type(entry->name);
    if(kind==DOVE_PICTURE||kind==DOVE_AUDIO||kind==DOVE_VIDEO){
        if(a->dirty&&save_notes(a)){message(a,"Save failed; media was not launched.");return;}
        snprintf(a->media_path,sizeof a->media_path,"%s",path);
        a->launch_media=1;quit=1;return;
    }
    if(kind!=DOVE_TEXT) {message(a,"This white file type cannot be opened.");return;}
    char notes_dir[1024];
    if(snprintf(notes_dir,sizeof notes_dir,"%s/DrMoSa",a->root)<(int)sizeof notes_dir&&
       !strcmp(a->cwd,notes_dir)&&!strcasecmp(entry->name,"notes.txt")){
        load_notes(a);a->page=NOTES;a->note_top=0;return;
    }
    FILE *file=fopen(path,"rb");
    if(!file){message(a,"Unable to open text file.");return;}
    size_t n=fread(a->view,1,MAX_VIEW-1,file);fclose(file);
    if(memchr(a->view,0,n)){message(a,"Binary file cannot be previewed.");return;}
    a->view[n]=0;snprintf(a->view_title,sizeof a->view_title,"%s",entry->name);
    a->page=VIEWER;a->note_top=0;
}
static void parent_folder(struct app *a) {
    const char *base=a->launcher_mode?a->launcher_root:a->root;
    if(!strcmp(a->cwd,base))return;
    char *last=strrchr(a->cwd,'/');
    if(last&&last>a->cwd)*last=0;
    if(strlen(a->cwd)<strlen(base))strcpy(a->cwd,base);
    list_files(a);
}
static void note_insert(struct app *a,char c) {
    if(a->notes_len>=MAX_NOTES-1){message(a,"Notes limit reached (8 KiB).");return;}
    memmove(a->notes+a->note_cursor+1,a->notes+a->note_cursor,a->notes_len-a->note_cursor+1);
    a->notes[a->note_cursor++]=c;a->notes_len++;a->dirty=1;
}
static int key_character(unsigned short code,int shift,int caps) {
    static const char base[54]={0,0,'1','2','3','4','5','6','7','8','9','0','-','=',0,0,
        'q','w','e','r','t','y','u','i','o','p','[',']',0,0,'a','s','d','f','g','h','j','k','l',';',
        '\'', '`',0,'\\','z','x','c','v','b','n','m',',','.','/'};
    static const char upper[54]={0,0,'!','@','#','$','%','^','&','*','(',')','_','+',0,0,
        'Q','W','E','R','T','Y','U','I','O','P','{','}',0,0,'A','S','D','F','G','H','J','K','L',':',
        '"','~',0,'|','Z','X','C','V','B','N','M','<','>','?'};
    if(code==KEY_SPACE)return ' ';
    if(code>=54)return 0;
    int c=base[code];
    if(isalpha((unsigned char)c))return shift^caps?toupper(c):c;
    return shift?upper[code]:c;
}
static int setting_is(const char *path,const char *expected) {
    char value[48]={0};FILE *f=fopen(path,"r");
    if(!f)return 0;
    int ok=fgets(value,sizeof value,f)!=NULL;fclose(f);
    if(!ok)return 0;
    value[strcspn(value,"\r\n")]=0;
    return !strcmp(value,expected);
}
static int saved_percent(struct app *a,const char *name,int fallback) {
    char path[1024];int value=fallback;
    if(snprintf(path,sizeof path,"%s/DrMoSa/%s",a->root,name)>=(int)sizeof path)return fallback;
    FILE *f=fopen(path,"r");
    if(f){if(fscanf(f,"%d",&value)!=1)value=fallback;fclose(f);}
    return value>=0&&value<=100?value:fallback;
}
static void save_percent(struct app *a,const char *name,int value){
    char path[1024];
    if(snprintf(path,sizeof path,"%s/DrMoSa/%s",a->root,name)>=(int)sizeof path)return;
    FILE *f=fopen(path,"w");
    if(f){fprintf(f,"%d\n",value);fclose(f);}
}
struct network_info {int ethernet, wifi, mhz, bars;char ssid[33], ip[32];};
static struct network_info network_info(void) {
    struct network_info n={.bars=-1};
    n.ethernet=setting_is("/sys/class/net/axdebug0/carrier","1")&&
        access("/media/usb0/DRMOSA_DISABLE_ETHERNET",F_OK)!=0;
    n.wifi=setting_is("/sys/class/net/wlan0/carrier","1")&&
        access("/tmp/doveos-wifi/lease",R_OK)==0;
    if(!n.wifi)return n;
    FILE *f=fopen("/tmp/doveos-wifi/link-status","r");
    if(f){char line[96]={0};if(fgets(line,sizeof line,f)){
        char *band=NULL;
        for(char *p=line;(p=strstr(p," at "))!=NULL;p++)band=p;
        if(band){*band=0;snprintf(n.ssid,sizeof n.ssid,"%.32s",line);sscanf(band+4,"%d",&n.mhz);}
    }fclose(f);}
    f=fopen("/tmp/doveos-wifi/lease","r");
    if(f){char line[64]={0};if(fgets(line,sizeof line,f)){
        line[strcspn(line,"/\r\n")]=0;snprintf(n.ip,sizeof n.ip,"%.31s",line);
    }fclose(f);}
    f=fopen("/proc/net/wireless","r");
    if(f){char line[256];while(fgets(line,sizeof line,f)){
        char *p=strstr(line,"wlan0:");unsigned status=0;int quality=0;float level=0;
        if(p&&sscanf(p+6,"%x %d. %f.",&status,&quality,&level)==3){
            (void)status;(void)quality;
            if(level<=-25&&level>=-100)n.bars=level>=-55?4:level>=-67?3:level>=-78?2:1;
            break;
        }
    }fclose(f);}
    if(n.bars<0){
        f=fopen("/tmp/doveos-wifi/signal","r");
        if(f){int level=0;if(fscanf(f,"%d",&level)==1&&level<=-1&&level>=-100)
            n.bars=level>=-55?4:level>=-67?3:level>=-78?2:1;
        fclose(f);}
    }
    return n;
}
static void signal_bars(struct app *a,int x,int y,int bars){
    for(int i=0;i<4;i++){
        int h=6+i*5;
        rect(a,x+i*13,y+22-h,9,h,MUTED);
        if(bars>i)rect(a,x+i*13+2,y+24-h,5,h-4,CYAN);
    }
    if(bars<0)label(a,x+53,y+5,"?",MUTED,1,1);
}
static int xbox_running(void) {
    FILE *f=fopen("/tmp/drmosa-xow/pid","r");long pid=0;
    if(!f)return 0;
    int ok=fscanf(f,"%ld",&pid)==1;fclose(f);
    return ok&&pid>1&&pid<1000000&&kill((pid_t)pid,0)==0;
}
static int wifi_command(struct app *a,const char *action,const char *ssid,const char *secret) {
    int input[2]={-1,-1};
    if(secret&&pipe(input)){message(a,"Could not pass Wi-Fi password.");return -1;}
    pid_t pid=fork();
    if(pid<0){if(input[0]>=0){close(input[0]);close(input[1]);}message(a,"Could not start Wi-Fi action.");return -1;}
    if(pid==0){
        if(secret){close(input[1]);dup2(input[0],STDIN_FILENO);close(input[0]);}
        execl("/bin/sh","sh",SETTINGS_HELPER,action,ssid?ssid:"",(char*)NULL);
        _exit(127);
    }
    if(secret){
        close(input[0]);
        size_t n=strlen(secret),done=0;
        while(done<n){ssize_t sent=write(input[1],secret+done,n-done);if(sent<=0)break;done+=(size_t)sent;}
        if(done==n){ssize_t sent=write(input[1],"\n",1);(void)sent;}
        close(input[1]);
    }
    int status=0;
    if(waitpid(pid,&status,0)<0){message(a,"Wi-Fi action failed.");return -1;}
    FILE *f=fopen("/tmp/drmosa-settings-result","r");
    if(f){if(fgets(a->wifi_state,sizeof a->wifi_state,f))a->wifi_state[strcspn(a->wifi_state,"\r\n")]=0;fclose(f);}
    else snprintf(a->wifi_state,sizeof a->wifi_state,"Wi-Fi action failed; check wifi.log.");
    message(a,a->wifi_state);
    return WIFEXITED(status)?WEXITSTATUS(status):-1;
}
static void wifi_scan(struct app *a) {
    a->wifi_count=0;a->wifi_selected=0;a->wifi_edit=0;
    wifi_command(a,"wifi-scan",NULL,NULL);
    FILE *f=fopen("/tmp/doveos-wifi/scan","r");
    if(!f)return;
    char line[80];
    while(a->wifi_count<16&&fgets(line,sizeof line,f)){
        line[strcspn(line,"\r\n")]=0;
        size_t n=strlen(line);
        if(n>0&&n<=32){memcpy(a->wifi_names[a->wifi_count],line,n+1);a->wifi_count++;}
    }
    fclose(f);
}
static void bluetooth_load_scan(struct app *a,const char *path,int le) {
    FILE *f=fopen(path,"r");
    if(!f)return;
    char line[192];
    while(a->bt_count<16&&fgets(line,sizeof line,f)){
        line[strcspn(line,"\r\n")]=0;
        char *start=line;
        while(*start==' '||*start=='\t')start++;
        char address[18];
        if(strlen(start)<17)continue;
        memcpy(address,start,17);address[17]=0;
        int valid=1;
        for(int i=0;i<17;i++){
            if(i%3==2){if(address[i]!=':')valid=0;}
            else if(!isxdigit((unsigned char)address[i]))valid=0;
        }
        if(!valid)continue;
        int duplicate=0;
        for(int i=0;i<a->bt_count;i++)if(!strcasecmp(a->bt_devices[i].address,address))duplicate=1;
        if(duplicate)continue;
        const char *name=start+17;
        while(*name==' '||*name=='\t')name++;
        struct bt_device *device=&a->bt_devices[a->bt_count++];
        snprintf(device->address,sizeof device->address,"%s",address);
        snprintf(device->name,sizeof device->name,"%s",*name?name:"Unnamed device");
        device->le=le;
    }
    fclose(f);
}
static void bluetooth_action(struct app *a,const char *action){
    if(a->bt_child>0){message(a,"Bluetooth is busy. Wait for the current action to finish.");return;}
    const char *address=a->bt_count?a->bt_devices[a->bt_selected].address:NULL;
    if(address==NULL&&strcmp(action,"bluetooth-start")&&strcmp(action,"bluetooth-scan")){
        message(a,"Scan and select a Bluetooth device first.");return;
    }
    const char *command=action;
    if(!strcmp(action,"bluetooth-pair"))
        command=a->bt_devices[a->bt_selected].le?"bluetooth-pair-le":"bluetooth-pair-classic";
    pid_t pid=fork();
    if(pid<0){message(a,"Could not start Bluetooth action.");return;}
    if(pid==0){
        execl("/bin/sh","sh",SETTINGS_HELPER,command,address?address:"",(char*)NULL);
        _exit(127);
    }
    a->bt_child=pid;
    a->bt_scan_pending=!strcmp(action,"bluetooth-scan");
    a->bt_start_pending=!strcmp(action,"bluetooth-start");
    snprintf(a->bt_state,sizeof a->bt_state,"%s",a->bt_scan_pending?
        "Scanning nearby devices; this takes about 20 seconds...":
        !strcmp(action,"bluetooth-start")?"Starting Bluetooth...":"Working on selected device...");
}
static int bluetooth_poll(struct app *a){
    if(a->bt_child<=0)return 0;
    int status=0;
    pid_t done=waitpid(a->bt_child,&status,WNOHANG);
    if(done==0)return 0;
    a->bt_child=0;
    FILE *f=fopen("/tmp/doveos-bluetooth/result","r");
    if(f){
        if(fgets(a->bt_state,sizeof a->bt_state,f))a->bt_state[strcspn(a->bt_state,"\r\n")]=0;
        fclose(f);
    } else snprintf(a->bt_state,sizeof a->bt_state,"Bluetooth action failed; check USB and radio.");
    if(a->bt_scan_pending){
        a->bt_count=0;a->bt_selected=0;
        bluetooth_load_scan(a,"/tmp/doveos-bluetooth/le-scan.log",1);
        bluetooth_load_scan(a,"/tmp/doveos-bluetooth/classic-scan.log",0);
        if(!a->bt_count&&!strncmp(a->bt_state,"Scan finished",13))
            snprintf(a->bt_state,sizeof a->bt_state,
                "No devices found. Put one in pairing mode and press F8 again.");
        a->bt_scan_pending=0;
    }
    if(a->bt_start_pending){
        a->bt_start_pending=0;
        if(a->page==BLUETOOTH&&!strncmp(a->bt_state,"Bluetooth ready.",16)){
            bluetooth_action(a,"bluetooth-scan");
            return 1;
        }
    }
    message(a,a->bt_state);
    (void)status;
    return 1;
}
static void setting_action(struct app *a,int option) {
    static const char *actions[]={NULL,NULL,NULL,NULL,"awake-toggle","volume-next","xbox-toggle"};
    if(option==7){a->page=HOME;return;}
    if(option<0||option>=7)return;
    if(option==0){a->page=NETWORK;a->network_selected=0;return;}
    if(option==1){a->page=BLUETOOTH;if(a->pixels)bluetooth_action(a,"bluetooth-start");return;}
    if(option==2){a->display_level=saved_percent(a,"brightness.txt",100);a->page=DISPLAY;return;}
    if(option==3){a->sound_level=saved_percent(a,"sound-volume.txt",80);a->page=SOUND;return;}
    pid_t pid=fork();
    if(pid<0){message(a,"Could not start Settings action.");return;}
    if(pid==0){
        execl("/bin/sh","sh",SETTINGS_HELPER,actions[option],(char*)NULL);
        _exit(127);
    }
    int status=0;
    if(waitpid(pid,&status,0)<0){message(a,"Settings action failed.");return;}
    FILE *f=fopen("/tmp/drmosa-settings-result","r");
    if(f){if(fgets(a->status,sizeof a->status,f))a->status[strcspn(a->status,"\r\n")]=0;fclose(f);}
    else message(a,"Settings action finished; check dock log.");
    if(option==5){
        static const int levels[]={0,25,50,70,80,90,100};
        char path[1024];snprintf(path,sizeof path,"%s/DrMoSa/volume.txt",a->root);
        f=fopen(path,"r");
        if(f){
            int value;
            if(fscanf(f,"%d",&value)==1){
                for(int i=0;i<7;i++)if(value==levels[i])a->ml_volume=i;
            }
            fclose(f);
        }
    }
}
static void percent_action(struct app *a,int display,int delta){
    if(display){message(a,"Use the dock brightness down/up keys.");return;}
    int *level=display?&a->display_level:&a->sound_level;
    int next=*level+delta;
    if(next<0)next=0;
    if(next>100)next=100;
    if(next==*level)return;
    char value[8];snprintf(value,sizeof value,"%d",next);
    if(wifi_command(a,display?"brightness-set":"sound-set",value,NULL)==0)*level=next;
}
static void network_action(struct app *a,int option){
    if(option==0){wifi_command(a,"wifi-status",NULL,NULL);return;}
    if(option==1){wifi_command(a,"ethernet-toggle",NULL,NULL);return;}
    if(option==2){a->page=WIFI;wifi_scan(a);return;}
    if(option==3){wifi_command(a,"wifi-reconnect",NULL,NULL);return;}
    if(option==4){wifi_command(a,"wifi-disconnect",NULL,NULL);return;}
    if(option==5)a->page=SETTINGS;
}
static int valid_host(const char *value){struct in_addr ip;return inet_pton(AF_INET,value,&ip)==1;}
static int sunshine_reachable(const char *host){
    struct sockaddr_in address={0};address.sin_family=AF_INET;
    address.sin_port=htons(47989);
    if(inet_pton(AF_INET,host,&address.sin_addr)!=1)return 0;
    int fd=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
    if(fd<0)return 0;
    int rc=connect(fd,(struct sockaddr *)&address,sizeof address);
    if(rc<0&&errno==EINPROGRESS){
        struct pollfd item={.fd=fd,.events=POLLOUT};
        if(poll(&item,1,700)>0){int error=0;socklen_t size=sizeof error;
            rc=getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)==0&&!error?0:-1;
        }
    }
    close(fd);return rc==0;
}
static void load_moonlight(struct app *a){
    a->host_count=1;
    strcpy(a->hosts[0],"169.254.244.136");
    char path[1024];
    snprintf(path,sizeof path,"%s/DrMoSa/moonlight-hosts.txt",a->root);
    FILE *f=fopen(path,"r");
    if(f){char line[80];while(fgets(line,sizeof line,f)&&a->host_count<5){
        line[strcspn(line,"\r\n")]=0;
        if(!valid_host(line))continue;
        int exists=0;for(int i=0;i<a->host_count;i++)if(!strcmp(a->hosts[i],line))exists=1;
        if(!exists){size_t len=strlen(line);memcpy(a->hosts[a->host_count],line,len+1);a->host_count++;}
    }fclose(f);}
    a->ml_resolution=0;a->ml_bitrate=2;a->ml_volume=3;a->ml_pointer=1;a->ml_paired=-1;
    a->app_count=1;a->apps[0].id=881448767;strcpy(a->apps[0].name,"Desktop");
    snprintf(path,sizeof path,"%s/DrMoSa/moonlight-settings.txt",a->root);
    f=fopen(path,"r");
    if(f){int r,b,v,p,c=0;if(fscanf(f,"%d %d %d %d",&r,&b,&v,&p)==4&&
        r>=0&&r<3&&b>=0&&b<4&&v>=0&&v<7&&p>=0&&p<4){
        a->ml_resolution=r;a->ml_bitrate=b;a->ml_volume=v;a->ml_pointer=p;
        if(fscanf(f,"%d",&c)==1&&c>=0&&c<=1)a->ml_codec=c;
    }fclose(f);}
    static const int levels[]={0,25,50,70,80,90,100};
    snprintf(path,sizeof path,"%s/DrMoSa/volume.txt",a->root);
    f=fopen(path,"r");
    if(f){
        int value;
        if(fscanf(f,"%d",&value)==1){
            for(int i=0;i<7;i++)if(value==levels[i])a->ml_volume=i;
        }
        fclose(f);
    }
    snprintf(a->ml_feedback,sizeof a->ml_feedback,"Select a PC, then Refresh or Start desktop.");
}
static int save_hosts(struct app *a){
    char path[1024];snprintf(path,sizeof path,"%s/DrMoSa/moonlight-hosts.txt",a->root);
    FILE *f=fopen(path,"w");if(!f)return -1;
    for(int i=1;i<a->host_count;i++)fprintf(f,"%s\n",a->hosts[i]);
    int bad=fflush(f)||fsync(fileno(f));if(fclose(f))bad=1;
    return bad?-1:0;
}
static void save_moonlight_settings(struct app *a){
    static const int levels[]={0,25,50,70,80,90,100};
    char path[1024];snprintf(path,sizeof path,"%s/DrMoSa/moonlight-settings.txt",a->root);
    FILE *f=fopen(path,"w");if(!f){message(a,"Could not save Moonlight settings to USB.");return;}
    fprintf(f,"%d %d %d %d %d\n",a->ml_resolution,a->ml_bitrate,a->ml_volume,a->ml_pointer,a->ml_codec);
    int bad=fflush(f)||fsync(fileno(f));if(fclose(f))bad=1;
    snprintf(path,sizeof path,"%s/DrMoSa/volume.txt",a->root);
    f=fopen(path,"w");
    if(!f)bad=1;
    else {fprintf(f,"%d\n",levels[a->ml_volume]);
        if(fflush(f)||fsync(fileno(f)))bad=1;
        if(fclose(f))bad=1;}
    if(bad)message(a,"Could not save Moonlight settings to USB.");
}
static void moonlight_probe(struct app *a,int request){
    if(a->ml_child>0){message(a,"A Moonlight request is still running.");return;}
    unlink("/tmp/drmosa-moonlight-control.log");a->ml_pin[0]=0;a->ml_paired=-1;
    if(request==0){a->app_count=1;a->app_selected=0;a->apps[0].id=881448767;strcpy(a->apps[0].name,"Desktop");}
    pid_t pid=fork();if(pid<0){message(a,"Could not start Moonlight request.");return;}
    if(pid==0){
        alarm(request==1?180:request==2?8:20);
        int fd=open("/tmp/drmosa-moonlight-control.log",O_WRONLY|O_CREAT|O_TRUNC,0600);
        if(fd<0)_exit(127);
        dup2(fd,1);dup2(fd,2);close(fd);
        if(request==2)
            execl("/tmp/drmosa-usb-menu-runtime/lib/ld-linux-armhf.so.3","ld-linux-armhf.so.3",
                "--library-path","/tmp/drmosa-usb-menu-runtime/lib",
                "/tmp/drmosa-usb-menu-runtime/moonlight-discover",(char*)NULL);
        else
            execl("/tmp/drmosa-usb-menu-runtime/lib/ld-linux-armhf.so.3","ld-linux-armhf.so.3",
                "--library-path","/tmp/drmosa-usb-menu-runtime/lib",
                request==1?"/tmp/drmosa-usb-menu-runtime/moonlight-pair":"/tmp/drmosa-usb-menu-runtime/moonlight-check",
                a->hosts[a->host_selected],"/tmp/drmosa-usb-menu-runtime/keys",(char*)NULL);
        _exit(127);
    }
    a->ml_child=pid;a->ml_request=request;
    snprintf(a->ml_feedback,sizeof a->ml_feedback,request==1?"Pairing... enter the PIN shown below in Sunshine.":
        request==2?"Searching for a PC on this network...":"Checking selected PC...");
}
static void moonlight_poll(struct app *a){
    if(a->ml_child<=0)return;
    FILE *f=fopen("/tmp/drmosa-moonlight-control.log","r");
    if(f){char line[256];while(fgets(line,sizeof line,f)){
        const char *host_name=strstr(line,"<hostname>");
        if(host_name){
            host_name+=10;const char *end=strstr(host_name,"</hostname>");
            if(end&&end>host_name){size_t n=(size_t)(end-host_name);
                if(n>=sizeof a->ml_host_name)n=sizeof a->ml_host_name-1;
                memcpy(a->ml_host_name,host_name,n);a->ml_host_name[n]=0;}
        }
        if(!strncmp(line,"MOONLIGHT_PIN=",14)&&strlen(line)>=18){memcpy(a->ml_pin,line+14,4);a->ml_pin[4]=0;}
        if(!strncmp(line,"PAIRED=",7))a->ml_paired=line[7]=='1';
        if(strstr(line,"MOONLIGHT_PAIRED=1"))a->ml_paired=1;
        if(!strncmp(line,"APP=",4)){
            char *name=strchr(line+4,':');
            if(name&&a->app_count<16){*name++=0;char *end;long id=strtol(line+4,&end,10);
                int exists=0;for(int i=0;i<a->app_count;i++)if(a->apps[i].id==id)exists=1;
                if(!*end&&id>0&&id<2147483647&&!exists){
                    if(a->app_count==1&&a->apps[0].id==881448767)a->app_count=0;
                    a->apps[a->app_count].id=(int)id;
                    name[strcspn(name,"\r\n")]=0;
                    snprintf(a->apps[a->app_count].name,sizeof a->apps[0].name,"%.59s",name);
                    a->app_count++;
                }
            }
        }
        if(!strncmp(line,"FOUND_PC=",9)){
            char *host=line+9;host[strcspn(host,"\r\n")]=0;
            if(valid_host(host)){
                int index=-1;for(int i=0;i<a->host_count;i++)if(!strcmp(a->hosts[i],host))index=i;
                if(index<0&&a->host_count<5){index=a->host_count++;
                    snprintf(a->hosts[index],sizeof a->hosts[0],"%.39s",host);save_hosts(a);}
                if(index>=0){a->host_selected=index;a->ml_paired=-1;}
            }
        }
    }fclose(f);}
    int status=0;pid_t done=waitpid(a->ml_child,&status,WNOHANG);
    if(done==a->ml_child){
        a->ml_child=0;
        a->ml_pin[0]=0;
        snprintf(a->ml_feedback,sizeof a->ml_feedback,
            a->ml_request==2?(WIFEXITED(status)&&WEXITSTATUS(status)==0?"PC found. Refresh its pairing status.":"No PC found; use Add PC address."):
            a->ml_paired==1?"PC paired and ready.":
            a->ml_paired==0?"PC found but not paired. Choose Pair this PC.":
            "PC check/pair failed. Check network and Sunshine.");
    }
}
static void moonlight_action(struct app *a){
    static const int bitrate[]={3000,5000,8000,10000};
    static const int volume[]={0,25,50,70,80,90,100};
    static const int pointer[]={40,60,80,100};
    switch(a->ml_selected){
    case 0:a->host_selected=(a->host_selected+1)%a->host_count;a->ml_paired=-1;a->ml_host_name[0]=0;
        a->app_count=1;a->app_selected=0;a->apps[0].id=881448767;strcpy(a->apps[0].name,"Desktop");break;
    case 1:a->ml_edit=1;a->host_input[0]=0;snprintf(a->ml_feedback,sizeof a->ml_feedback,"Type the PC IPv4 address, then Enter.");break;
    case 2:moonlight_probe(a,2);break;
    case 3:moonlight_probe(a,0);break;
    case 4:moonlight_probe(a,1);break;
    case 5:a->app_selected=(a->app_selected+1)%a->app_count;break;
    case 6:{
        if(a->ml_child>0){message(a,"Wait for the pairing check to finish.");break;}
        if(a->ml_paired!=1){message(a,"Check this PC and pair it before starting a stream.");break;}
        if(!sunshine_reachable(a->hosts[a->host_selected])){
            snprintf(a->ml_feedback,sizeof a->ml_feedback,
                "PC %s is offline. Select another PC or Find PC on LAN.",
                a->hosts[a->host_selected]);
            break;
        }
        FILE *f=fopen("/tmp/drmosa-moonlight-session","w");
        if(!f){message(a,"Could not prepare Moonlight session.");break;}
        int width=a->ml_resolution==1?1280:1920,height=a->ml_resolution==1?720:1080;
        int fps=a->ml_resolution==2?30:60;
        fprintf(f,"%s %d %d %d %d %d %d %d %s\n",a->hosts[a->host_selected],width,height,fps,
            bitrate[a->ml_bitrate],volume[a->ml_volume],pointer[a->ml_pointer],a->apps[a->app_selected].id,
            a->ml_codec?"hevc":"h264");
        int bad=fflush(f)||fsync(fileno(f));if(fclose(f))bad=1;
        if(bad){message(a,"Could not save session settings.");break;}
        a->launch_moonlight=1;quit=1;break;
    }
    case 7:a->ml_resolution=(a->ml_resolution+1)%3;save_moonlight_settings(a);break;
    case 8:a->ml_bitrate=(a->ml_bitrate+1)%4;save_moonlight_settings(a);break;
    case 9:a->ml_volume=(a->ml_volume+1)%7;save_moonlight_settings(a);break;
    case 10:a->ml_pointer=(a->ml_pointer+1)%4;save_moonlight_settings(a);break;
    case 11:a->ml_codec=1-a->ml_codec;save_moonlight_settings(a);break;
    case 12:a->page=HOME;break;
    }
}
static void note_position(struct app *a,size_t index,int *row,int *col) {
    *row=0;*col=0;
    for(size_t i=0;i<index&&i<a->notes_len;i++) {
        if(a->notes[i]=='\n'){(*row)++;*col=0;}
        else if(++*col>=70){(*row)++;*col=0;}
    }
}
static size_t index_for_position(struct app *a,int wanted_row,int wanted_col) {
    int row=0,col=0;size_t closest=0;int best=100000;
    for(size_t i=0;i<=a->notes_len;i++) {
        if(row==wanted_row&&abs(col-wanted_col)<best){best=abs(col-wanted_col);closest=i;}
        if(i==a->notes_len)break;
        if(a->notes[i]=='\n'){row++;col=0;}
        else if(++col>=70){row++;col=0;}
    }
    return closest;
}
static void key_press(struct app *a,unsigned short code,int value) {
    if(code==KEY_LEFTSHIFT||code==KEY_RIGHTSHIFT){a->shift=value!=0;return;}
    if(code==KEY_LEFTCTRL||code==KEY_RIGHTCTRL){a->ctrl=value!=0;return;}
    if(!value)return;
    if(code==KEY_CAPSLOCK&&value==1){a->caps=!a->caps;return;}
    if(a->page==DISPLAY&&(code==KEY_BRIGHTNESSDOWN||code==KEY_BRIGHTNESSUP)){
        int next=a->display_level+(code==KEY_BRIGHTNESSUP?10:-10);
        if(next<0)next=0;
        if(next>100)next=100;
        a->display_level=next;save_percent(a,"brightness.txt",next);
        message(a,"Brightness changed with the dock hardware key.");return;
    }
    if(a->launcher_mode){
        if(code==KEY_F10){quit=1;return;}
        if(code==KEY_ESC||code==KEY_BACKSPACE){
            if(!strcmp(a->cwd,a->launcher_root))quit=1;
            else parent_folder(a);
            return;
        }
        int root=!strcmp(a->cwd,a->launcher_root);
        if(code==KEY_UP&&a->selected>=(root?2:1))a->selected-=root?2:1;
        if(code==KEY_DOWN&&a->selected+(root?2:1)<a->file_count)a->selected+=root?2:1;
        if(root&&code==KEY_LEFT&&a->selected%2)a->selected--;
        if(root&&code==KEY_RIGHT&&!(a->selected%2)&&a->selected+1<a->file_count)a->selected++;
        if(!root&&code==KEY_PAGEUP)a->selected=a->selected>10?a->selected-10:0;
        if(!root&&code==KEY_PAGEDOWN&&a->file_count)a->selected=a->selected+10<a->file_count?a->selected+10:a->file_count-1;
        if(code==KEY_ENTER)open_entry(a,a->selected);
        if(a->selected<a->file_top)a->file_top=a->selected;
        if(a->selected>=a->file_top+10)a->file_top=a->selected-9;
        return;
    }
    if(code==KEY_F3){
        if(a->dirty&&save_notes(a)){message(a,"Save Notes before Moonlight.");return;}
        a->page=MOONLIGHT;
        moonlight_probe(a,sunshine_reachable(a->hosts[a->host_selected])?0:2);
        return;
    }
    if(code==KEY_F7){
        if(a->dirty&&save_notes(a)){message(a,"Save Notes before Games.");return;}
        a->launch_game=1;quit=1;return;
    }
    if(code==KEY_F4){
        if(a->dirty&&save_notes(a)){message(a,"Save failed; HDMI In was not launched.");return;}
        a->launch_hdmi=1;quit=1;return;
    }
    if(code==KEY_F6){if(a->dirty&&save_notes(a)){message(a,"Save Notes before Settings.");return;}a->page=SETTINGS;return;}
    if(code==KEY_F10){
#ifdef DOVEOS_XWINDOW
        return;
#else
        if(a->dirty&&save_notes(a))message(a,"Save failed. Notes remain in memory.");else quit=1;
        return;
#endif
    }
    if(code==KEY_F1){if(a->dirty&&save_notes(a)){message(a,"Save failed; stay in Notes.");return;}a->page=FILES;return;}
    if(a->page==HOME){
        if(code==KEY_LEFT&&(a->home_selected&1)&&a->home_selected<4)a->home_selected--;
        if(code==KEY_RIGHT&&!(a->home_selected&1)&&a->home_selected<3)a->home_selected++;
        if(code==KEY_UP&&a->home_selected>=2)a->home_selected-=2;
        if(code==KEY_DOWN&&a->home_selected<=2)a->home_selected+=2;
        if(code==KEY_DOWN&&a->home_selected==3)a->home_selected=4;
        if(code==KEY_ENTER){
            static const unsigned short actions[]={KEY_F1,KEY_F3,KEY_F4,KEY_F6,KEY_F7};
            key_press(a,actions[a->home_selected],1);
        }
        return;
    }
    if(a->page==SETTINGS){
        if(code==KEY_UP&&a->settings_selected>0)a->settings_selected--;
        if(code==KEY_DOWN&&a->settings_selected<7)a->settings_selected++;
        if(code==KEY_ENTER)setting_action(a,a->settings_selected);
        if(code==KEY_ESC)a->page=HOME;
        return;
    }
    if(a->page==NETWORK){
        if(code==KEY_UP&&a->network_selected>0)a->network_selected--;
        if(code==KEY_DOWN&&a->network_selected<5)a->network_selected++;
        if(code==KEY_ENTER)network_action(a,a->network_selected);
        if(code==KEY_ESC)a->page=SETTINGS;
        return;
    }
    if(a->page==BLUETOOTH){
        if(code==KEY_ESC){a->page=SETTINGS;return;}
        if(code==KEY_UP&&a->bt_selected>0)a->bt_selected--;
        if(code==KEY_DOWN&&a->bt_selected<a->bt_count-1)a->bt_selected++;
        if(code==KEY_F8)bluetooth_action(a,"bluetooth-scan");
        if(code==KEY_ENTER)bluetooth_action(a,a->bt_count?"bluetooth-pair":"bluetooth-scan");
        if(code==KEY_F9)bluetooth_action(a,"bluetooth-connect");
        return;
    }
    if(a->page==DISPLAY){
        if(code==KEY_ESC)a->page=SETTINGS;
        else message(a,"Use the dock brightness down/up keys.");
        return;
    }
    if(a->page==SOUND){
        if(code==KEY_ESC){a->page=SETTINGS;return;}
        if(code==KEY_LEFT||code==KEY_DOWN)percent_action(a,0,-10);
        if(code==KEY_RIGHT||code==KEY_UP)percent_action(a,0,10);
        return;
    }
    if(a->page==WIFI){
        if(a->wifi_edit){
            size_t n=strlen(a->wifi_password);
            if(code==KEY_ESC){memset(a->wifi_password,0,sizeof a->wifi_password);a->wifi_edit=0;return;}
            if(code==KEY_BACKSPACE&&n){a->wifi_password[n-1]=0;return;}
            if(code==KEY_ENTER){
                if(n<8){message(a,"Password must be at least 8 characters.");return;}
                wifi_command(a,"wifi-join",a->wifi_names[a->wifi_selected],a->wifi_password);
                volatile char *p=a->wifi_password;
                for(size_t i=0;i<sizeof a->wifi_password;i++)p[i]=0;
                a->wifi_edit=0;return;
            }
            int c=key_character(code,a->shift,a->caps);
            if(c>=32&&c<=126&&n<63){a->wifi_password[n]=(char)c;a->wifi_password[n+1]=0;}
            return;
        }
        if(code==KEY_ESC){a->page=NETWORK;return;}
        if(code==KEY_UP&&a->wifi_selected>0)a->wifi_selected--;
        if(code==KEY_DOWN&&a->wifi_selected<a->wifi_count-1)a->wifi_selected++;
        if(code==KEY_ENTER&&a->wifi_count){a->wifi_edit=1;a->wifi_password[0]=0;message(a,"Type network password, then Enter.");}
        if(code==KEY_F8)wifi_scan(a);
        if(code==KEY_F9)wifi_command(a,"wifi-reconnect",NULL,NULL);
        if(code==KEY_DELETE)wifi_command(a,"wifi-disconnect",NULL,NULL);
        return;
    }
    if(a->page==MOONLIGHT){
        if(a->ml_edit){
            size_t len=strlen(a->host_input);
            if(code==KEY_ESC){a->ml_edit=0;return;}
            if(code==KEY_BACKSPACE&&len){a->host_input[len-1]=0;return;}
            if(code==KEY_ENTER){
                if(!valid_host(a->host_input)){message(a,"Enter a valid IPv4 address.");return;}
                for(int i=0;i<a->host_count;i++)if(!strcmp(a->hosts[i],a->host_input)){
                    a->host_selected=i;a->ml_edit=0;message(a,"PC already in list.");return;
                }
                if(a->host_count==5){message(a,"PC list is full (5 hosts).");return;}
                snprintf(a->hosts[a->host_count],sizeof a->hosts[0],"%s",a->host_input);
                a->host_selected=a->host_count++;a->ml_edit=0;a->ml_paired=-1;
                if(save_hosts(a))message(a,"Could not save PC list to USB.");
                else message(a,"PC added. Choose Refresh to check it.");
                return;
            }
            int c=key_character(code,a->shift,a->caps);
            if(((c>='0'&&c<='9')||c=='.')&&len<15){a->host_input[len]=(char)c;a->host_input[len+1]=0;}
            return;
        }
#ifdef DOVEOS_XWINDOW
        if(code==KEY_ESC){
            if(a->ml_settings_open)a->ml_settings_open=0;
            else if(a->ml_apps_open)a->ml_apps_open=0;
            else a->page=HOME;
            return;
        }
        if(a->ml_apps_open){
            if(code==KEY_LEFT&&a->app_selected>0)a->app_selected--;
            if(code==KEY_RIGHT&&a->app_selected<a->app_count-1)a->app_selected++;
            if(code==KEY_ENTER){a->ml_selected=6;moonlight_action(a);}
            return;
        }
        if(!a->ml_settings_open){
            if(code==KEY_LEFT&&a->host_selected>0){a->host_selected--;moonlight_probe(a,0);}
            if(code==KEY_RIGHT&&a->host_selected<a->host_count-1){a->host_selected++;moonlight_probe(a,0);}
            if(code==KEY_ENTER){if(a->ml_paired==1)a->ml_apps_open=1;else moonlight_probe(a,1);}
            return;
        }
#else
        if(code==KEY_ESC){a->page=HOME;return;}
#endif
        if(code==KEY_UP&&a->ml_selected>0)a->ml_selected--;
        if(code==KEY_DOWN&&a->ml_selected<12)a->ml_selected++;
        if(code==KEY_ENTER)moonlight_action(a);
        return;
    }
    if(code==KEY_ESC) {
        if(a->page==VIEWER)a->page=FILES;
        else if(a->page==FILES){if(!strcmp(a->cwd,a->root))a->page=HOME;else parent_folder(a);}
        else if(a->page==NOTES){if(!a->dirty||!save_notes(a))a->page=FILES;}
        else a->page=HOME;
        return;
    }
    if(a->page==FILES) {
        if(code==KEY_UP&&a->selected>0)a->selected--;
        if(code==KEY_DOWN&&a->selected<a->file_count-1)a->selected++;
        if(code==KEY_ENTER)open_entry(a,a->selected);
        if(code==KEY_BACKSPACE)parent_folder(a);
        if(a->selected<a->file_top)a->file_top=a->selected;
        if(a->selected>=a->file_top+10)a->file_top=a->selected-9;
        return;
    }
    if(a->page==VIEWER) {
        if(code==KEY_UP&&a->note_top>0)a->note_top--;
        if(code==KEY_DOWN)a->note_top++;
        return;
    }
    if(a->ctrl&&code==KEY_S){if(save_notes(a))message(a,"Save failed; check the USB.");return;}
    if(code==KEY_BACKSPACE&&a->note_cursor) {
        memmove(a->notes+a->note_cursor-1,a->notes+a->note_cursor,a->notes_len-a->note_cursor+1);
        a->note_cursor--;a->notes_len--;a->dirty=1;return;
    }
    if(code==KEY_DELETE&&a->note_cursor<a->notes_len) {
        memmove(a->notes+a->note_cursor,a->notes+a->note_cursor+1,a->notes_len-a->note_cursor);
        a->notes_len--;a->dirty=1;return;
    }
    if(code==KEY_LEFT&&a->note_cursor)a->note_cursor--;
    if(code==KEY_RIGHT&&a->note_cursor<a->notes_len)a->note_cursor++;
    if(code==KEY_HOME){int row,col;note_position(a,a->note_cursor,&row,&col);a->note_cursor=index_for_position(a,row,0);}
    if(code==KEY_END){int row,col;note_position(a,a->note_cursor,&row,&col);a->note_cursor=index_for_position(a,row,70);}
    if(code==KEY_UP||code==KEY_DOWN){int row,col;note_position(a,a->note_cursor,&row,&col);a->note_cursor=index_for_position(a,row+(code==KEY_UP?-1:1),col);}
    if(code==KEY_ENTER)note_insert(a,'\n');
    else if(code==KEY_TAB)note_insert(a,'\t');
    else if(!a->ctrl){int c=key_character(code,a->shift,a->caps);if(c)note_insert(a,(char)c);}
}
static void click(struct app *a,int x,int y) {
    if(a->launcher_mode){
        if(y>=15&&y<70&&x<240){key_press(a,KEY_F10,1);return;}
        if(strcmp(a->cwd,a->launcher_root)&&y>=615&&y<660&&x<260){key_press(a,KEY_BACKSPACE,1);return;}
        if(!strcmp(a->cwd,a->launcher_root)){
            if(x>=58&&x<1222&&y>=146&&y<650){
                int rx=x-58,ry=y-146;
                if(rx%584>=566||ry%84>=78)return;
                int col=rx/584,row=ry/84,index=row*2+col;
                if(index<a->file_count){a->selected=index;open_entry(a,index);}
            }
        } else if(x>=58&&x<1222&&y>=160&&y<600){
            int index=a->file_top+(y-160)/44;
            if(index<a->file_count){a->selected=index;open_entry(a,index);}
        }
        return;
    }
    if(a->page==HOME){
        static const int xx[]={58,646,58,646,58};
        static const int yy[]={150,150,290,290,430};
        static const unsigned short actions[]={KEY_F1,KEY_F3,KEY_F4,KEY_F6,KEY_F7};
        for(int i=0;i<5;i++)if(x>=xx[i]&&x<xx[i]+(i==4?1164:558)&&y>=yy[i]&&y<yy[i]+112){
            a->home_selected=i;key_press(a,actions[i],1);return;
        }
        return;
    }
    if(a->page==SETTINGS){
        if(y>=140&&y<140+8*64){a->settings_selected=(y-140)/64;setting_action(a,a->settings_selected);}
        return;
    }
    if(a->page==NETWORK){
        if(y>=170&&y<170+6*72){a->network_selected=(y-170)/72;network_action(a,a->network_selected);}
        return;
    }
    if(a->page==BLUETOOTH){
        if(y>=610&&y<660){
            if(x<320)bluetooth_action(a,"bluetooth-scan");
            else if(x<620)bluetooth_action(a,"bluetooth-pair");
            else if(x<930)bluetooth_action(a,"bluetooth-connect");
            else a->page=SETTINGS;
        } else if(y>=190&&y<190+16*26){
            int index=(y-190)/26;
            if(index<a->bt_count)a->bt_selected=index;
        }
        return;
    }
    if(a->page==DISPLAY){
        if(y>=525&&y<605)a->page=SETTINGS;
        else message(a,"Use the dock brightness down/up keys.");
        return;
    }
    if(a->page==SOUND){
        if(y>=250&&y<350){
            if(x<WIDTH/2)percent_action(a,0,-10);
            else percent_action(a,0,10);
        } else if(y>=525&&y<605)a->page=SETTINGS;
        return;
    }
    if(a->page==WIFI){
        int top=a->wifi_selected>11?a->wifi_selected-11:0;
        if(y>=180&&y<180+12*32){
            int index=top+(y-180)/32;
            if(index<a->wifi_count){a->wifi_selected=index;if(!a->wifi_edit){a->wifi_edit=1;a->wifi_password[0]=0;}}
        }
        else if(y>=610&&y<660){
            if(x<300)wifi_scan(a);
            else if(x<590)wifi_command(a,"wifi-reconnect",NULL,NULL);
            else if(x<880)wifi_command(a,"wifi-disconnect",NULL,NULL);
            else a->page=NETWORK;
        }
        return;
    }
    if(a->page==MOONLIGHT){
#ifdef DOVEOS_XWINDOW
        if(y<100){
            if(x<100){a->ml_settings_open=0;a->ml_apps_open=0;}
            else if(x>1190){a->ml_settings_open=!a->ml_settings_open;}
            else if(x>1125)message(a,"Select a PC, then an app. Use Ctrl+Alt+Shift+Q to quit streaming.");
            else if(x>1040){a->ml_edit=1;a->host_input[0]=0;}
            return;
        }
        if(a->ml_settings_open){
            if(y>=164&&y<164+5*84){a->ml_selected=7+(y-164)/84;moonlight_action(a);}
        }else if(a->ml_apps_open){
            if(y>=145&&y<500&&x>=50){
                int index=(x-50)/230;
                if((x-50)%230<192&&index<a->app_count){a->app_selected=index;a->ml_selected=6;moonlight_action(a);}
            }
        }else if(y>=145&&y<500&&x>=50&&x<306){
            int index=a->host_selected;
            if(index<a->host_count){
                a->host_selected=index;
                if(a->ml_paired==1)a->ml_apps_open=1;
                else {a->ml_selected=4;moonlight_action(a);}
            }
        }
        return;
#else
        if(y>=168&&y<168+13*39){a->ml_selected=(y-168)/39;moonlight_action(a);}
        return;
#endif
    }
    if(y>=18&&y<70){
        if(x>=40&&x<180)key_press(a,KEY_F1,1);
        else if(x>=200&&x<400)key_press(a,KEY_F3,1);
        else if(x>=420&&x<620)key_press(a,KEY_F4,1);
        else if(x>=640&&x<840)key_press(a,KEY_F6,1);
        else if(x>=860&&x<1020)key_press(a,KEY_F7,1);
        else if(x>=1060&&x<1240)key_press(a,KEY_F10,1);
        return;
    }
    if(a->page==FILES){
        if(y>=160&&y<600) {
            int index=a->file_top+(y-160)/44;
            if(index<a->file_count){a->selected=index;open_entry(a,index);}
        }
        if(y>=615&&x<240)parent_folder(a);
    } else if(a->page==VIEWER&&y>=615&&x<240)a->page=FILES;
    else if(a->page==NOTES&&y>=615&&x>1000){if(save_notes(a))message(a,"Save failed; check the USB.");}
}
static void text_area(struct app *a,const char *data,size_t len,int top,int cursor) {
    int row=0,col=0;
    for(size_t i=0;i<=len;i++){
        if((int)i==cursor && row>=top&&row<top+13)
            rect(a,68+col*16,174+(row-top)*32,2,30,CYAN);
        if(i==len)break;
        unsigned char c=(unsigned char)data[i];
        if(c=='\n'){row++;col=0;continue;}
        if(row>=top&&row<top+13)glyph(a,68+col*16,174+(row-top)*32,c=='\t'?' ':c,FG,2);
        if(++col>=70){row++;col=0;}
    }
}
#ifdef DOVEOS_V2
#include "desktop_v2.h"
#endif
static void draw(struct app *a) {
#ifdef DOVEOS_XWINDOW
    dove_text_count=0;
#endif
    rect(a,0,0,WIDTH,HEIGHT,BG);
    if(a->launcher_mode){
        button(a,58,24,190,"F10 Home",0);
        if(!strcmp(a->cwd,a->launcher_root)){
            label(a,58,74,"Game Systems",FG,3,35);
            label(a,58,126,"Choose a system. ROMs are stored on the USB drive.",MUTED,1,85);
            for(int i=0;i<a->file_count&&i<12;i++){
                int x=58+(i%2)*584,y=146+(i/2)*84;
                rect(a,x,y,566,78,i==a->selected?ACTIVE:CARD);
                if(i==a->selected)rect(a,x,y,566,4,CYAN);
                label(a,x+16,y+9,a->files[i].name,FG,2,32);
                label(a,x+16,y+47,!strcasecmp(a->files[i].name,"NES")?"NES core installed":"Core pending",MUTED,1,55);
            }
        } else {
            const char *system=strrchr(a->cwd,'/');system=system?system+1:a->cwd;
            label(a,58,88,system,FG,3,35);
            label(a,58,126,"Select a ROM. Enter starts supported games.",MUTED,1,85);
            for(int row=0;row<10;row++){
                int index=a->file_top+row;
                if(index>=a->file_count)break;
                int y=160+row*44;
                rect(a,58,y,1164,40,index==a->selected?ACTIVE:CARD);
                label(a,76,y+5,a->files[index].directory?"[DIR]":"[ROM]",CYAN,2,5);
                label(a,200,y+5,a->files[index].name,FG,2,57);
            }
            button(a,58,616,180,"Back",0);
            if(!a->file_count)label(a,80,260,"No ROMs in this folder yet",MUTED,2,50);
        }
        rect(a,0,670,WIDTH,50,CARD);
        label(a,20,686,a->status,MUTED,1,125);
        present(a);return;
    }
    if(a->page==HOME){
#ifdef DOVEOS_V2
        desktop_v2_home(a);
        present(a);return;
#endif
        label(a,58,48,"DoveOS",FG,3,35);
        struct network_info net=network_info();
        char connection[48];
        if(net.ethernet)snprintf(connection,sizeof connection,"Online: Ethernet");
        else if(net.wifi)snprintf(connection,sizeof connection,"Online: Wi-Fi (%s)",net.mhz>=4900?"5G":"2.4G");
        else snprintf(connection,sizeof connection,"Offline");
        label(a,950,55,connection,CYAN,1,27);
        if(net.wifi)signal_bars(a,1176,48,net.bars);
        label(a,58,108,"Choose with arrow keys and Enter",MUTED,2,55);
        static const int xx[]={58,646,58,646,58};
        static const int yy[]={150,150,290,290,430};
        static const char *titles[]={"Files","Moonlight","HDMI In","Connection Settings","Games"};
        static const char *details[]={"Notes, pictures, audio and video on USB","PC desktop","USB capture card","Network, display and sound","Open the game system launcher"};
        for(int i=0;i<5;i++){
            int w=i==4?1164:558;
            rect(a,xx[i],yy[i],w,112,i==a->home_selected?ACTIVE:CARD);
            if(i==a->home_selected)rect(a,xx[i],yy[i],w,5,CYAN);
            label(a,xx[i]+20,yy[i]+19,titles[i],FG,2,25);
            label(a,xx[i]+20,yy[i]+70,details[i],MUTED,1,60);
        }
        rect(a,0,660,WIDTH,60,CARD);
        label(a,20,676,"Arrows: select   Enter: open   F10: exit",MUTED,1,90);
        rect(a,a->cursor_x-1,a->cursor_y-12,3,25,FG);
        rect(a,a->cursor_x-12,a->cursor_y-1,25,3,FG);
        present(a);return;
    }
    if(a->page==SETTINGS){
        label(a,58,45,"Connection Settings",FG,3,35);
        label(a,58,105,"Arrows and Enter select. Esc returns home.",MUTED,2,60);
        static const char *items[]={"Network Settings","Bluetooth Settings","Display Settings","Sound Settings","Keep screen awake","Moonlight volume","Xbox wireless dongle","Back to home"};
        char detail[160];
        for(int i=0;i<8;i++){
            int y=140+i*64;
            rect(a,58,y,1164,56,i==a->settings_selected?ACTIVE:CARD);
            label(a,76,y+8,items[i],FG,2,27);
            if(i==0)snprintf(detail,sizeof detail,"Wi-Fi info, Ethernet, network search");
            if(i==1)snprintf(detail,sizeof detail,"Radio and pairing information");
            if(i==2)snprintf(detail,sizeof detail,"Screen brightness: %d%%",saved_percent(a,"brightness.txt",100));
            if(i==3)snprintf(detail,sizeof detail,"Dock speaker volume: %d%%",saved_percent(a,"sound-volume.txt",80));
            if(i==4)snprintf(detail,sizeof detail,"%s (Ethernet required)",setting_is("/usr/local/etc/drmosa-ethernet/keepawake-enabled","1")?"On":"Off");
            if(i==5){char path[1024];snprintf(path,sizeof path,"%s/DrMoSa/volume.txt",a->root);FILE *f=fopen(path,"r");int v=70;if(f){if(fscanf(f,"%d",&v)!=1)v=70;fclose(f);}snprintf(detail,sizeof detail,"%d%%; next Moonlight launch",v);}
            if(i==6)snprintf(detail,sizeof detail,"%s; Xbox USB adapter required",xbox_running()?"Running":"Stopped");
            if(i==7)snprintf(detail,sizeof detail,"Return to main menu");
            label(a,690,y+17,detail,MUTED,1,61);
        }
        rect(a,0,681,WIDTH,39,CARD);
        label(a,20,692,a->status,MUTED,1,125);
        present(a);return;
    }
    if(a->page==NETWORK){
        struct network_info net=network_info();
        char title[96],detail[128];
        label(a,58,38,"Network Settings",FG,3,35);
        label(a,58,103,"Arrows and Enter select. Esc returns to Connection Settings.",MUTED,1,75);
        for(int i=0;i<6;i++){
            int y=170+i*72;
            rect(a,58,y,1164,64,i==a->network_selected?ACTIVE:CARD);
            title[0]=detail[0]=0;
            if(i==0){
                snprintf(title,sizeof title,"Wi-Fi information");
                if(net.wifi)snprintf(detail,sizeof detail,"%.32s | %s | %s | signal %s",net.ssid[0]?net.ssid:"Connected",net.ip[0]?net.ip:"IP pending",net.mhz>=4900?"5 GHz":"2.4 GHz",net.bars<0?"unavailable":"shown by bars");
                else snprintf(detail,sizeof detail,"Disconnected");
            }
            if(i==1){
                snprintf(title,sizeof title,"Ethernet: %s",access("/media/usb0/DRMOSA_DISABLE_ETHERNET",F_OK)?"On":"Off");
                snprintf(detail,sizeof detail,"%s | Enter to turn %s",net.ethernet?"Cable connected":"No cable link or disabled",access("/media/usb0/DRMOSA_DISABLE_ETHERNET",F_OK)?"off":"on");
            }
            if(i==2){snprintf(title,sizeof title,"Search Wi-Fi networks");snprintf(detail,sizeof detail,"Scan and choose a network");}
            if(i==3){snprintf(title,sizeof title,"Reconnect saved Wi-Fi");snprintf(detail,sizeof detail,"Use the network key saved on USB");}
            if(i==4){snprintf(title,sizeof title,"Disconnect Wi-Fi");snprintf(detail,sizeof detail,"Keep saved network for later");}
            if(i==5){snprintf(title,sizeof title,"Back");snprintf(detail,sizeof detail,"Connection Settings");}
            label(a,76,y+5,title,FG,2,27);
            label(a,620,y+18,detail,MUTED,1,63);
            if(i==0&&net.wifi)signal_bars(a,1152,y+17,net.bars);
        }
        rect(a,0,681,WIDTH,39,CARD);
        label(a,20,692,a->wifi_state[0]?a->wifi_state:a->status,MUTED,1,125);
        present(a);return;
    }
    if(a->page==BLUETOOTH){
        label(a,58,38,"Bluetooth Settings",FG,3,35);
        label(a,58,93,"Built-in Realtek radio. Put a device in pairing mode, then scan.",MUTED,2,75);
        label(a,58,133,"The Xbox may pair, but its input driver still needs verification.",ORANGE,1,88);
        if(!a->bt_count)label(a,80,234,"No devices listed. Press F8 to search nearby Bluetooth devices.",MUTED,2,75);
        for(int i=0;i<a->bt_count;i++){
            int y=190+i*26;
            struct bt_device *device=&a->bt_devices[i];
            rect(a,58,y,1164,24,i==a->bt_selected?ACTIVE:CARD);
            label(a,75,y+4,device->le?"LE":"Classic",CYAN,1,8);
            label(a,185,y+4,device->name,FG,1,55);
            label(a,900,y+4,device->address,MUTED,1,18);
        }
        button(a,58,610,250,"F8 Scan",0);
        button(a,355,610,250,"Enter Pair",0);
        button(a,652,610,250,"F9 Connect",0);
        button(a,949,610,273,"Esc Back",0);
        rect(a,0,681,WIDTH,39,CARD);
        label(a,20,692,a->bt_state[0]?a->bt_state:a->status,MUTED,1,125);
        present(a);return;
    }
    if(a->page==DISPLAY||a->page==SOUND){
        int level=a->page==DISPLAY?a->display_level:a->sound_level;
        char value[32];snprintf(value,sizeof value,"%d%%",level);
        label(a,58,45,a->page==DISPLAY?"Display Settings":"Sound Settings",FG,3,35);
        label(a,58,110,a->page==DISPLAY?"Screen brightness":"Dock speaker volume",MUTED,2,40);
        rect(a,140,250,1000,70,CARD);
        rect(a,150,260,level*98/10,50,CYAN);
        label(a,590,270,value,FG,2,8);
        button(a,140,360,280,a->page==DISPLAY?"Brightness down key":"Left: -10",0);
        button(a,860,360,280,a->page==DISPLAY?"Brightness up key":"Right: +10",0);
        label(a,a->page==DISPLAY?305:375,445,
              a->page==DISPLAY?"Use the dock brightness keys. The level is saved on USB.":
                               "Use Left/Right or Up/Down. The level is saved on USB.",
              MUTED,1,a->page==DISPLAY?82:70);
        button(a,58,525,260,"Esc Back",1);
        rect(a,0,681,WIDTH,39,CARD);
        label(a,20,692,a->status,MUTED,1,125);
        present(a);return;
    }
    if(a->page==WIFI){
        label(a,58,38,"Search Wi-Fi networks",FG,3,35);
        label(a,58,100,"Select a network, type password, press Enter.",MUTED,2,58);
        int top=a->wifi_selected>11?a->wifi_selected-11:0;
        for(int i=top;i<a->wifi_count&&i<top+12;i++){
            int y=180+(i-top)*32;
            rect(a,58,y,1164,30,i==a->wifi_selected?ACTIVE:CARD);
            label(a,75,y+5,a->wifi_names[i],FG,1,32);
        }
        if(!a->wifi_count)label(a,75,220,"No networks listed. Choose Scan again.",MUTED,2,50);
        if(a->wifi_edit){
            char dots[65];size_t n=strlen(a->wifi_password);memset(dots,'*',n);dots[n]=0;
            rect(a,58,540,1164,55,ACTIVE);
            label(a,76,548,"Password:",FG,2,12);
            label(a,255,548,dots,FG,2,48);
        }
        button(a,58,610,230,"F8 Scan",0);
        button(a,348,610,230,"F9 Reconnect",0);
        button(a,638,610,230,"Del Disconnect",0);
        button(a,928,610,230,"Esc Back",0);
        rect(a,0,681,WIDTH,39,CARD);
        label(a,20,692,a->wifi_state[0]?a->wifi_state:a->status,MUTED,1,125);
        present(a);return;
    }
    if(a->page==MOONLIGHT){
#ifdef DOVEOS_XWINDOW
        static const int bitrates[]={3000,5000,8000,10000};
        static const int volumes[]={0,25,50,70,80,90,100};
        static const int pointers[]={40,60,80,100};
        const uint32_t white=0x00ffffffu,gray=0x00bdbdbdu,blue=0x004054afu;
        rect(a,0,0,WIDTH,HEIGHT,0x002e2e2eu);
        rect(a,0,0,WIDTH,96,blue);
        if(a->ml_settings_open){
            moon_icon(a,moonlight_back_rgb,32,24,32);
            label(a,530,23,"Settings",white,3,20);
        }else if(a->ml_apps_open){
            moon_icon(a,moonlight_back_rgb,32,24,32);
            label(a,500,23,a->ml_host_name[0]?a->ml_host_name:a->hosts[a->host_selected],white,3,22);
        }else{
            label(a,518,23,"Computers",white,3,20);
            moon_icon(a,moonlight_add_rgb,32,1103,32);
            moon_icon(a,moonlight_help_rgb,32,1155,32);
        }
        moon_icon(a,moonlight_settings_rgb,32,1207,32);
        if(a->ml_settings_open){
            const char *titles[]={"Resolution","Bitrate","Dock volume","Pointer speed","Video codec"};
            char values[5][80];
            snprintf(values[0],80,"%s",a->ml_resolution==0?"1080p 60 FPS":a->ml_resolution==1?"720p 60 FPS":"1080p 30 FPS");
            snprintf(values[1],80,"%d Kbps",bitrates[a->ml_bitrate]);
            snprintf(values[2],80,"%d%%",volumes[a->ml_volume]);
            snprintf(values[3],80,"%d%%",pointers[a->ml_pointer]);
            snprintf(values[4],80,"%s",a->ml_codec?"HEVC hardware":"H.264 hardware");
            label(a,50,112,"Streaming settings",white,2,40);
            for(int i=0;i<5;i++){
                int y=164+i*84;
                rect(a,50,y,1180,72,i==a->ml_selected-7?0x003a3a3au:0x00333333u);
                label(a,75,y+13,titles[i],white,2,24);
                label(a,680,y+13,values[i],gray,2,28);
            }
            label(a,54,610,"Click a setting to change it",gray,1,70);
        }else if(a->ml_apps_open){
            for(int i=0;i<a->app_count&&i<4;i++){
                int x=50+i*230;
                const unsigned char *art=a->apps[i].id==881448767?moonlight_desktop_art_rgb:
                    a->apps[i].id==1093255277?moonlight_steam_art_rgb:NULL;
                if(art)moon_art(a,art,x,145);
                else {moon_icon_2x(a,moonlight_app_rgb,128,x,145);
                    label(a,x,424,a->apps[i].name,white,3,16);}
            }
            label(a,50,615,"Select an app to start streaming",gray,1,65);
            label(a,50,657,"Ctrl+Alt+Shift+Q ends a stream",gray,1,65);
        }else{
            for(int i=a->host_selected;i==a->host_selected;i++){
                int x=50;
                moon_icon_2x(a,moonlight_pc_rgb,128,x,145);
                if(i==a->host_selected&&a->ml_paired!=1)moon_icon(a,moonlight_lock_rgb,64,x+96,226);
                const char *name=i==a->host_selected&&a->ml_host_name[0]?a->ml_host_name:a->hosts[i];
                label(a,x,424,name,white,3,16);
            }
            if(a->host_count>1)label(a,50,505,"Use left/right arrows to change PC",gray,1,60);
            if(a->ml_edit){
                rect(a,210,218,860,246,0x00424242u);
                label(a,254,247,"Add PC",white,3,20);
                label(a,254,320,"Enter PC address and press Enter",gray,2,50);
                char input[64];snprintf(input,sizeof input,"%s_",a->host_input);
                label(a,254,375,input,white,2,40);
            }
            if(a->ml_pin[0]){
                char pin[100];snprintf(pin,sizeof pin,"Pairing PIN: %s - enter it in Sunshine",a->ml_pin);
                label(a,50,610,pin,white,2,60);
            }else label(a,50,610,a->ml_feedback[0]?a->ml_feedback:a->status,gray,1,120);
        }
        present(a);return;
#else
        static const int bitrate[]={3000,5000,8000,10000};
        static const int volume[]={0,25,50,70,80,90,100};
        static const int pointer[]={40,60,80,100};
        label(a,58,28,"Moonlight",FG,3,35);
        label(a,58,89,"Choose a PC and app, pair, then start the stream.",MUTED,2,58);
        char line[160];
        if(a->ml_edit)snprintf(line,sizeof line,"PC address: %s_",a->host_input);
        else if(a->ml_pin[0])snprintf(line,sizeof line,"Pairing PIN: %s  (enter it in Sunshine on the PC)",a->ml_pin);
        else snprintf(line,sizeof line,"%s",a->ml_feedback);
        label(a,58,135,line,CYAN,2,70);
        const char *res=a->ml_resolution==0?"1080p / 60":a->ml_resolution==1?"720p / 60":"1080p / 30";
        const char *titles[]={"PC","Add PC address","Find PC on LAN","Refresh status","Pair this PC","App","Start stream","Resolution","Bitrate","Dock volume","Pointer speed","Codec","Back to home"};
        char values[13][96];
        snprintf(values[0],sizeof values[0],"%s (%d of %d)",a->hosts[a->host_selected],a->host_selected+1,a->host_count);
        snprintf(values[1],sizeof values[1],"Enter an IPv4 address");
        snprintf(values[2],sizeof values[2],"Search for Sunshine PCs");
        snprintf(values[3],sizeof values[3],"%s",a->ml_paired==1?"Paired":a->ml_paired==0?"Not paired":"Check PC");
        snprintf(values[4],sizeof values[4],"Show a PIN on this screen");
        snprintf(values[5],sizeof values[5],"%s",a->apps[a->app_selected].name);
        snprintf(values[6],sizeof values[6],"Connect to selected PC and app");
        snprintf(values[7],sizeof values[7],"%s",res);
        snprintf(values[8],sizeof values[8],"%d Kbps",bitrate[a->ml_bitrate]);
        snprintf(values[9],sizeof values[9],"%d%%",volume[a->ml_volume]);
        snprintf(values[10],sizeof values[10],"%d%%",pointer[a->ml_pointer]);
        snprintf(values[11],sizeof values[11],"%s",a->ml_codec?"HEVC hardware (experimental)":"H.264 hardware");
        snprintf(values[12],sizeof values[12],"Return to main menu");
        for(int i=0;i<13;i++){
            int y=168+i*39;
            rect(a,58,y,1164,36,i==a->ml_selected?ACTIVE:CARD);
            label(a,72,y+7,titles[i],FG,1,26);
            label(a,430,y+7,values[i],MUTED,1,85);
        }
        rect(a,0,681,WIDTH,39,CARD);
        label(a,20,692,a->status,MUTED,1,125);
        present(a);return;
#endif
    }
    button(a,40,18,140,"Files",a->page==FILES||a->page==VIEWER||a->page==NOTES);
    button(a,200,18,200,"Moonlight",0);
    button(a,420,18,200,"HDMI In",0);
    button(a,640,18,200,"Settings",0);
    button(a,860,18,160,"Games",0);
    button(a,1060,18,180,"Save & Exit",0);
    rect(a,40,84,1200,4,CYAN);
    if(a->page==FILES){
        label(a,58,104,"USB files",FG,2,40);
        label(a,58,132,a->cwd,MUTED,1,125);
        for(int row=0;row<10;row++){
            int index=a->file_top+row;
            if(index>=a->file_count)break;
            int y=160+row*44;
            rect(a,58,y,1164,40,index==a->selected?ACTIVE:CARD);
            enum dove_file_type type=dove_file_type(a->files[index].name);
            const char *kind=a->files[index].directory?"[DIR]":
                type==DOVE_TEXT?"[TXT]":type==DOVE_PICTURE?"[PIC]":
                type==DOVE_AUDIO?"[AUD]":type==DOVE_VIDEO?"[VID]":"[---]";
            uint32_t color=a->files[index].directory?CYAN:
                type==DOVE_TEXT?GREEN:type==DOVE_PICTURE?GOLD:
                type==DOVE_AUDIO?CYAN:type==DOVE_VIDEO?ORANGE:FG;
            label(a,74,y+5,kind,color,2,5);
            label(a,200,y+5,a->files[index].name,color,2,57);
        }
        button(a,58,616,180,"Back",0);
        if(!a->file_count)label(a,80,260,"USB folder is empty",MUTED,2,50);
    } else if(a->page==NOTES){
        label(a,58,105,"Notes.txt on USB",FG,2,50);
        rect(a,58,160,1164,426,CARD);
        int row,col;note_position(a,a->note_cursor,&row,&col);
        if(row<a->note_top)a->note_top=row;
        if(row>=a->note_top+13)a->note_top=row-12;
        text_area(a,a->notes,a->notes_len,a->note_top,(int)a->note_cursor);
        label(a,58,600,"Type. Ctrl+S saves. Esc returns to Files.",MUTED,1,90);
        button(a,1030,616,192,a->dirty?"Save *":"Saved",0);
    } else {
        label(a,58,105,a->view_title,FG,2,50);
        rect(a,58,160,1164,426,CARD);
        text_area(a,a->view,strlen(a->view),a->note_top,-1);
        button(a,58,616,180,"Back",0);
        label(a,270,628,"Read only. Up/Down scroll.",MUTED,1,70);
    }
    rect(a,0,681,WIDTH,39,CARD);
    label(a,20,692,a->status,MUTED,1,125);
    rect(a,a->cursor_x-1,a->cursor_y-12,3,25,FG);
    rect(a,a->cursor_x-12,a->cursor_y-1,25,3,FG);
    present(a);
}
#ifndef DOVEOS_XWINDOW
static int elapsed_ms(struct timespec first,struct timespec second) {
    return (int)((second.tv_sec-first.tv_sec)*1000+(second.tv_nsec-first.tv_nsec)/1000000);
}
static int input_event(struct app *a,int device,struct input_event *ev) {
    if(device==0&&ev->type==EV_KEY){key_press(a,ev->code,ev->value);return 1;}
    if(device!=1)return 0;
    if(ev->type==EV_KEY&&ev->code==BTN_TOUCH){
        if(ev->value){a->touching=1;a->moved=0;a->have_x=a->have_y=0;clock_gettime(CLOCK_MONOTONIC,&a->touched);}
        else if(a->touching){
            struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
            if(!a->moved&&elapsed_ms(a->touched,now)<700)click(a,a->cursor_x,a->cursor_y);
            a->touching=0;a->have_x=a->have_y=0;
        }
        return 1;
    }
    if(ev->type==EV_KEY&&(ev->code==BTN_LEFT||ev->code==BTN_RIGHT)&&ev->value==1){
        if(ev->code==BTN_LEFT)click(a,a->cursor_x,a->cursor_y);
        return 1;
    }
    if(ev->type==EV_ABS&&a->touching&&(ev->code==ABS_X||ev->code==ABS_Y)){
        int delta=0;
        if(ev->code==ABS_X){if(a->have_x)delta=ev->value-a->last_x;a->last_x=ev->value;a->have_x=1;}
        else {if(a->have_y)delta=ev->value-a->last_y;a->last_y=ev->value;a->have_y=1;}
        if(abs(delta)>3){a->moved=1;if(ev->code==ABS_X)a->cursor_x+=delta*60/100;else a->cursor_y+=delta*60/100;}
        if(a->cursor_x<0)a->cursor_x=0;
        if(a->cursor_x>=WIDTH)a->cursor_x=WIDTH-1;
        if(a->cursor_y<0)a->cursor_y=0;
        if(a->cursor_y>=HEIGHT)a->cursor_y=HEIGHT-1;
        return delta!=0;
    }
    return 0;
}
#endif
#ifdef DOVEOS_XWINDOW
#include "xwindow_backend.h"
#endif
static int open_display(struct app *a) {
#ifdef DOVEOS_XWINDOW
    return dove_x_open(a);
#else
    int fd=open("/dev/fb0",O_RDWR|O_CLOEXEC);
    if(fd<0)return -1;
    struct fb_var_screeninfo var={0};struct fb_fix_screeninfo fix={0};
    if(ioctl(fd,FBIOGET_VSCREENINFO,&var)||ioctl(fd,FBIOGET_FSCREENINFO,&fix)||
       var.xres!=WIDTH||var.yres!=HEIGHT||var.bits_per_pixel!=32||fix.line_length<WIDTH*4){close(fd);return -1;}
    a->stride=(int)fix.line_length/4;a->fb_size=(size_t)fix.line_length*HEIGHT;
    a->pixels=mmap(NULL,a->fb_size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    close(fd);
    if(a->pixels==MAP_FAILED){a->pixels=NULL;return -1;}
    a->saved=malloc(a->fb_size);
    if(!a->saved){munmap(a->pixels,a->fb_size);a->pixels=NULL;return -1;}
    memcpy(a->saved,a->pixels,a->fb_size);
    vo_library=dlopen("/usr/lib/libVOutWrapper.so",RTLD_LAZY|RTLD_LOCAL|RTLD_DEEPBIND);
    if(!vo_library){fprintf(stderr,"VOUT load: %s\n",dlerror());return -1;}
    int (*init)(void)=dlsym(vo_library,"vo_wrap_init");
    int (*force)(void)=dlsym(vo_library,"vo_wrap_v1_force_on");
    int (*alloc)(vo_buffer*)=dlsym(vo_library,"vo_wrap_alloc_nv12_buffer");
    vo_render=dlsym(vo_library,"vo_wrap_render");
    vo_stop=dlsym(vo_library,"vo_wrap_v1_stop");
    vo_deinit=dlsym(vo_library,"vo_wrap_deinit");
    vo_free=dlsym(vo_library,"vo_wrap_free_nv12_buffer");
    if(!init||!force||!alloc||!vo_render||!vo_stop||!vo_deinit||!vo_free)return -1;
    if(init())return -1;
    vo_ready=1;
    vo_frame.width=WIDTH;vo_frame.height=HEIGHT;
    vo_frame.stride=WIDTH;vo_frame.aligned_height=HEIGHT;
    if(alloc(&vo_frame)||!vo_frame.data||!vo_frame.physical||
       vo_frame.bytes<(WIDTH*HEIGHT*3/2))return -1;
    if((unsigned)force()!=0x10000000u)return -1;
    fprintf(stderr,"MENU_VOUT_READY bytes=%u physical=%08x\n",vo_frame.bytes,vo_frame.physical);
    return 0;
#endif
}
#ifndef DOVEOS_XWINDOW
static uint8_t clipped(int n) {return (uint8_t)(n<0?0:n>255?255:n);}
#endif
static void present(struct app *a) {
#ifdef DOVEOS_XWINDOW
    dove_x_present(a);
#else
    if(!vo_ready||!vo_frame.data)return;
    uint8_t *yplane=vo_frame.data;
    uint8_t *uvplane=vo_frame.data+vo_frame.stride*vo_frame.aligned_height;
    for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++){
        uint32_t c=*pixel(a,x,y);
        int r=(c>>16)&255,g=(c>>8)&255,b=c&255;
        yplane[y*WIDTH+x]=clipped(((66*r+129*g+25*b+128)>>8)+16);
        if(!(x&1)&&!(y&1)){
            int slot=(y/2)*WIDTH+x;
            uvplane[slot]=clipped(((-38*r-74*g+112*b+128)>>8)+128);
            uvplane[slot+1]=clipped(((112*r-94*g-18*b+128)>>8)+128);
        }
    }
    __sync_synchronize();
    int rc=vo_render(&vo_frame);
    if(rc)fprintf(stderr,"MENU_RENDER_ERROR=%d\n",rc);
#endif
}
static void close_display(struct app *a) {
#ifdef DOVEOS_XWINDOW
    dove_x_close(a);
#else
    if(vo_ready){
        int stopped=vo_stop();
        fprintf(stderr,"MENU_VOUT_STOP=%08x\n",(unsigned)stopped);
        if((unsigned)stopped==0x10000000u&&vo_frame.data)vo_free(&vo_frame);
        vo_deinit();vo_ready=0;
    }
    if(vo_library){dlclose(vo_library);vo_library=NULL;}
    if(a->pixels){memcpy(a->pixels,a->saved,a->fb_size);munmap(a->pixels,a->fb_size);}
    free(a->saved);
#endif
}
int main(int argc,char **argv) {
    if(argc>1&&!strcmp(argv[1],"--check")) {
        if(font8x16['A'][0]==0&&font8x16['A'][7]==0)return 1;
        if(key_character(KEY_A,0,0)!='a'||key_character(KEY_A,1,0)!='A')return 2;
        struct app test={0};
        key_press(&test,KEY_F3,1);
        if(test.page!=MOONLIGHT||quit)return 3;
        key_press(&test,KEY_F4,1);
        if(!test.launch_hdmi||!quit)return 4;
        quit=0;
        struct app home={.page=HOME};
        key_press(&home,KEY_RIGHT,1);
        if(home.home_selected!=1)return 5;
        key_press(&home,KEY_LEFT,1);
        key_press(&home,KEY_ENTER,1);
        if(home.page!=FILES)return 6;
        struct app settings={.page=HOME,.home_selected=3};
        key_press(&settings,KEY_F6,1);
        if(settings.page!=SETTINGS)return 7;
        key_press(&settings,KEY_ENTER,1);
        if(settings.page!=NETWORK)return 14;
        key_press(&settings,KEY_DOWN,1);
        if(settings.network_selected!=1)return 15;
        key_press(&settings,KEY_ESC,1);
        if(settings.page!=SETTINGS)return 16;
        key_press(&settings,KEY_DOWN,1);
        if(settings.settings_selected!=1)return 8;
        key_press(&settings,KEY_ENTER,1);
        if(settings.page!=BLUETOOTH)return 17;
        key_press(&settings,KEY_ESC,1);
        if(settings.page!=SETTINGS)return 18;
        key_press(&settings,KEY_ESC,1);
        if(settings.page!=HOME)return 9;
        struct app wifi={.page=WIFI,.wifi_edit=1};
        key_press(&wifi,KEY_A,1);
        if(strcmp(wifi.wifi_password,"a"))return 12;
        key_press(&wifi,KEY_BACKSPACE,1);
        if(wifi.wifi_password[0])return 13;
        struct app game={.page=HOME,.home_selected=4};
        key_press(&game,KEY_ENTER,1);
        if(game.launch_game!=1||!quit)return 10;
        quit=0;
        struct app launcher={.launcher_mode=1,.page=FILES};
        strcpy(launcher.cwd,"/usb/ROMs");strcpy(launcher.launcher_root,"/usb/ROMs");
        key_press(&launcher,KEY_F10,1);
        if(!quit)return 11;
        quit=0;
        puts("MENU_CHECK_OK");return 0;
    }
    if(argc>2&&!strcmp(argv[1],"--render-demo")) {
        struct app demo={.cursor_x=1100,.cursor_y=300,.page=HOME,.stride=WIDTH};
        if(argc>3&&!strcmp(argv[3],"launcher")){
            demo.launcher_mode=1;demo.page=FILES;demo.file_count=11;
            strcpy(demo.cwd,"/media/usb0/DrMoSa/Games/ROMs");
            strcpy(demo.launcher_root,demo.cwd);
            static const char *systems[]={"Arcade","Atari 2600","GBA","Genesis","MSX","MSX2","NES","Neo Geo","Nintendo DS","PS1","SNES"};
            for(int i=0;i<11;i++){strcpy(demo.files[i].name,systems[i]);demo.files[i].directory=1;}
        }
        if(argc>3&&!strcmp(argv[3],"files")){
            demo.page=FILES;demo.file_count=6;
            static const char *names[]={"DrMoSa","notes.txt","picture.png","music.mp3","movie.mp4","archive.zip"};
            for(int i=0;i<6;i++)strcpy(demo.files[i].name,names[i]);
            demo.files[0].directory=1;
        }
        demo.pixels=calloc((size_t)WIDTH*HEIGHT,sizeof(uint32_t));
        if(!demo.pixels)return 1;
        if(!demo.launcher_mode)strcpy(demo.cwd,"/media/usb0");
        strcpy(demo.status,demo.launcher_mode?"Arrows: choose system   Enter: open   F10: home":
            "F1 Files    F3 Moonlight    F4 HDMI In    F6 Settings    F7 Games    F10 Exit");
        if(!demo.launcher_mode&&demo.page==HOME){
            demo.file_count=3;
            strcpy(demo.files[0].name,"DrMoSa");demo.files[0].directory=1;
            strcpy(demo.files[1].name,"readme.txt");
            strcpy(demo.files[2].name,"sample.txt");
        }
        draw(&demo);
        FILE *out=fopen(argv[2],"wb");
        if(!out){free(demo.pixels);return 2;}
        fprintf(out,"P6\n%d %d\n255\n",WIDTH,HEIGHT);
        for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++){
            uint32_t c=demo.pixels[(size_t)y*WIDTH+x];
            unsigned char rgb[3]={(unsigned char)(c>>16),(unsigned char)(c>>8),(unsigned char)c};
            fwrite(rgb,1,3,out);
        }
        fclose(out);free(demo.pixels);puts("MENU_RENDER_OK");return 0;
    }
    struct app a={.kb=-1,.tp=-1,.cursor_x=WIDTH/2,.cursor_y=HEIGHT/2,.page=HOME};
    a.launcher_mode=argc>3&&!strcmp(argv[3],"--rom-launcher");
    const char *root=argc>1&&argv[1][0]=='/'?argv[1]:USB_ROOT;
    char *resolved=realpath(root,NULL);
    if(!resolved||strlen(resolved)>=sizeof a.root){
        free(resolved);
        fprintf(stderr,"USB root unavailable: %s\n",root);return 1;
    }
    strcpy(a.root,resolved);
    free(resolved);
    if(a.launcher_mode){
        if(snprintf(a.launcher_root,sizeof a.launcher_root,"%s/DrMoSa/Games/ROMs",a.root)>=(int)sizeof a.launcher_root)return 1;
        strcpy(a.cwd,a.launcher_root);a.page=FILES;
    } else strcpy(a.cwd,a.root);
    char notes_dir[1024];
    if(snprintf(notes_dir,sizeof notes_dir,"%s/DrMoSa",a.root)<(int)sizeof notes_dir)
        mkdir(notes_dir,0700);
    list_files(&a);
    if(!a.launcher_mode){load_notes(&a);load_moonlight(&a);message(&a,"Use arrow keys and Enter to choose a mode.");}
    int return_to_files=0;
    FILE *last=a.launcher_mode?NULL:fopen("/tmp/drmosa-usb-menu-last","r");
    if(last){
        char line[120]={0};
        if(fgets(line,sizeof line,last)){
            if(strstr(line,"MOONLIGHT_OK")){message(&a,"Moonlight ended. Choose PC and Start Desktop.");a.page=MOONLIGHT;}
            else if(strstr(line,"MOONLIGHT_FAILED")){message(&a,"Moonlight could not connect. Check Sunshine and Ethernet.");a.page=MOONLIGHT;}
            else if(strstr(line,"HDMI_OK"))message(&a,"HDMI In ended. F4 starts it again.");
            else if(strstr(line,"HDMI_FAILED"))message(&a,"HDMI In could not start. Check capture card and USB hub.");
            else if(strstr(line,"MEDIA_OK")){message(&a,"File closed. Select another file.");return_to_files=1;}
            else if(strstr(line,"MEDIA_FAILED")){message(&a,"File could not open. Check its format and media.log.");return_to_files=1;}
            else if(strstr(line,"GAME_OK"))message(&a,"Game ended. Open Games to choose another ROM.");
            else if(strstr(line,"GAME_FAILED"))message(&a,"Game could not start. Check Games game.log.");
        }
        fclose(last);unlink("/tmp/drmosa-usb-menu-last");
    }
    if(return_to_files){
        FILE *saved=fopen("/tmp/drmosa-files-return","r");
        if(saved){
            char path[sizeof a.cwd]={0};
            if(fgets(path,sizeof path,saved)){
                path[strcspn(path,"\r\n")]=0;
                char *real=realpath(path,NULL);
                size_t root_len=strlen(a.root);
                struct stat st;
                if(real&&!strncmp(real,a.root,root_len)&&
                   (real[root_len]=='/'||real[root_len]==0)&&
                   strlen(real)<sizeof a.cwd&&!stat(real,&st)&&S_ISDIR(st.st_mode))
                    strcpy(a.cwd,real);
                free(real);
            }
            fclose(saved);unlink("/tmp/drmosa-files-return");
        }
        a.page=FILES;list_files(&a);message(&a,"File closed. Select another file.");
    }
#ifdef DOVEOS_XWINDOW
    if(argc>3&&!strcmp(argv[3],"--moonlight"))a.page=MOONLIGHT;
    else if(argc>3&&!strcmp(argv[3],"--settings"))a.page=SETTINGS;
    if(a.page==MOONLIGHT)moonlight_probe(&a,0);
#endif
    if(open_display(&a)){fprintf(stderr,"1280x720 framebuffer or video output unavailable\n");close_display(&a);return 2;}
#ifdef DOVEOS_XWINDOW
    signal(SIGINT,on_signal);signal(SIGTERM,on_signal);
    draw(&a);
    int poll_ticks=0;
    while(!quit){
        int changed=0;
        while(XPending(dove_display)){
            XEvent e;XNextEvent(dove_display,&e);
            if(e.type==Expose)changed=1;
            else if(e.type==ClientMessage&&e.xclient.data.l[0]==(long)dove_delete)quit=1;
            else if(e.type==DestroyNotify)quit=1;
            else if(e.type==KeyPress){
                int code=e.xkey.keycode-8;
                if(code>=0&&code<248){key_press(&a,(unsigned short)code,1);changed=1;}
            }else if(e.type==KeyRelease){
                int code=e.xkey.keycode-8;
                if(code>=0&&code<248)key_press(&a,(unsigned short)code,0);
            }else if(e.type==ButtonPress){
                int x=e.xbutton.x*WIDTH/DOVE_X_W,y=e.xbutton.y*HEIGHT/DOVE_X_H;
                if(e.xbutton.button==1)click(&a,x,y);
                else if(e.xbutton.button==4)key_press(&a,KEY_UP,1);
                else if(e.xbutton.button==5)key_press(&a,KEY_DOWN,1);
                changed=1;
            }
        }
        if(a.page==HOME)quit=1;
        if(a.ml_child>0&&++poll_ticks>=8){moonlight_poll(&a);changed=1;poll_ticks=0;}
        changed|=bluetooth_poll(&a);
        if(changed&&!quit)draw(&a);
        usleep(25000);
    }
    if(a.dirty&&save_notes(&a))fprintf(stderr,"Unable to save notes to USB\n");
    close_display(&a);
    return a.launch_moonlight?10:0;
#else
    a.kb=open("/dev/input/event1",O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    a.tp=open("/dev/input/event2",O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    if(a.kb<0||a.tp<0){fprintf(stderr,"Dock keyboard or touchpad unavailable\n");close_display(&a);return 3;}
    signal(SIGINT,on_signal);signal(SIGTERM,on_signal);
    int seconds=argc>2?atoi(argv[2]):MENU_DEFAULT_SECONDS;
    if(seconds<0||seconds>7200)seconds=MENU_DEFAULT_SECONDS;
    time_t end=seconds?time(NULL)+seconds:0,last_draw=0;
    draw(&a);
    fprintf(stderr,"MENU_DRAWN root=%s files=%d fb_size=%zu\n",a.root,a.file_count,a.fb_size);
    fflush(stderr);
    struct pollfd fds[2]={{a.kb,POLLIN,0},{a.tp,POLLIN,0}};
    while(!quit&&(!end||time(NULL)<end)&&access(MENU_STOP_PATH,F_OK)){
        int changed=0;int ready=poll(fds,2,100);
        if(a.ml_child>0){int prior=a.ml_child;moonlight_poll(&a);if(a.ml_child!=prior)changed=1;}
        changed|=bluetooth_poll(&a);
        if(ready<0&&errno!=EINTR)break;
        for(int dev=0;dev<2;dev++)if(fds[dev].revents&POLLIN){
            struct input_event event;
            while(read(fds[dev].fd,&event,sizeof event)==sizeof event)
                changed|=input_event(&a,dev,&event);
        }
        if(changed||time(NULL)-last_draw>=2){draw(&a);last_draw=time(NULL);}
    }
    if(a.dirty&&save_notes(&a))fprintf(stderr,"Unable to save notes to USB\n");
    if(a.launch_media){
        FILE *selected=fopen("/tmp/drmosa-media-selected","w");
        if(selected){fprintf(selected,"%s\n",a.media_path);fclose(selected);}
        else a.launch_media=0;
        FILE *folder=fopen("/tmp/drmosa-files-return","w");
        if(folder){fprintf(folder,"%s\n",a.cwd);fclose(folder);}
    }
    if(a.launcher_mode&&a.launch_game==2){
        FILE *selected=fopen("/tmp/drmosa-rom-selected","w");
        if(selected){fprintf(selected,"%s\n",a.game_path);fclose(selected);}
        else a.launch_game=0;
    }
    fprintf(stderr,"MENU_DONE reason=%s\n",a.launch_moonlight?"moonlight":a.launch_hdmi?"hdmi":a.launch_media?"media":a.launch_game?"game":quit?"signal-or-exit":end&&time(NULL)>=end?"timeout":"stop-or-input");
    close(a.kb);close(a.tp);close_display(&a);
    return a.launch_moonlight?10:a.launch_hdmi?11:a.launch_media?12:a.launcher_mode?(a.launch_game==2?14:0):a.launch_game?13:0;
#endif
}
