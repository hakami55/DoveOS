#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t running=1;
static void stop(int sig) { (void)sig; running=0; }
static int ignore_x_error(Display *d,XErrorEvent *e) { (void)d;(void)e;return 0; }

static int vlc_visible(Display *d,Window parent,int depth) {
    Window root,up,*children=NULL;
    unsigned count=0;
    int found=0;
    if(!XQueryTree(d,parent,&root,&up,&children,&count))return 0;
    for(unsigned i=0;i<count && !found;i++) {
        XClassHint hint={0};
        XWindowAttributes attr;
        if(XGetWindowAttributes(d,children[i],&attr) && attr.map_state==IsViewable &&
           XGetClassHint(d,children[i],&hint)) {
            found=(hint.res_class && !strcasecmp(hint.res_class,"vlc")) ||
                  (hint.res_name && !strcasecmp(hint.res_name,"vlc"));
            if(hint.res_name)XFree(hint.res_name);
            if(hint.res_class)XFree(hint.res_class);
        }
        if(!found && depth>0)found=vlc_visible(d,children[i],depth-1);
    }
    if(children)XFree(children);
    return found;
}

int main(int argc,char **argv) {
    if(argc!=2)return 2;
    pid_t app=(pid_t)strtol(argv[1],NULL,10);
    if(app<=0)return 2;
    Display *d=XOpenDisplay(NULL);
    if(!d)return 1;
    XSetErrorHandler(ignore_x_error);
    Window root=DefaultRootWindow(d);
    if(vlc_visible(d,root,2)){XCloseDisplay(d);return 0;}
    signal(SIGTERM,stop);signal(SIGINT,stop);
    FILE *marker=fopen("/tmp/doveos-vlc-opening","w");
    if(marker){fprintf(marker,"%ld\n",(long)app);fclose(marker);}
    puts("VLC_BUSY_POINTER_SHOWN");fflush(stdout);
    struct timespec pause={0,200000000};
    for(int n=0;n<300 && running && kill(app,0)==0;n++) {
        if(vlc_visible(d,root,2))break;
        nanosleep(&pause,NULL);
    }
    unlink("/tmp/doveos-vlc-opening");
    puts("VLC_POINTER_RESTORED");fflush(stdout);
    XCloseDisplay(d);
    return 0;
}
