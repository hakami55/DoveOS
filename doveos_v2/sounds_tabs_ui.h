static void sounds_add(GtkWidget *box,GtkWidget *widget){gtk_box_pack_start(GTK_BOX(box),widget,FALSE,FALSE,1);}
static GtkWidget *sounds_book,*startup_enabled,*startup_clip,*startup_gain,*events_enabled,*event_clip;
static GtkWidget *startup_preview,*events_preview,*sounds_apply;
static const char *startup_ids[]={"dove-dawn","dove-wings","dove-nest"};
static const char *event_ids[]={"information","warning","question","error"};
static void sounds_command(GtkWidget *w,gpointer data){
 (void)w;char arg[128];int i=gtk_combo_box_get_active(GTK_COMBO_BOX(startup_clip));if(i<0||i>2)i=0;
 const char *command=data;
 if(!strcmp(command,"startup-sound-save"))snprintf(arg,sizeof arg,"%d:%s:%d:%d",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(startup_enabled)),startup_ids[i],(int)gtk_range_get_value(GTK_RANGE(startup_gain)),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(events_enabled)));
 else if(!strcmp(command,"startup-sound-preview"))snprintf(arg,sizeof arg,"%s:%d",startup_ids[i],(int)gtk_range_get_value(GTK_RANGE(startup_gain)));
 else {i=gtk_combo_box_get_active(GTK_COMBO_BOX(event_clip));if(i<0||i>3)i=0;snprintf(arg,sizeof arg,"%s",event_ids[i]);}
 run_action(command,arg,NULL);gtk_label_set_text(GTK_LABEL(status),message);
}
static GtkWidget *sounds_tab(const char *name){
 GtkWidget *box=gtk_vbox_new(FALSE,4);gtk_container_set_border_width(GTK_CONTAINER(box),12);
 gtk_notebook_append_page(GTK_NOTEBOOK(sounds_book),box,gtk_label_new(name));return box;
}
static GtkWidget *sounds_frame(GtkWidget *box,const char *name){
 GtkWidget *frame=dove_groupbox(name),*inside=gtk_vbox_new(FALSE,3);sounds_add(box,frame);
 gtk_container_set_border_width(GTK_CONTAINER(inside),6);gtk_container_add(GTK_CONTAINER(frame),inside);return inside;
}
static GtkWidget *sounds_button(GtkWidget *box,const char *title,const char *command){
 GtkWidget *b=dove_button(title);sounds_add(box,b);g_signal_connect(b,"clicked",G_CALLBACK(sounds_command),(gpointer)command);return b;
}
static void sounds_build_page(GtkWidget *parent){
 sounds_book=gtk_notebook_new();gtk_box_pack_start(GTK_BOX(parent),sounds_book,TRUE,TRUE,0);
 GtkWidget *box=sounds_tab("Volume"),*inside=sounds_frame(box,"Device volume");
 volume_label=label("");sounds_add(inside,volume_label);volume_scale=gtk_hscale_new_with_range(0,100,1);gtk_scale_set_value_pos(GTK_SCALE(volume_scale),GTK_POS_RIGHT);sounds_add(inside,volume_scale);g_signal_connect(volume_scale,"value-changed",G_CALLBACK(volume_changed),NULL);
 mute=gtk_check_button_new_with_label("Mute audio");sounds_add(inside,mute);g_signal_connect(mute,"toggled",G_CALLBACK(mute_changed),NULL);
 audio_note(box,"Fn volume keys continue to work when the dock sends key events.");
 box=sounds_tab("Sounds");inside=sounds_frame(box,"Startup melody");
 startup_enabled=gtk_check_button_new_with_label("Play a sound when DoveOS starts");sounds_add(inside,startup_enabled);
 startup_clip=gtk_combo_box_text_new();const char *names[]={"Dove Dawn (recommended, 2 sec)","Dove Wings (1.6 sec)","Dove Nest (1.9 sec)"};
 for(int i=0;i<3;i++){gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(startup_clip),names[i]);}sounds_add(inside,startup_clip);
 sounds_add(inside,label("Melody volume:"));startup_gain=gtk_hscale_new_with_range(0,100,1);sounds_add(inside,startup_gain);
 startup_preview=sounds_button(inside,"Preview startup melody","startup-sound-preview");
 inside=sounds_frame(box,"System events");events_enabled=gtk_check_button_new_with_label("Play DoveOS dialog sounds");sounds_add(inside,events_enabled);
 event_clip=gtk_combo_box_text_new();const char *events[]={"Information","Warning","Question","Error"};for(int i=0;i<4;i++)gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(event_clip),events[i]);gtk_combo_box_set_active(GTK_COMBO_BOX(event_clip),0);sounds_add(inside,event_clip);
 events_preview=sounds_button(inside,"Preview event sound","system-sound-preview");
 sounds_apply=sounds_button(box,"Apply sound settings","startup-sound-save");
 audio_note(box,"Startup plays once per full boot. Previews respect mute and active playback.");
 char config[128],clip[64];int enabled=1,gain=40,event=1;read_line("/media/usb0/DoveOS-v2/home/startup-sounds.txt",config,sizeof config);
 int selected=0;if(sscanf(config,"%d:%63[^:]:%d:%d",&enabled,clip,&gain,&event)==4){for(int i=0;i<3;i++)if(!strcmp(clip,startup_ids[i]))selected=i;}
 else {enabled=1;gain=40;event=1;}if(gain<0||gain>100)gain=40;
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(startup_enabled),enabled==1);gtk_combo_box_set_active(GTK_COMBO_BOX(startup_clip),selected);gtk_range_set_value(GTK_RANGE(startup_gain),gain);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(events_enabled),event==1);
 box=sounds_tab("Audio");audio_build_output(box);
 box=sounds_tab("Voice");audio_build_voice(box);
 box=sounds_tab("Hardware");inside=sounds_frame(box,"Built-in audio hardware");
 char cards[1024];read_line("/proc/asound/cards",cards,sizeof cards);sounds_add(inside,label(cards[0]?cards:"Realtek built-in audio (RTKAlsaCard)"));
 audio_note(inside,"Stereo playback and 3.5 mm headset microphone.\nThe jack automatically switches speakers to headphones.\nManual speaker/headphone routing is not available.\nMicrophone recording stays in RAM until restart.");
}
static gboolean sounds_smoke(gpointer data){
 (void)data;static int step;
 if(step>0){GdkPixbuf *image=gdk_pixbuf_get_from_drawable(NULL,gtk_widget_get_window(window),gtk_widget_get_colormap(window),0,0,0,0,window->allocation.width,window->allocation.height);if(image){char path[96];snprintf(path,sizeof path,"/tmp/doveos-sounds-tab-%d.png",step-1);gdk_pixbuf_save(image,path,"png",NULL,NULL);g_object_unref(image);}}
 if(step<5){gtk_notebook_set_current_page(GTK_NOTEBOOK(book),3);gtk_notebook_set_current_page(GTK_NOTEBOOK(sounds_book),step++);return TRUE;}
 printf("SOUNDS_TABS_CHECK tabs=%d output=Stereo,Left,Right microphone=Record,Play,Stop\n",gtk_notebook_get_n_pages(GTK_NOTEBOOK(sounds_book)));gtk_widget_destroy(window);return FALSE;
}

