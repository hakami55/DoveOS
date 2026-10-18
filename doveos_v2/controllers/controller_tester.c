#define _POSIX_C_SOURCE 200809L
#include "doveos.h"
#include "dove_ui_layout.h"
#include <linux/input.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#define WORD_BITS (8U * sizeof(unsigned long))
#define WORDS(n) (((n) + WORD_BITS - 1) / WORD_BITS)
static int bit(const unsigned long *b, unsigned n) { return (b[n / WORD_BITS] >> (n % WORD_BITS)) & 1UL; }
typedef struct {
    int code, minimum, maximum;
    GtkWidget *bar;
} Axis;
static GtkWidget *window, *devices, *status;
static GtkListStore *buttons;
static Axis axes[ABS_CNT];
static int axis_count, fd = -1;
static guint timer;
static unsigned ticks;
static gboolean dropped;
static GtkWidget *axis_box;
static char selected_path[64];
static int rumble_effect = -1;
static guint rumble_timer;

static gboolean is_controller(int handle) {
    unsigned long ev[WORDS(EV_CNT)] = {0}, abs[WORDS(ABS_CNT)] = {0}, keys[WORDS(KEY_CNT)] = {0};
    if (ioctl(handle, EVIOCGBIT(0, sizeof(ev)), ev) < 0 || !bit(ev, EV_ABS)) return FALSE;
    ioctl(handle, EVIOCGBIT(EV_ABS, sizeof(abs)), abs);
    ioctl(handle, EVIOCGBIT(EV_KEY, sizeof(keys)), keys);
    gboolean button = FALSE;
    for (unsigned i = BTN_JOYSTICK; i < BTN_DIGI; i++) if (bit(keys, i)) button = TRUE;
    return button && (bit(abs, ABS_X) || bit(abs, ABS_RX) || bit(abs, ABS_HAT0X));
}
static const char *axis_name(int code) {
    switch (code) {
    case ABS_X: return "X / left stick"; case ABS_Y: return "Y / left stick";
    case ABS_RX: return "RX / right stick"; case ABS_RY: return "RY / right stick";
    case ABS_Z: return "Z / trigger"; case ABS_RZ: return "RZ / trigger";
    case ABS_HAT0X: return "D-pad X"; case ABS_HAT0Y: return "D-pad Y";
    default: return "Axis";
    }
}
static const char *button_name(int code) {
    switch (code) {
    case BTN_SOUTH: return "South / A"; case BTN_EAST: return "East / B";
    case BTN_NORTH: return "X (Linux BTN_X)"; case BTN_WEST: return "Y (Linux BTN_Y)";
    case BTN_TL: return "Left shoulder"; case BTN_TR: return "Right shoulder";
    case BTN_TL2: return "Left trigger button"; case BTN_TR2: return "Right trigger button";
    case BTN_SELECT: return "Select"; case BTN_START: return "Start";
    case BTN_MODE: return "Guide"; case BTN_THUMBL: return "Left stick click";
    case BTN_THUMBR: return "Right stick click"; default: return "Button";
    }
}
static void disconnect_device(void) {
    if (rumble_timer) { g_source_remove(rumble_timer); rumble_timer = 0; }
    if (fd >= 0 && rumble_effect >= 0) ioctl(fd, EVIOCRMFF, rumble_effect);
    rumble_effect = -1;
    if (fd >= 0) close(fd);
    fd = -1; axis_count = 0; dropped = FALSE;
    GList *children = gtk_container_get_children(GTK_CONTAINER(axis_box));
    for (GList *it = children; it; it = it->next) gtk_widget_destroy(GTK_WIDGET(it->data));
    g_list_free(children);
    gtk_list_store_clear(buttons);
}
static void update_axis(int code, int value) {
    for (int i = 0; i < axis_count; i++) if (axes[i].code == code) {
        Axis *a = &axes[i];
        double fraction = a->maximum > a->minimum ? ((double)value - a->minimum) / ((double)a->maximum - a->minimum) : 0;
        char text[128];
        g_snprintf(text, sizeof(text), "%s [%d]: %d (range %d..%d)", axis_name(code), code, value, a->minimum, a->maximum);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->bar), CLAMP(fraction, 0.0, 1.0));
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(a->bar), text);
        return;
    }
}
static void update_button(int code, int value) {
    GtkTreeIter row;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(buttons), &row);
    while (valid) {
        int found;
        gtk_tree_model_get(GTK_TREE_MODEL(buttons), &row, 0, &found, -1);
        if (found == code) { gtk_list_store_set(buttons, &row, 2, value ? "PRESSED" : "Released", -1); return; }
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(buttons), &row);
    }
}
static void sync_state(void) {
    for (int i = 0; i < axis_count; i++) {
        struct input_absinfo info;
        if (ioctl(fd, EVIOCGABS(axes[i].code), &info) >= 0) update_axis(axes[i].code, info.value);
    }
    unsigned long down[WORDS(KEY_CNT)] = {0};
    if (ioctl(fd, EVIOCGKEY(sizeof(down)), down) >= 0) {
        GtkTreeIter row;
        gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(buttons), &row);
        while (valid) {
            int code;
            gtk_tree_model_get(GTK_TREE_MODEL(buttons), &row, 0, &code, -1);
            update_button(code, bit(down, (unsigned)code));
            valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(buttons), &row);
        }
    }
}
static void select_device(GtkComboBox *combo, gpointer data) {
    (void)data;
    disconnect_device();
    char *text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
    if (!text) { gtk_label_set_text(GTK_LABEL(status), "No controller selected. Connect USB or pair Bluetooth, then Refresh."); return; }
    if (sscanf(text, "%63s", selected_path) != 1) { g_free(text); return; }
    g_free(text);
    fd = open(selected_path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) fd = open(selected_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { gtk_label_set_text(GTK_LABEL(status), strerror(errno)); return; }
    if (!is_controller(fd)) { disconnect_device(); gtk_label_set_text(GTK_LABEL(status), "Device is no longer a controller."); return; }
    unsigned long abits[WORDS(ABS_CNT)] = {0}, kbits[WORDS(KEY_CNT)] = {0};
    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abits)), abits);
    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(kbits)), kbits);
    for (int code = 0; code < ABS_CNT; code++) if (bit(abits, (unsigned)code)) {
        struct input_absinfo info;
        if (ioctl(fd, EVIOCGABS(code), &info) < 0) continue;
        Axis *a = &axes[axis_count++];
        a->code = code; a->minimum = info.minimum; a->maximum = info.maximum;
        a->bar = dove_progressbar(0); dove_add(axis_box, a->bar);
    }
    for (int code = BTN_MISC; code < KEY_CNT; code++) if (bit(kbits, (unsigned)code)) {
        GtkTreeIter row; char name[96];
        g_snprintf(name, sizeof(name), "%s (%d)", button_name(code), code);
        gtk_list_store_append(buttons, &row);
        gtk_list_store_set(buttons, &row, 0, code, 1, name, 2, "Released", -1);
    }
    sync_state(); gtk_widget_show_all(axis_box);
    gtk_label_set_text(GTK_LABEL(status), "Reading controller. Use Rumble (1 sec) to test vibration.");
}
static void refresh(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    disconnect_device();
    gtk_list_store_clear(GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(devices))));
    int count = 0, denied = 0;
    for (int i = 0; i < 128; i++) {
        char path[64], name[160] = "Unnamed controller", text[240];
        g_snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int handle = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (handle < 0) { if (errno == EACCES) denied++; continue; }
        if (is_controller(handle)) {
            ioctl(handle, EVIOCGNAME(sizeof(name)), name); name[sizeof(name)-1] = 0;
            g_snprintf(text, sizeof(text), "%s  %s", path, name);
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(devices), text); count++;
        }
        close(handle);
    }
    if (count) gtk_combo_box_set_active(GTK_COMBO_BOX(devices), 0);
    else gtk_label_set_text(GTK_LABEL(status), denied ? "Input access denied. Use the DoveOS session's existing input permissions; do not chmod all devices." : "No gamepad detected. Connect USB or pair Bluetooth, then Refresh.");
}
static gboolean poll_input(gpointer data) {
    (void)data;
    if (fd >= 0) {
        struct input_event events[32];
        for (int batch = 0; batch < 8; batch++) {
            ssize_t n = read(fd, events, sizeof(events));
            if (n < 0 && (errno == EAGAIN || errno == EINTR)) break;
            if (n <= 0) { disconnect_device(); gtk_label_set_text(GTK_LABEL(status), "Controller disconnected. Reconnect and Refresh."); break; }
            for (size_t i = 0; i < (size_t)n / sizeof(events[0]); i++) {
                if (events[i].type == EV_SYN && events[i].code == SYN_DROPPED) { dropped = TRUE; continue; }
                if (dropped) {
                    if (events[i].type == EV_SYN && events[i].code == SYN_REPORT) { dropped = FALSE; sync_state(); }
                    continue;
                }
                if (events[i].type == EV_ABS) update_axis(events[i].code, events[i].value);
                else if (events[i].type == EV_KEY) update_button(events[i].code, events[i].value);
            }
        }
    }
    if (++ticks == 1) g_print("CONTROLLER_TESTER_READY\n");
    if (fd < 0 && ticks % 100 == 0) refresh(NULL, NULL);
    return TRUE;
}
static void finish(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    if (timer) dove_timer_stop(timer);
    disconnect_device(); gtk_main_quit();
}
static void test_rumble(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    if (fd < 0) { gtk_label_set_text(GTK_LABEL(status), "Select a controller first."); return; }
    struct ff_effect effect; memset(&effect, 0, sizeof(effect));
    effect.type = FF_RUMBLE; effect.id = -1; effect.replay.length = 1000;
    effect.u.rumble.strong_magnitude = 20000;
    effect.u.rumble.weak_magnitude = 20000;
    if (ioctl(fd, EVIOCSFF, &effect) < 0) { gtk_label_set_text(GTK_LABEL(status), "This controller does not provide rumble."); return; }
    struct input_event event; memset(&event, 0, sizeof(event));
    event.type = EV_FF; event.code = effect.id; event.value = 1;
    if (write(fd, &event, sizeof(event)) != (ssize_t)sizeof(event)) {
        gtk_label_set_text(GTK_LABEL(status), "Could not start vibration.");
    } else gtk_label_set_text(GTK_LABEL(status), "One-second vibration test sent.");
    /* The backend stops at replay.length. Closing this evdev fd also removes
       owned effects; erase after expiry so repeated tests cannot exhaust slots. */
    rumble_effect = effect.id;
}
static gboolean finish_rumble(gpointer data) {
    (void)data;
    if (rumble_effect >= 0 && fd >= 0) ioctl(fd, EVIOCRMFF, rumble_effect);
    rumble_effect = -1; rumble_timer = 0;
    return FALSE;
}
static void rumble_clicked(GtkWidget *widget, gpointer data) {
    if (rumble_timer) return;
    test_rumble(widget, data);
    if (rumble_effect >= 0) rumble_timer = g_timeout_add(1200, finish_rumble, NULL);
}
int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    window = dove_form("Controller Tester", 660, 480);
    gtk_window_set_icon_from_file(GTK_WINDOW(window), "/media/usb0/DoveOS-v2/ClassicApps/icons/controller-48.png", NULL);
    GtkWidget *box = dove_panel(FALSE, 6); gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_container_add(GTK_CONTAINER(window), box);
    ui_header(box,"Controller Tester","Check sticks, triggers, buttons and vibration.","/media/usb0/DoveOS-v2/ClassicApps/icons/controller-48.png");
    GtkWidget *row = dove_panel(TRUE, 8); ui_pack(box, row);
    devices = gtk_combo_box_text_new(); gtk_box_pack_start(GTK_BOX(row), devices, TRUE, TRUE, 0);
    GtkWidget *reload = dove_button("Refresh"); dove_add(row, reload);
    GtkWidget *rumble = dove_button("Rumble (1 sec)"); dove_add(row, rumble);
    g_signal_connect(rumble, "clicked", G_CALLBACK(rumble_clicked), NULL);
    GtkWidget *body = dove_panel(TRUE, 10); gtk_box_pack_start(GTK_BOX(box), body, TRUE, TRUE, 0);
    axis_box = dove_panel(FALSE, 2);
    GtkWidget *axis_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(axis_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(axis_scroll), axis_box);
    gtk_widget_set_size_request(axis_scroll, 330, 260); GtkWidget *axis_frame=gtk_frame_new("Sticks & triggers");gtk_container_add(GTK_CONTAINER(axis_frame),axis_scroll);gtk_box_pack_start(GTK_BOX(body), axis_frame, TRUE, TRUE, 0);
    buttons = gtk_list_store_new(3, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget *view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(buttons));
    for (int col = 1; col <= 2; col++) gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, col == 1 ? "Button" : "State", gtk_cell_renderer_text_new(), "text", col, NULL);
    GtkWidget *button_frame=gtk_frame_new("Buttons & D-pad");gtk_container_add(GTK_CONTAINER(button_frame),dove_scrolled(view,230,260));gtk_box_pack_start(GTK_BOX(body),button_frame,TRUE,TRUE,0);
    status=ui_status(box,"Connect a USB controller or pair one with Bluetooth.");
    gtk_widget_set_tooltip_text(rumble,"Sends one second of vibration when supported by the selected controller.");
    g_signal_connect(devices, "changed", G_CALLBACK(select_device), NULL);
    g_signal_connect(reload, "clicked", G_CALLBACK(refresh), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(finish), NULL);
    gtk_widget_show_all(window); refresh(NULL, NULL);
    timer = dove_timer(20, poll_input, NULL); gtk_main();
    g_object_unref(buttons); return 0;
}
