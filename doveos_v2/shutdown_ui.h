#ifndef DOVEOS_SHUTDOWN_UI_H
#define DOVEOS_SHUTDOWN_UI_H
#include <X11/Xatom.h>

/* Ask applications to close through their save-aware window protocol.
 * Never turn a missing WM_DELETE_WINDOW handler into XKillClient. */
static unsigned shutdown_windows(Display *d, int request, GString *names) {
    Atom actual; int format; unsigned long count=0, left=0;
    unsigned char *data=NULL;
    Atom clients=XInternAtom(d,"_NET_CLIENT_LIST",False);
    Atom protocols=XInternAtom(d,"WM_PROTOCOLS",False);
    Atom close_atom=XInternAtom(d,"WM_DELETE_WINDOW",False);
    unsigned pending=0;
    gdk_error_trap_push();
    if(XGetWindowProperty(d,DefaultRootWindow(d),clients,0,4096,False,
            XA_WINDOW,&actual,&format,&count,&left,&data)!=Success || format!=32)
        count=0;
    for(unsigned long i=0;i<count;i++) {
        Window w=((Window *)data)[i];
        unsigned char *value=NULL; unsigned long n=0, extra=0;
        Atom type=XInternAtom(d,"_NET_WM_WINDOW_TYPE",False);
        int skip=0;
        if(XGetWindowProperty(d,w,type,0,16,False,XA_ATOM,&actual,&format,
                &n,&extra,&value)==Success && format==32) {
            for(unsigned long j=0;j<n;j++) {
                Atom a=((Atom *)value)[j];
                if(a==XInternAtom(d,"_NET_WM_WINDOW_TYPE_DESKTOP",False) ||
                   a==XInternAtom(d,"_NET_WM_WINDOW_TYPE_DOCK",False))skip=1;
            }
        }
        if(value)XFree(value);
        value=NULL;n=0;
        if(XGetWindowProperty(d,w,XInternAtom(d,"_NET_WM_PID",False),0,1,
                False,XA_CARDINAL,&actual,&format,&n,&extra,&value)==Success &&
                format==32 && n && *(unsigned long *)value==(unsigned long)getpid())skip=1;
        if(value)XFree(value);
        if(skip)continue;
        pending++;
        if(names && pending<=4) {
            char *title=NULL;
            if(XFetchName(d,w,&title) && title && g_utf8_validate(title,-1,NULL))
                g_string_append_printf(names,"\n\342\200\242 %.100s",title);
            else g_string_append(names,"\n\342\200\242 Application window");
            if(title)XFree(title);
        }
        if(request) {
            Atom *supported=NULL;int length=0, accepts=0;
            if(XGetWMProtocols(d,w,&supported,&length)) {
                for(int j=0;j<length;j++)if(supported[j]==close_atom)accepts=1;
                XFree(supported);
            }
            if(accepts) {
                XEvent event;memset(&event,0,sizeof event);
                event.xclient.type=ClientMessage;event.xclient.window=w;
                event.xclient.message_type=protocols;event.xclient.format=32;
                event.xclient.data.l[0]=(long)close_atom;
                event.xclient.data.l[1]=CurrentTime;
                XSendEvent(d,w,False,NoEventMask,&event);
            }
        }
    }
    if(data)XFree(data);
    XSync(d,False);(void)gdk_error_trap_pop();
    return pending;
}
typedef struct { GtkWidget *dialog,*label; Display *display; } ShutdownWait;
static gboolean shutdown_poll(gpointer pointer) {
    ShutdownWait *wait=pointer;
    GString *text=g_string_new("Finish saving or close these applications.\n"
                              "DoveOS waits until they close:");
    unsigned count=shutdown_windows(wait->display,0,text);
    if(count>4)g_string_append_printf(text,"\n…and %u more.",count-4);
    if(!count)gtk_dialog_response(GTK_DIALOG(wait->dialog),GTK_RESPONSE_OK);
    else gtk_label_set_text(GTK_LABEL(wait->label),text->str);
    g_string_free(text,TRUE);
    return TRUE;
}
static int prepare_shutdown(void) {
    Display *d=gdk_x11_get_default_xdisplay();
    if(!d)return 0;
    GtkWidget *dialog=gtk_dialog_new_with_buttons("Closing applications - DoveOS",NULL,0,
            "Cancel",GTK_RESPONSE_CANCEL,NULL);
    gtk_window_set_wmclass(GTK_WINDOW(dialog),"DoveShutdown","DoveShutdown");
    gtk_window_set_position(GTK_WINDOW(dialog),GTK_WIN_POS_CENTER);
    gtk_window_set_resizable(GTK_WINDOW(dialog),FALSE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_CANCEL);
    GtkWidget *content=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content),16);
    GtkWidget *label=gtk_label_new("Asking applications to close...");
    gtk_label_set_line_wrap(GTK_LABEL(label),TRUE);
    gtk_box_pack_start(GTK_BOX(content),label,FALSE,FALSE,0);
    gtk_widget_show_all(dialog);
    shutdown_windows(d,1,NULL);
    ShutdownWait wait={dialog,label,d};
    guint timer=g_timeout_add(500,shutdown_poll,&wait);
    int response=gtk_dialog_run(GTK_DIALOG(dialog));
    g_source_remove(timer);gtk_widget_destroy(dialog);
    return response==GTK_RESPONSE_OK;
}
#endif
