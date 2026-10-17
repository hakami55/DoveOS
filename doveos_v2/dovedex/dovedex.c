#define _POSIX_C_SOURCE 200809L
#include "doveos.h"
#include <glib/gstdio.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>

static GtkWidget *window, *mode, *pages, *guide, *status, *start_button;
static GtkWidget *codec, *rate, *fps, *size, *audio, *audio_delay, *endpoint;
static GtkWidget *endpoint_row;
static GKeyFile *preferences;
static gchar *settings_path;
static const char *modes[] = {"USB", "Wi-Fi"};
static const char *codecs[] = {"H.264", "HEVC / H.265"};
static const char *sizes[] = {"1280 x 720", "1920 x 1080"};
static const char *groups[] = {"usb-adb", "wifi-adb"};
static int previous_mode;
static gboolean loading;
static GPid backend_pid;
static guint backend_timeout;
static gboolean backend_timed_out;
static GtkWidget *details,*open_button;
static GtkWidget *wifi_setup_button;
static gboolean preparing_wifi;
static guint backend_poll;
static int backend_out=-1;
static GString *backend_text;
static int test_result;
static const char *preview_path;

static int selected(void) { return MAX(0, gtk_combo_box_get_active(GTK_COMBO_BOX(mode))); }
static void notice(const char *text) { gtk_label_set_text(GTK_LABEL(status), text); }
static void set_guide(int choice) {
    const char *text = choice == 0 ?
        "USB connection\n\n"
        "1. Keep the DoveOS flash drive connected.\n"
        "2. On your Android device, enable Developer options and USB debugging.\n"
        "3. Connect a USB DATA cable to a free port on the RIGHT-side hub.\n"
        "4. Click Check USB and accept Allow USB debugging on the phone.\n"
        "5. Click Open USB / Wi-Fi. Ctrl+Alt+Shift+Q returns to DoveOS.\n\n"
        "A phone seen by USB is not yet authorized for ADB. Approval must happen on the unlocked phone."
        :
        "Wi-Fi connection\n\n"
        "1. Connect both devices to the same trusted 5 GHz Wi-Fi, or connect DoveOS to the phone hotspot.\n"
        "2. For first-time setup, connect an authorized USB data cable and click Set up Wi-Fi from USB.\n"
        "3. Click Check Wi-Fi. After it says connected and authorized, remove only the PHONE USB cable.\n"
        "4. Click Open USB / Wi-Fi. Quality settings include Forward sound through dock speakers.\n"
        "5. Ctrl+Alt+Shift+Q returns to DoveOS.\n\n"
        "The address and profile are saved. If the phone restarts or its address changes, repeat USB setup. Keep the DoveOS flash drive connected.\n\n"
        "An existing TCP ADB address can also be entered manually. Wireless debugging pairing-code/TLS ports are not supported by this backend.";
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(guide)), text, -1);
    gtk_button_set_label(GTK_BUTTON(start_button), choice == 0 ? "Check USB" : "Check Wi-Fi");
}
static void store_profile(int index) {
    const char *group = groups[index];
    g_key_file_set_integer(preferences, group, "codec", gtk_combo_box_get_active(GTK_COMBO_BOX(codec)));
    g_key_file_set_integer(preferences, group, "bitrate-mbps", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(rate)));
    g_key_file_set_integer(preferences, group, "fps-limit", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(fps)));
    g_key_file_set_integer(preferences, group, "resolution", gtk_combo_box_get_active(GTK_COMBO_BOX(size)));
    g_key_file_set_boolean(preferences, group, "audio", gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(audio)));
    g_key_file_set_integer(preferences,group,"audio-delay-ms",gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(audio_delay)));
    if (index == 1) g_key_file_set_string(preferences, group, "endpoint", gtk_entry_get_text(GTK_ENTRY(endpoint)));
}
static int read_int(const char *group, const char *key, int fallback, int minimum, int maximum) {
    GError *error = NULL;
    int value = g_key_file_get_integer(preferences, group, key, &error);
    if (error) { g_error_free(error); return fallback; }
    return CLAMP(value, minimum, maximum);
}
static void load_profile(int index) {
    loading = TRUE;
    const char *group = groups[index];
    gtk_combo_box_set_active(GTK_COMBO_BOX(codec), read_int(group, "codec", 1, 0, 1));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rate), read_int(group, "bitrate-mbps", 8, 1, 20));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(fps), read_int(group, "fps-limit", 60, 15, 60));
    gtk_combo_box_set_active(GTK_COMBO_BOX(size), read_int(group, "resolution", 1, 0, 1));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(audio), g_key_file_get_boolean(preferences, group, "audio", NULL));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(audio_delay),read_int(group,"audio-delay-ms",0,0,250));
    gchar *address = g_key_file_get_string(preferences, groups[1], "endpoint", NULL);
    gtk_entry_set_text(GTK_ENTRY(endpoint), address ? address : ""); g_free(address);
    loading = FALSE;
}
static void choose_mode(GtkComboBox *box, gpointer data) {
    (void)box; (void)data;
    if (!loading) store_profile(previous_mode);
    int choice = selected(); previous_mode = choice; load_profile(choice); set_guide(choice);
    GtkWidget *quality = gtk_notebook_get_nth_page(GTK_NOTEBOOK(pages), 1);
    gtk_widget_set_sensitive(quality, TRUE);
    gtk_widget_set_sensitive(endpoint, choice == 1);
    if (choice == 1) gtk_widget_show(endpoint_row); else gtk_widget_hide(endpoint_row);
    notice(choice == 0 ? "Check USB authorization, then open the desktop." : "Use Set up Wi-Fi from USB once, or enter an existing TCP ADB address.");
}
static gboolean save_preferences(void) {
    store_profile(selected());
    g_key_file_set_integer(preferences,"general","connection",selected());
    gchar *directory = g_path_get_dirname(settings_path);
    if (g_mkdir_with_parents(directory, 0700) < 0) { notice("Could not create the settings folder."); g_free(directory); return FALSE; }
    g_free(directory);
    gsize length; gchar *text = g_key_file_to_data(preferences, &length, NULL);
    GError *error = NULL;
    gboolean ok = g_file_set_contents(settings_path, text, length, &error);
    g_free(text);
    if (!ok) { notice(error->message); g_error_free(error); return FALSE; }
    notice("Connection and quality profile saved. Forward sound applies on supported Android versions."); return TRUE;
}
static void save_clicked(GtkWidget *widget, gpointer data) { (void)widget; (void)data; save_preferences(); }
static void set_preset(GtkWidget *widget, gpointer data) {
    (void)widget; int preset = GPOINTER_TO_INT(data);
    gtk_combo_box_set_active(GTK_COMBO_BOX(codec), preset==0?0:1);
    gtk_combo_box_set_active(GTK_COMBO_BOX(size), preset==2?1:0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rate), preset == 2 ? 8 : preset == 1 ? 6 : 4);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(fps), preset==0?30:60);
    notice("Preset selected. HEVC is recommended for Android device; choose H.264 if another device cannot encode HEVC.");
}
static gboolean expire_backend(gpointer data) {
    (void)data; backend_timeout=0;backend_timed_out=TRUE;
    if(backend_pid) kill(backend_pid,SIGTERM);
    return FALSE;
}
static gboolean read_backend(gpointer unused){
 (void)unused;if(backend_out<0)return FALSE;char buffer[2048];ssize_t n;gboolean changed=FALSE;
 while((n=read(backend_out,buffer,sizeof buffer))>0){if(backend_text->len<16384)g_string_append_len(backend_text,buffer,MIN(n,16384-(gssize)backend_text->len));changed=TRUE;}
 if(changed)gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(details)),backend_text->str,-1);
 return TRUE;
}
static void finish_backend(GPid pid,gint result,gpointer unused){
 (void)unused;if(backend_timeout){g_source_remove(backend_timeout);backend_timeout=0;}
 if(backend_poll){g_source_remove(backend_poll);backend_poll=0;}
 read_backend(NULL);if(backend_out>=0){close(backend_out);backend_out=-1;}
 notice(backend_timed_out?"Operation timed out; DoveOS is still running.":WIFEXITED(result)&&WEXITSTATUS(result)==0?"Completed. Read the result below.":"Connection is not ready. Read the next steps below.");
 backend_pid=0;g_spawn_close_pid(pid);gtk_widget_set_sensitive(start_button,TRUE);dove_pointer(window,GDK_LEFT_PTR);
 gtk_widget_set_sensitive(open_button,TRUE);
 gtk_widget_set_sensitive(mode,TRUE);
 gtk_widget_set_sensitive(wifi_setup_button,TRUE);
 if(preparing_wifi&&WIFEXITED(result)&&WEXITSTATUS(result)==0){
  const char *marker=strstr(backend_text->str,"DOVEDEX_WIFI_ENDPOINT=");
  if(marker){
   const char *at=marker+strlen("DOVEDEX_WIFI_ENDPOINT=");size_t n=strcspn(at,"\r\n");
   if(n>0&&n<64){
    gchar *address=g_strndup(at,n);
    gtk_combo_box_set_active(GTK_COMBO_BOX(mode),1);
    gtk_entry_set_text(GTK_ENTRY(endpoint),address);g_free(address);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(audio),TRUE);
    if(save_preferences())notice("Wi-Fi address saved. Check Wi-Fi, remove the phone USB cable, then open the desktop.");
   }
  }
 }
 preparing_wifi=FALSE;
}
static void start_clicked(GtkWidget *widget,gpointer action){
 (void)widget;if(backend_pid)return;gboolean streaming=GPOINTER_TO_INT(action)==1;
 preparing_wifi=GPOINTER_TO_INT(action)==2;
 gchar *br=g_strdup_printf("%d",gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(rate)));
 gchar *frames=g_strdup_printf("%d",gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(fps)));
 gchar *delay=g_strdup_printf("%d",gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(audio_delay)));
 gchar *argv[]={"/bin/sh","/media/usb0/DoveOS-v2/dovedex/backend.sh",preparing_wifi?"prepare-wifi":streaming?"stream":"check",preparing_wifi||selected()==0?"usb":"wifi",(gchar*)gtk_entry_get_text(GTK_ENTRY(endpoint)),br,frames,gtk_combo_box_get_active(GTK_COMBO_BOX(size))==0?"720":"1080",gtk_combo_box_get_active(GTK_COMBO_BOX(codec))==0?"h264":"h265",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(audio))?"1":"0",delay,NULL};
 GError *error=NULL;g_string_truncate(backend_text,0);backend_timed_out=FALSE;
 if(!g_spawn_async_with_pipes(NULL,argv,NULL,G_SPAWN_DO_NOT_REAP_CHILD,NULL,NULL,&backend_pid,NULL,&backend_out,NULL,&error)){notice(error->message);g_error_free(error);backend_pid=0;g_free(br);g_free(frames);g_free(delay);return;}
 g_free(br);g_free(frames);g_free(delay);fcntl(backend_out,F_SETFL,fcntl(backend_out,F_GETFL)|O_NONBLOCK);
 gtk_widget_set_sensitive(start_button,FALSE);dove_pointer(window,GDK_WATCH);
 gtk_widget_set_sensitive(open_button,FALSE);
 gtk_widget_set_sensitive(mode,FALSE);
 gtk_widget_set_sensitive(wifi_setup_button,FALSE);
 notice(preparing_wifi?"Preparing Wi-Fi from the authorized USB device...":streaming?"Starting full-screen desktop. Ctrl+Alt+Shift+Q returns here.":"Checking authorization. Accept the prompt on the phone if shown.");
 backend_poll=dove_timer(100,read_backend,NULL);backend_timeout=dove_timer(streaming?1810000:preparing_wifi?45000:25000,expire_backend,NULL);g_child_watch_add(backend_pid,finish_backend,NULL);
}
static void open_connection_clicked(GtkWidget *widget,gpointer unused){
 (void)unused;
 if(backend_pid)return;
 if(save_preferences())start_clicked(widget,GINT_TO_POINTER(1));
}
static void close_window(GtkWidget *widget,gpointer unused){
 (void)widget;(void)unused;if(backend_pid)kill(backend_pid,SIGTERM);
 if(backend_timeout)g_source_remove(backend_timeout);
 if(backend_poll)g_source_remove(backend_poll);
 if(backend_out>=0)close(backend_out);
 gtk_main_quit();
}
static void field(GtkWidget *box, const char *label, GtkWidget *input) {
    GtkWidget *row = dove_panel(TRUE, 12); GtkWidget *name = dove_label(label);
    gtk_widget_set_size_request(name, 160, -1); dove_add(row, name);
    gtk_box_pack_start(GTK_BOX(row), input, TRUE, TRUE, 0); dove_add(box, row);
}
static GtkWidget *scroll_page(GtkWidget *child){
 GtkWidget *scroll=dove_scroll(child);
 gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
 gtk_viewport_set_shadow_type(GTK_VIEWPORT(gtk_bin_get_child(GTK_BIN(scroll))),GTK_SHADOW_NONE);
 return scroll;
}
static gboolean smoke_test(gpointer data) {
    (void)data;
    gtk_combo_box_set_active(GTK_COMBO_BOX(mode), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rate), 12);
    gboolean passed = save_preferences();
    gtk_combo_box_set_active(GTK_COMBO_BOX(mode), 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rate), 6);
    passed = save_preferences() && passed;
    gtk_combo_box_set_active(GTK_COMBO_BOX(mode), 0);
    passed = passed && gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(rate)) == 12;
    gtk_combo_box_set_active(GTK_COMBO_BOX(mode), 1);
    passed = passed && gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(rate)) == 6;
    GKeyFile *saved = g_key_file_new();
    passed = g_key_file_load_from_file(saved, settings_path, G_KEY_FILE_NONE, NULL) && passed;
    passed = passed && g_key_file_get_integer(saved, "usb-adb", "bitrate-mbps", NULL) == 12;
    passed = passed && g_key_file_get_integer(saved, "wifi-adb", "bitrate-mbps", NULL) == 6;
    g_key_file_free(saved);
    gtk_combo_box_set_active(GTK_COMBO_BOX(mode), 0);
    g_print("DOVEDEX_PROFILE_PERSISTENCE passed=%d\n", passed);
    if (preview_path) {
        while (gtk_events_pending()) gtk_main_iteration();
        GdkWindow *drawable = gtk_widget_get_window(window);
        gint width, height; gdk_drawable_get_size(drawable, &width, &height);
        GdkPixbuf *image = gdk_pixbuf_get_from_drawable(NULL, drawable, NULL, 0, 0, 0, 0, width, height);
        GError *error = NULL;
        gboolean captured = image && gdk_pixbuf_save(image, preview_path, "png", &error, NULL);
        g_print("DOVEDEX_PREVIEW captured=%d size=%dx%d%s%s\n", captured, width, height,
                error ? " error=" : "", error ? error->message : "");
        if (error) g_error_free(error);
        if (!captured && image) {
            gchar *path = g_strconcat(preview_path, ".ppm", NULL);
            FILE *file = fopen(path, "wb");
            if (file) {
                int channels = gdk_pixbuf_get_n_channels(image), stride = gdk_pixbuf_get_rowstride(image);
                guchar *pixels = gdk_pixbuf_get_pixels(image);
                fprintf(file, "P6\n%d %d\n255\n", width, height);
                for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) fwrite(pixels + y * stride + x * channels, 1, 3, file);
                fclose(file); g_print("DOVEDEX_PREVIEW_PPM saved\n");
            }
            g_free(path);
        }
        if (image) g_object_unref(image);
    }
    test_result = passed ? 0 : 1;
    g_print("DOVEDEX_PROFILE_SMOKE passed=%d\n", passed);
    gtk_widget_destroy(window); return FALSE;
}
static gboolean apply_recommended(gpointer data) {
    (void)data;
    gtk_combo_box_set_active(GTK_COMBO_BOX(mode), 0);
    set_preset(NULL, GINT_TO_POINTER(2));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(audio), TRUE);
    gboolean saved=save_preferences();
    g_print("DOVEDEX_RECOMMENDED_PROFILE saved=%d codec=HEVC size=1920x1080 fps=60 bitrate=8\n",saved);
    test_result=saved?0:1;
    gtk_widget_destroy(window);return FALSE;
}
int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    preferences = g_key_file_new();
    settings_path = g_build_filename(g_get_user_config_dir(), "dovedex", "profiles.ini", NULL);
    g_key_file_load_from_file(preferences, settings_path, G_KEY_FILE_NONE, NULL);
    window = dove_form("DoveDeX", 760, 480);dove_window_fit(window,760,480);gtk_window_set_wmclass(GTK_WINDOW(window),"dovedex","DoveDeX");gtk_window_set_icon_from_file(GTK_WINDOW(window),"/etc/doveos/classic-icons/48/video-display.png",NULL); backend_text=g_string_new(""); GtkWidget *box = dove_panel(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12); gtk_container_add(GTK_CONTAINER(window), box);
    dove_add(box, dove_label("DoveDeX - your phone desktop on the lapdock"));
    mode = dove_combobox(modes, 2, 0); field(box, "Connection", mode);
    pages = dove_tabs(); gtk_box_pack_start(GTK_BOX(box), pages, TRUE, TRUE, 0);
    GtkWidget *connect = dove_panel(FALSE, 6); gtk_container_set_border_width(GTK_CONTAINER(connect), 10);
    guide = dove_textarea(""); gtk_text_view_set_editable(GTK_TEXT_VIEW(guide), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(guide), FALSE);
    gtk_box_pack_start(GTK_BOX(connect), dove_scrolled(guide, -1, 140), TRUE, TRUE, 0);
    endpoint = dove_textbox(""); gtk_entry_set_max_length(GTK_ENTRY(endpoint), 255);
    endpoint_row = dove_panel(TRUE, 12);
    dove_add(endpoint_row, dove_label("Wi-Fi address:port"));
    gtk_box_pack_start(GTK_BOX(endpoint_row), endpoint, TRUE, TRUE, 0);
    dove_add(connect, endpoint_row);
    wifi_setup_button=dove_button("Set up Wi-Fi from USB");dove_add(connect,wifi_setup_button);
    g_signal_connect(wifi_setup_button,"clicked",G_CALLBACK(start_clicked),GINT_TO_POINTER(2));
    details=dove_textarea("No connection check has run yet.");gtk_text_view_set_editable(GTK_TEXT_VIEW(details),FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(details),FALSE);
    dove_add(connect,dove_scrolled(details,-1,80));dove_tab_add(pages, connect, "Connect");
    GtkWidget *quality = dove_panel(FALSE, 8); gtk_container_set_border_width(GTK_CONTAINER(quality), 12);
    dove_add(quality, dove_label("USB and Wi-Fi save separate profiles. Direct hardware display avoids CPU video conversion. The FPS setting is a limit; actual rate depends on the phone, content and connection."));
    GtkWidget *presets = dove_panel(TRUE, 6); const char *names[] = {"720p / 30", "720p / 60", "1080p / 60"};
    for (int i = 0; i < 3; i++) { GtkWidget *b = dove_button(names[i]); dove_add(presets, b); g_signal_connect(b, "clicked", G_CALLBACK(set_preset), GINT_TO_POINTER(i)); }
    dove_add(quality, presets);
    codec = dove_combobox(codecs, 2, 0); rate = dove_numberbox(1, 20, 1);
    fps = dove_numberbox(15, 60, 1); size = dove_combobox(sizes, 2, 0);
    audio = dove_checkbox("Forward sound through dock speakers", TRUE);
    audio_delay=dove_numberbox(0,250,10);
    field(quality, "Video codec", codec); field(quality, "Bitrate (Mb/s)", rate);
    field(quality, "FPS limit", fps); field(quality, "Video resolution", size); dove_add(quality, audio);
    field(quality,"Sound delay (ms)",audio_delay);
    dove_add(quality,dove_label("If the picture is behind the sound, increase Sound delay. Reconnect to apply. USB and Wi-Fi save separate values."));
    dove_add(quality, dove_label("Wi-Fi starting point: HEVC / 720p / 60 FPS / 4 Mb/s. USB: HEVC / 1080p / 60 FPS / 8 Mb/s. H.264 is also supported. Sound forwarding needs Android 11 or newer. Phone sound returns when the session closes."));
    GtkWidget *save = dove_button("Save profile"); dove_add(quality, save); g_signal_connect(save, "clicked", G_CALLBACK(save_clicked), NULL);
    dove_tab_add(pages, scroll_page(quality), "Quality");
    GtkWidget *help = dove_panel(FALSE, 10); gtk_container_set_border_width(GTK_CONTAINER(help), 12);
    dove_add(help, dove_label("If your phone does not appear\nUse a USB data cable, unlock the phone and approve USB debugging. Wi-Fi currently requires an already enabled legacy ADB address:port. Modern wireless-debugging pairing is not available yet."));
    dove_add(help, dove_label("If Wi-Fi feels slow\nUse 5 GHz and compare network delay. If using a Wi-Fi low-latency companion, keep it visible on the phone with its screen on. Support depends on the Android device."));
    dove_add(help, dove_label("Desktop support\nNative desktop modes depend on phone firmware. Secondary desktop and freeform-window support depend on the Android version and manufacturer. Compatibility varies; screen mirroring is available where supported."));
    dove_add(help, dove_label("Leave a session\nCtrl + Alt + Shift + Q returns to DoveOS; Alt + Q also closes the viewer. The phone desktop uses the full screen while the DoveOS desktop stays running in the background."));
    dove_tab_add(pages, scroll_page(help), "Help");
    status = dove_label(""); dove_add(box, status);
    GtkWidget *actions = dove_panel(TRUE, 10); start_button = dove_button("Check USB");
    open_button=dove_button("Open USB / Wi-Fi");
    gtk_widget_set_tooltip_text(open_button,"Start the phone desktop using the connection selected above.");
    GtkWidget *close = dove_button("Close");
    dove_add(actions,open_button);g_signal_connect(open_button,"clicked",G_CALLBACK(open_connection_clicked),NULL);
    dove_add(actions, start_button); dove_add(actions, close); dove_add(box, actions);
    g_signal_connect(start_button, "clicked", G_CALLBACK(start_clicked), NULL);
    g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_widget_destroy), window);
    g_signal_connect(window, "destroy", G_CALLBACK(close_window), NULL);
    g_signal_connect(mode, "changed", G_CALLBACK(choose_mode), NULL);
    int saved_mode=read_int("general","connection",0,0,1);
    loading=TRUE;gtk_combo_box_set_active(GTK_COMBO_BOX(mode),saved_mode);
    gtk_widget_show_all(window); loading=TRUE;choose_mode(NULL, NULL); g_print("DOVEDEX_UI_READY\n");
    if (argc > 1 && !strcmp(argv[1], "--smoke-test")) {
        if (argc > 2) preview_path = argv[2];
        dove_timer(1000, smoke_test, NULL);
    } else if (argc > 1 && !strcmp(argv[1], "--apply-recommended")) {
        dove_timer(500, apply_recommended, NULL);
    }
    gtk_main(); g_key_file_free(preferences); g_free(settings_path);g_string_free(backend_text,TRUE); return test_result;
}
