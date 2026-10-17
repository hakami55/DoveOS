#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "settings_icons.h"

#define W 900
#define H 580
#define USB "/media/usb0/DoveOS-v2"
#define SCROLL_FILE USB "/home/scroll-direction.txt"

typedef struct _XftDraw XftDraw;
typedef struct _XftFont XftFont;
typedef struct {unsigned short red, green, blue, alpha;} XRenderColorLocal;
typedef struct {unsigned long pixel; XRenderColorLocal color;} XftColor;
extern XftDraw *XftDrawCreate(Display *, Drawable, Visual *, Colormap);
extern void XftDrawDestroy(XftDraw *);
extern XftFont *XftFontOpenName(Display *, int, const char *);
extern void XftDrawStringUtf8(XftDraw *, const XftColor *, XftFont *, int, int, const unsigned char *, int);

static Display *d;
static Window win;
static GC gc;
static XftDraw *draw;
static XftFont *regular, *small, *heading;
static XftColor ink, muted, white, blue, soft;
static unsigned long bg, sidebar, card, line, accent;
static Pixmap panel_icons[6];
static const unsigned char *panel_pixels[]={panel_icon_0,panel_icon_1,panel_icon_2,panel_icon_3,panel_icon_4,panel_icon_5};
static int page, subpage, selected, volume, saved_volume = 70, password_edit;
static char password[65], chosen_ssid[64], message[240], wifi_names[16][64];
static int wifi_count, bt_count, bt_selected;
static struct {char addr[18], name[90]; int le;} bt[16];
static pid_t child;
static char pending_action[40];

