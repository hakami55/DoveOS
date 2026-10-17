#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <time.h>

static void pause_ms(long ms) {
    struct timespec value={ms/1000,(ms%1000)*1000000};
    nanosleep(&value,NULL);
}
int main(void) {
    Display *display=XOpenDisplay(":7");
    if(!display)return 1;
    XTestFakeMotionEvent(display,-1,175,35,CurrentTime);
    for(int click=0;click<2;click++) {
        XTestFakeButtonEvent(display,1,True,CurrentTime);
        XTestFakeButtonEvent(display,1,False,CurrentTime);
        XFlush(display);pause_ms(120);
    }
    XCloseDisplay(display);
    puts("SETTINGS_ICON_DOUBLE_CLICKED");
    return 0;
}
