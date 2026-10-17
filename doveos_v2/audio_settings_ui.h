/* Simple tests use automatic dock output and the verified headset mic. */
static GtkWidget *audio_test_button(GtkWidget *box,const char *title,const char *command) {
    GtkWidget *b=dove_button(title);add(box,b);
    g_signal_connect(b,"clicked",G_CALLBACK(action),(gpointer)command);return b;
}
static void audio_note(GtkWidget *box,const char *text) {
    GtkWidget *note=label(text);gtk_label_set_line_wrap(GTK_LABEL(note),FALSE);add(box,note);
}
static void audio_build_output(GtkWidget *box) {
    GtkWidget *frame=dove_groupbox("Test Output");add(box,frame);
    GtkWidget *row=dove_panel(TRUE,8);gtk_container_set_border_width(GTK_CONTAINER(row),8);gtk_container_add(GTK_CONTAINER(frame),row);
    sound_test=audio_test_button(row,"Stereo","sound-test");
    sound_left=audio_test_button(row,"Left","sound-test-left");
    sound_right=audio_test_button(row,"Right","sound-test-right");
    audio_note(box,"Headphones plugged in: headphone output.\nHeadphones unplugged: built-in speakers.");
}
static void audio_build_voice(GtkWidget *box) {
    GtkWidget *frame=dove_groupbox("Test Microphone");add(box,frame);
    GtkWidget *row=dove_panel(TRUE,8);gtk_container_set_border_width(GTK_CONTAINER(row),8);gtk_container_add(GTK_CONTAINER(frame),row);
    sound_record=audio_test_button(row,"Record (5 sec)","sound-record");
    sound_play=audio_test_button(row,"Play Recording","sound-play-recording");
    sound_stop=dove_button("Stop Test");add(row,sound_stop);
    g_signal_connect(sound_stop,"clicked",G_CALLBACK(stop_sound_test),NULL);
    audio_note(box,"Use a 3.5 mm headset with a microphone.\nClick Record, wait for Recording, then speak for five seconds.\nPlay Recording to listen. Sample stays in RAM.");
}