static unsigned long color(const char *name) {
    XColor got, exact;
    if (!XAllocNamedColor(d, DefaultColormap(d, DefaultScreen(d)), name, &got, &exact)) return 0;
    return got.pixel;
}
static void make_xft(const char *name, XftColor *out) {
    XColor got, exact;
    if (XAllocNamedColor(d, DefaultColormap(d, DefaultScreen(d)), name, &got, &exact)) {
        out->pixel=got.pixel;
        out->color.red=got.red;out->color.green=got.green;out->color.blue=got.blue;
        out->color.alpha=65535;
    } else memset(out,0,sizeof *out);
}
static void fill(int x, int y, int w, int h, unsigned long c) {
    XSetForeground(d, gc, c); XFillRectangle(d, win, gc, x, y, (unsigned)w, (unsigned)h);
}
static void stroke(int x, int y, int w, int h, unsigned long c) {
    XSetForeground(d, gc, c); XDrawRectangle(d, win, gc, x, y, (unsigned)w, (unsigned)h);
}
static void text(int x, int y, const char *s, XftFont *font, XftColor *c) {
    XftDrawStringUtf8(draw, c, font, x, y, (const unsigned char *)s, (int)strlen(s));
}
static void read_line(const char *path, char *out, size_t cap) {
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(out, (int)cap, f)) out[strcspn(out, "\r\n")] = 0;
    fclose(f);
}
static int read_number(const char *path, int fallback) {
    char s[32]; read_line(path, s, sizeof s);
    if (!s[0]) return fallback;
    for (size_t i=0; s[i]; i++) if (!isdigit((unsigned char)s[i])) return fallback;
    return atoi(s);
}
static int link_up(const char *interface) {
    char path[128], value[24];
    snprintf(path, sizeof path, "/sys/class/net/%s/carrier", interface);
    read_line(path, value, sizeof value);
    return !strcmp(value, "1");
}
static void battery(char *out, size_t cap) {
    DIR *dir = opendir("/sys/class/power_supply");
    if (!dir) {snprintf(out, cap, "Battery information unavailable"); return;}
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        char path[384], type[32], state[48];
        snprintf(path, sizeof path, "/sys/class/power_supply/%s/type", entry->d_name);
        read_line(path, type, sizeof type);
        if (strcmp(type, "Battery")) continue;
        snprintf(path, sizeof path, "/sys/class/power_supply/%s/capacity", entry->d_name);
        int percent = read_number(path, -1);
        snprintf(path, sizeof path, "/sys/class/power_supply/%s/status", entry->d_name);
        read_line(path, state, sizeof state);
        if (percent >= 0 && percent <= 100)
            snprintf(out, cap, "Battery %d%%  %s", percent, state[0] ? state : "status unknown");
        else snprintf(out, cap, "Battery percentage unavailable");
        closedir(dir); return;
    }
    closedir(dir);
    snprintf(out, cap, "Battery information unavailable");
}
static int reverse_scroll(void) {
    char s[24]; read_line(SCROLL_FILE, s, sizeof s);
    return strcmp(s, "normal") != 0;
}
static void set_reverse_scroll(void) {
    const char *state = reverse_scroll() ? "normal\n" : "reverse\n";
    FILE *f = fopen(SCROLL_FILE ".next", "w");
    if (!f) {snprintf(message, sizeof message, "Could not save touchpad setting."); return;}
    int wrote=fputs(state, f);
    int closed=fclose(f);
    if (wrote < 0 || closed || rename(SCROLL_FILE ".next", SCROLL_FILE))
        snprintf(message, sizeof message, "Could not save touchpad setting.");
    else snprintf(message, sizeof message, "Two-finger scroll direction changed.");
}
static void load_wifi(void) {
    wifi_count = 0;
    FILE *f = fopen("/tmp/doveos-wifi/scan", "r");
    if (!f) return;
    while (wifi_count < 16 && fgets(wifi_names[wifi_count], sizeof wifi_names[0], f)) {
        wifi_names[wifi_count][strcspn(wifi_names[wifi_count], "\r\n")] = 0;
        if (wifi_names[wifi_count][0]) wifi_count++;
    }
    fclose(f);
}
static void load_bt_file(const char *path, int le) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char s[160];
    while (bt_count < 16 && fgets(s, sizeof s, f)) {
        if (strlen(s) < 17 || s[2] != ':' || s[5] != ':' || s[8] != ':') continue;
        int duplicate = 0;
        for (int i=0;i<bt_count;i++) if (!strncasecmp(bt[i].addr, s, 17)) duplicate = 1;
        if (duplicate) continue;
        memcpy(bt[bt_count].addr, s, 17); bt[bt_count].addr[17] = 0;
        char *name = s + 17;
        while (*name == ' ' || *name == '\t') name++;
        name[strcspn(name, "\r\n")] = 0;
        snprintf(bt[bt_count].name, sizeof bt[bt_count].name, "%s", *name ? name : "Unnamed device");
        bt[bt_count].le = le; bt_count++;
    }
    fclose(f);
}
static void load_bt(void) {
    bt_count = bt_selected = 0;
    load_bt_file("/tmp/doveos-bluetooth/le-scan.log", 1);
    load_bt_file("/tmp/doveos-bluetooth/classic-scan.log", 0);
}
static void run_action(const char *action, const char *argument, const char *secret) {
    if (child > 0) {snprintf(message, sizeof message, "Wait for the current action to finish."); return;}
    int pipefd[2] = {-1,-1};
    if (secret && pipe(pipefd)) {snprintf(message,sizeof message,"Could not send password."); return;}
    unlink("/tmp/drmosa-settings-result");
    snprintf(pending_action, sizeof pending_action, "%s", action);
    child = fork();
    if (child == 0) {
        if (secret) {close(pipefd[1]); dup2(pipefd[0], STDIN_FILENO); close(pipefd[0]);}
        execl("/bin/sh", "sh", "/usr/bin/doveos-settings-proxy", action, argument ? argument : "", (char *)NULL);
        _exit(127);
    }
    if (secret) {
        close(pipefd[0]);
        if (child > 0) {
            ssize_t sent=write(pipefd[1], secret, strlen(secret));
            if (sent==(ssize_t)strlen(secret)) sent=write(pipefd[1], "\n", 1);
            (void)sent;
        }
        close(pipefd[1]);
    }
    if (child < 0) {child = 0; snprintf(message, sizeof message, "Could not start action.");}
    else snprintf(message, sizeof message, "Working: %s...", action);
}
static void poll_action(void) {
    if (child <= 0) return;
    int status;
    pid_t ended = waitpid(child, &status, WNOHANG);
    if (ended == 0) return;
    child = 0;
    const char *result = !strncmp(pending_action, "bluetooth-", 10) ?
        "/tmp/doveos-bluetooth/result" : "/tmp/drmosa-settings-result";
    read_line(result, message, sizeof message);
    if (!message[0]) snprintf(message, sizeof message, "Action finished; check the device status.");
    if (!strcmp(pending_action, "wifi-scan")) {load_wifi(); subpage = 1; selected = 0;}
    if (!strcmp(pending_action, "bluetooth-scan")) {load_bt(); subpage = 2; selected = 0;}
    volume = read_number("/media/usb0/DrMoSa/sound-volume.txt", volume);
    (void)status;
}
static void row(int index, int y, const char *title, const char *detail, const char *action) {
    int x = 240, w = 625, h = 53;
    fill(x,y,w,h,index == selected ? color("#DCDCDC") : card);
    stroke(x,y,w,h,index == selected ? accent : line);
    text(x+17,y+23,title,regular,&ink);
    if (detail && *detail) text(x+17,y+43,detail,small,&muted);
    if (action && *action) text(x+w-110,y+31,action,small,&blue);
}
static void bevel(int x,int y,int w,int h,int pressed) {
    unsigned long light=color("#FFFFFF"),dark=color("#808080");
    XSetForeground(d,gc,pressed?dark:light);
    XDrawLine(d,win,gc,x,y,x+w,y); XDrawLine(d,win,gc,x,y,x,y+h);
    XSetForeground(d,gc,pressed?light:dark);
    XDrawLine(d,win,gc,x,y+h,x+w,y+h); XDrawLine(d,win,gc,x+w,y,x+w,y+h);
}
static void panel_home(void) {
    static const char *names[]={"Network","Bluetooth","Display","Sound","Mouse & Keyboard","System"};
    fill(0,0,W,H,bg);
    text(18,28,"DoveOS Settings",small,&ink);bevel(8,38,W-17,H-77,1);
    text(30,77,"Control Panel",heading,&ink);
    text(30,103,"Double-click an icon to change its settings.",small,&muted);
    for(int i=0;i<6;i++) {
        int x=40+(i%3)*285,y=140+(i/3)*160;
        if(i==selected) {fill(x,y,250,128,color("#D8D8D8"));stroke(x,y,250,128,line);}
        XCopyArea(d,panel_icons[i],win,gc,0,0,32,32,x+109,y+21);
        int tx=x+125-(int)strlen(names[i])*4;
        if(i==selected)fill(tx-5,y+70,(int)strlen(names[i])*8+10,26,accent);
        text(tx,y+88,names[i],regular,i==selected?&white:&ink);
    }
    bevel(8,H-31,W-17,23,1);text(18,H-13,"6 items   |   DoveOS Control Panel",small,&ink);XFlush(d);
}
static void render(void) {
    static const char *pages[]={"Overview","Network","Bluetooth","Display","Sound","Controls","About"};
    if(page==0) {panel_home();return;}
    fill(0,0,W,H,bg); fill(0,0,208,H,sidebar);
    fill(0,0,208,74,bg);bevel(8,8,190,56,0);
    text(22,44,"Control Panel",regular,&ink);
    for (int i=0;i<7;i++) {
        int y=104+i*55;
        if (i==page) fill(8,y-26,192,42,accent);
        text(24,y,pages[i],regular,i==page?&white:&ink);
    }
    text(240,58,pages[page],heading,&ink);
    text(240,84,"Settings for this Lap Dock",small,&muted);
    int y=115;
    char detail[220], battery_info[100];
    if (page==0) {
        row(0,y,"Network",link_up("axdebug0")?"Ethernet connected":"Ethernet disconnected","Open"); y+=66;
        read_line("/tmp/doveos-wifi/link-status",detail,sizeof detail);
        row(1,y,"Wi-Fi",detail[0]?detail:"No active Wi-Fi connection","Open"); y+=66;
        snprintf(detail,sizeof detail,"Dock speaker volume: %d%%",volume);
        row(2,y,"Sound",detail,"Open"); y+=66;
        battery(battery_info,sizeof battery_info);
        row(3,y,"Battery",battery_info,NULL);
    } else if (page==1 && subpage==1) {
        row(0,y,"Back to network",NULL,"Back"); y+=62;
        for (int i=0;i<wifi_count && i<5;i++,y+=62) row(i+1,y,wifi_names[i],"Enter password to connect","Join");
        if (!wifi_count) text(250,205,"No networks found. Try Scan again.",regular,&muted);
    } else if (page==1) {
        row(0,y,"Ethernet",link_up("axdebug0")?"Connected to PC":"Disconnected","Toggle"); y+=62;
        read_line("/tmp/doveos-wifi/link-status",detail,sizeof detail);
        row(1,y,"Wi-Fi status",detail[0]?detail:"Not connected","Refresh"); y+=62;
        row(2,y,"Find Wi-Fi networks","Search nearby access points","Scan"); y+=62;
        row(3,y,"Reconnect Wi-Fi","Use saved network details","Connect"); y+=62;
        row(4,y,"Disconnect Wi-Fi",NULL,"Disconnect");
    } else if (page==2 && subpage==2) {
        row(0,y,"Back to Bluetooth",NULL,"Back"); y+=62;
        for (int i=0;i<bt_count && i<5;i++,y+=62) {
            snprintf(detail,sizeof detail,"%.17s  %s",bt[i].addr,bt[i].le?"LE":"Classic");
            row(i+1,y,bt[i].name,detail,"Pair");
        }
        if (!bt_count) text(250,205,"No devices found. Put one in pairing mode.",regular,&muted);
        if (bt_count) text(250,520,"Select a device to pair; press C to reconnect it.",small,&muted);
    } else if (page==2) {
        row(0,y,"Bluetooth controller","Initialize the built-in radio","Start"); y+=70;
        row(1,y,"Find devices","The scan can take about 20 seconds","Scan"); y+=80;
        text(250,y,"Pairing depends on the device and dock driver.",regular,&muted);
    } else if (page==3) {
        row(0,y,"Screen brightness","Use the dock's Fn brightness keys","Hardware"); y+=70;
        char state[8]; read_line("/proc/1/root/usr/local/etc/drmosa-ethernet/keepawake-enabled",state,sizeof state);
        row(1,y,"Keep screen awake",!strcmp(state,"1")?"Enabled with Ethernet":"Off or unavailable","Toggle"); y+=77;
        text(250,y,"DoveOS stayed on beyond the old seven-minute idle point.",regular,&muted);
        text(250,y+27,"Battery and brightness levels need exposed hardware controls.",small,&muted);
    } else if (page==4) {
        snprintf(detail,sizeof detail,"Current dock volume: %d%%",volume);
        row(0,y,"Lower volume",detail,"-10%"); y+=70;
        row(1,y,"Raise volume",detail,"+10%"); y+=70;
        row(2,y,volume?"Mute speakers":"Unmute speakers",NULL,"Toggle"); y+=82;
        text(250,y,"Fn volume keys are supported when the dock sends key events.",small,&muted);
    } else if (page==5) {
        row(0,y,"Two-finger scrolling",reverse_scroll()?"Reversed direction":"Original direction","Toggle"); y+=76;
        text(250,y,"One-finger movement and touchpad clicks are unchanged.",regular,&muted); y+=56;
        text(250,y,"Alt+Tab switches windows. Alt+F4 closes a window.",small,&muted); y+=30;
        text(250,y,"Ctrl+Alt+Shift+Q ends a Moonlight stream.",small,&muted);
    } else {
        text(250,150,"DoveOS v2  •  HP Elite x3 Lap Dock",regular,&ink);
        text(250,195,"USB-based desktop using the dock's existing Linux kernel.",small,&muted);
        text(250,230,"The original firmware remains available for recovery.",small,&muted);
    }
    fill(208,546,692,34,bg);bevel(214,550,680,23,1);
    text(225,568,message[0]?message:"Choose a setting on the left.",small,&ink);
    if (password_edit) {
        fill(224,165,650,210,color("#FFFFFF")); stroke(224,165,650,210,accent);
        text(245,204,"Join Wi-Fi network",heading,&ink);
        text(245,245,chosen_ssid,regular,&ink);
        char mask[66]; size_t len=strlen(password); memset(mask,'*',len);mask[len]=0;
        fill(245,266,595,48,color("#F1F5FA"));stroke(245,266,595,48,line);
        text(259,297,mask,regular,&ink);
        text(245,347,"Enter: connect     Esc: cancel",small,&muted);
    }
    XFlush(d);
}
static void set_page(int n) {page=n; subpage=0; selected=0; password_edit=0;}
static void activate(void) {
    if (password_edit) return;
    if (page==0) {set_page(selected+1);return;}
    if (page==1 && subpage==1) {
        if (!selected) {subpage=0;selected=2;return;}
        if (selected<=wifi_count) {snprintf(chosen_ssid,sizeof chosen_ssid,"%s",wifi_names[selected-1]);password[0]=0;password_edit=1;}
        return;
    }
    if (page==1) {
        static const char *actions[]={"ethernet-toggle","wifi-status","wifi-scan","wifi-reconnect","wifi-disconnect"};
        if (selected>=0&&selected<5) run_action(actions[selected],NULL,NULL);
    } else if (page==2 && subpage==2) {
        if (!selected) {subpage=0;selected=1;return;}
        if (selected<=bt_count) {bt_selected=selected-1;run_action(bt[bt_selected].le?"bluetooth-pair-le":"bluetooth-pair-classic",bt[bt_selected].addr,NULL);}
    } else if (page==2) {
        if (selected==0) run_action("bluetooth-start",NULL,NULL);
        else if (selected==1) run_action("bluetooth-scan",NULL,NULL);
    } else if (page==3) {
        if (selected==1) run_action("awake-toggle",NULL,NULL);
        else snprintf(message,sizeof message,"Brightness is controlled by the dock hardware keys.");
    } else if (page==4) {
        int next=volume;
        if (selected==0) next=volume-10;
        if (selected==1) next=volume+10;
        if (selected==2) {if (volume) {saved_volume=volume;next=0;} else next=saved_volume;}
        if (next<0) next=0;
        if (next>100) next=100;
        char value[8];snprintf(value,sizeof value,"%d",next);run_action("sound-set",value,NULL);
    } else if (page==5 && selected==0) set_reverse_scroll();
}
static int row_count(void) {
    if (page==0) return 6;
    if (page==1) return subpage==1 ? (wifi_count<5?wifi_count:5)+1 : 5;
    if (page==2) return subpage==2 ? (bt_count<5?bt_count:5)+1 : 2;
    if (page==3) return 2;
    if (page==4) return 3;
    if (page==5) return 1;
    return 0;
}
static void click(int x,int y,Time timestamp) {
    static Time previous;static int last_icon=-1;
    if(page==0) {
        for(int i=0;i<6;i++) {int gx=40+(i%3)*285,gy=140+(i/3)*160;
            if(x>=gx&&x<gx+250&&y>=gy&&y<gy+128) {selected=i;
                if(last_icon==i&&timestamp-previous<450) {activate();last_icon=-1;}
                else {last_icon=i;previous=timestamp;}return;}}return;
    }
    if (password_edit) return;
    if (x<208 && y>=78 && y<104+6*55+35) {
        int n=(y-78)/55;
        if (n>=0&&n<7) set_page(n);
    } else if (x>=240 && x<865 && y>=115 && y<525) {
        int step=page==0?66:page==1?62:page==2&&subpage==2?62:70;
        int n=(y-115)/step;
        if (n>=0&&n<row_count()) {selected=n;activate();}
    }
}
int main(int argc,char **argv) {
    d=XOpenDisplay(NULL);if (!d) return 1;
    int screen=DefaultScreen(d);
    bg=color("#C0C0C0");sidebar=color("#C0C0C0");card=color("#C0C0C0");
    line=color("#808080");accent=color("#000080");
    make_xft("#151515",&ink);make_xft("#505050",&muted);
    make_xft("#FFFFFF",&white);make_xft("#000080",&blue);
    make_xft("#DBE4F0",&soft);
    regular=XftFontOpenName(d,screen,"DejaVu Sans-10");
    small=XftFontOpenName(d,screen,"DejaVu Sans-10");
    heading=XftFontOpenName(d,screen,"DejaVu Sans:bold:pixelsize=19");
    if (!regular||!small||!heading) return 2;
    win=XCreateSimpleWindow(d,RootWindow(d,screen),150,70,W,H,0,0,bg);
    XStoreName(d,win,"Control Panel - DoveOS");
    XSizeHints size={0};size.flags=PMinSize|PMaxSize;size.min_width=size.max_width=W;size.min_height=size.max_height=H;XSetWMNormalHints(d,win,&size);
    XSelectInput(d,win,ExposureMask|ButtonPressMask|KeyPressMask|StructureNotifyMask);
    Atom close=XInternAtom(d,"WM_DELETE_WINDOW",False);XSetWMProtocols(d,win,&close,1);
    XMapWindow(d,win);gc=XCreateGC(d,win,0,NULL);
    draw=XftDrawCreate(d,win,DefaultVisual(d,screen),DefaultColormap(d,screen));
    for(int i=0;i<6;i++) {
        panel_icons[i]=XCreatePixmap(d,win,32,32,(unsigned)DefaultDepth(d,screen));
        XImage *image=XCreateImage(d,DefaultVisual(d,screen),(unsigned)DefaultDepth(d,screen),ZPixmap,0,calloc(32*32,4),32,32,32,0);
        if(!image)return 3;
        for(int y=0;y<32;y++)for(int x=0;x<32;x++){const unsigned char *p=panel_pixels[i]+(y*32+x)*3;XPutPixel(image,x,y,((unsigned long)p[0]<<16)|((unsigned long)p[1]<<8)|p[2]);}
        XPutImage(d,panel_icons[i],gc,image,0,0,0,0,32,32);XDestroyImage(image);
    }
    volume=read_number("/media/usb0/DrMoSa/sound-volume.txt",70);
    snprintf(message,sizeof message,"Changes are saved on the USB when supported.");
    if (argc>1) {if (!strcmp(argv[1],"network"))page=1;else if(!strcmp(argv[1],"bluetooth"))page=2;else if(!strcmp(argv[1],"display"))page=3;else if(!strcmp(argv[1],"sound"))page=4;}
    int done=0; time_t last=0;
    while (!done) {
        while (XPending(d)) {
            XEvent event;XNextEvent(d,&event);
            if (event.type==ClientMessage && (Atom)event.xclient.data.l[0]==close) done=1;
            else if (event.type==DestroyNotify) done=1;
            else if (event.type==ButtonPress) click(event.xbutton.x,event.xbutton.y,event.xbutton.time);
            else if (event.type==KeyPress) {
                KeySym key;char typed[32];int n=XLookupString(&event.xkey,typed,sizeof typed,&key,NULL);
                if (password_edit) {
                    size_t len=strlen(password);
                    if (key==XK_Escape) {memset(password,0,sizeof password);password_edit=0;}
                    else if (key==XK_BackSpace && len) password[len-1]=0;
                    else if (key==XK_Return && len>=8) {run_action("wifi-join",chosen_ssid,password);memset(password,0,sizeof password);password_edit=0;subpage=0;selected=1;}
                    else if (n==1 && (unsigned char)typed[0]>=32 && (unsigned char)typed[0]<127 && len<64) {password[len]=typed[0];password[len+1]=0;}
                } else if (key==XK_Escape) {if(subpage){subpage=0;selected=0;}else if(page)set_page(0);else done=1;}
                else if (key==XK_Left&&page==0&&selected>0) selected--;
                else if (key==XK_Right&&page==0&&selected<5) selected++;
                else if (key==XK_Up && selected>0) selected=page==0?(selected>=3?selected-3:selected):selected-1;
                else if (key==XK_Down && selected+1<row_count()) selected=page==0?(selected<3?selected+3:selected):selected+1;
                else if (key==XK_Return || key==XK_space) activate();
                else if ((key==XK_c||key==XK_C)&&page==2&&subpage==2&&bt_count&&selected>0) run_action("bluetooth-connect",bt[selected-1].addr,NULL);
            }
        }
        poll_action();
        time_t now=time(NULL);
        if (now!=last) {render();last=now;}
        struct timespec pause={0,60000000};nanosleep(&pause,NULL);
    }
    memset(password,0,sizeof password);
    for(int i=0;i<6;i++)XFreePixmap(d,panel_icons[i]);
    XftDrawDestroy(draw);XFreeGC(d,gc);XDestroyWindow(d,win);XCloseDisplay(d);
    return 0;
}
