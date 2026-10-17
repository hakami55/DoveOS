/* Three interactive layout examples. All changes stay inside this demo. */
typedef struct { GtkWidget *name,*size,*value,*summary; } LayoutForm;
static GtkWidget *layout_name,*layout_size,*layout_value,*layout_apply,*layout_reset;
static GtkWidget *layout_list,*layout_details,*layout_editor,*layout_count,*layout_clear,*layout_tabs;
static void layout_apply_clicked(GtkWidget *button,gpointer data) {
 (void)button;LayoutForm *form=data;
 gchar *size=gtk_combo_box_get_active_text(GTK_COMBO_BOX(form->size));
 gchar *text=g_strdup_printf("Preview: %s | %s | Value %d",
  gtk_entry_get_text(GTK_ENTRY(form->name)),size?size:"Normal",
  gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(form->value)));
 gtk_label_set_text(GTK_LABEL(form->summary),text);g_free(text);g_free(size);
}
static void layout_reset_clicked(GtkWidget *button,gpointer data) {
 (void)button;LayoutForm *form=data;gtk_entry_set_text(GTK_ENTRY(form->name),"DoveOS");
 gtk_combo_box_set_active(GTK_COMBO_BOX(form->size),1);
 gtk_spin_button_set_value(GTK_SPIN_BUTTON(form->value),50);
 gtk_label_set_text(GTK_LABEL(form->summary),"Change the fields, then click Apply example.");
}
static void layout_item_selected(GtkTreeSelection *selection,gpointer data) {
 GtkTreeModel *model;GtkTreeIter iter;if(!gtk_tree_selection_get_selected(selection,&model,&iter))return;
 gchar *name,*description;gtk_tree_model_get(model,&iter,0,&name,1,&description,-1);
 gchar *text=g_strdup_printf("%s\n\n%s\n\nDrag the divider to resize the list and details.",name,description);
 gtk_label_set_text(GTK_LABEL(data),text);g_free(text);g_free(name);g_free(description);
}
static void layout_text_changed(GtkTextBuffer *buffer,gpointer data) {
 gchar *text=g_strdup_printf("%d characters | %d lines",gtk_text_buffer_get_char_count(buffer),gtk_text_buffer_get_line_count(buffer));
 gtk_label_set_text(GTK_LABEL(data),text);g_free(text);
}
static void layout_clear_clicked(GtkWidget *button,gpointer data) {
 (void)button;gtk_text_buffer_set_text(GTK_TEXT_BUFFER(data),"",-1);
}
static GtkWidget *layout_examples(void) {
 GtkWidget *outer=dove_layout(FALSE);layout_tabs=dove_tabs();dove_pack(outer,layout_tabs,TRUE);
 GtkWidget *page=dove_page(),*fields=dove_fields();LayoutForm *form=g_new0(LayoutForm,1);
 g_object_set_data_full(G_OBJECT(outer),"layout-form",form,g_free);
 dove_pack(page,dove_label("Settings form: aligned labels and right-aligned actions."),FALSE);
 form->name=layout_name=dove_textbox("DoveOS");const char *choices[]={"Small","Normal","Large"};
 form->size=layout_size=dove_combobox(choices,3,1);form->value=layout_value=dove_numberbox(0,100,1);
 gtk_spin_button_set_value(GTK_SPIN_BUTTON(form->value),50);
 dove_field(fields,"_Name",form->name);dove_field(fields,"_Size",form->size);dove_field(fields,"_Value",form->value);dove_pack(page,fields,FALSE);
 form->summary=dove_label("Change the fields, then click Apply example.");gtk_label_set_line_wrap(GTK_LABEL(form->summary),TRUE);dove_pack(page,form->summary,FALSE);
 GtkWidget *actions=dove_actions();dove_pack(page,actions,FALSE);
 layout_reset=dove_action(actions,"_Reset",G_CALLBACK(layout_reset_clicked),form);
 layout_apply=dove_action(actions,"_Apply example",G_CALLBACK(layout_apply_clicked),form);
 dove_pack(page,dove_label("Alt+N focuses Name. These sample values do not change system settings."),FALSE);
 dove_tab_add(layout_tabs,dove_scroll(page),"Form");

 page=dove_page();dove_pack(page,dove_label("List and details: select a location; resize either pane."),FALSE);
 GtkWidget *split=gtk_hpaned_new();dove_pack(page,split,TRUE);GtkListStore *store=gtk_list_store_new(2,G_TYPE_STRING,G_TYPE_STRING);
 const char *names[]={"Desktop","Media","Documents","Downloads"};
 const char *descriptions[]={"Shortcuts and application launchers.","Videos, music and pictures.","Your documents and saved work.","Files received from other devices."};
 GtkTreeIter iter;for(gint i=0;i<4;i++){gtk_list_store_append(store,&iter);gtk_list_store_set(store,&iter,0,names[i],1,descriptions[i],-1);}
 layout_list=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));g_object_unref(store);dove_table_text(layout_list,"Location",0,TRUE);
 gtk_paned_pack1(GTK_PANED(split),dove_scroll(layout_list),TRUE,FALSE);
 layout_details=dove_label("Select a location to see its details.");gtk_label_set_line_wrap(GTK_LABEL(layout_details),TRUE);gtk_label_set_selectable(GTK_LABEL(layout_details),TRUE);
 gtk_misc_set_alignment(GTK_MISC(layout_details),0,0);GtkWidget *details=dove_page();dove_pack(details,layout_details,TRUE);
 gtk_paned_pack2(GTK_PANED(split),dove_scroll(details),TRUE,FALSE);gtk_paned_set_position(GTK_PANED(split),170);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(layout_list)),"changed",G_CALLBACK(layout_item_selected),layout_details);
 dove_pack(page,dove_label("Demo locations only: this example does not open or modify files."),FALSE);dove_tab_add(layout_tabs,page,"List / details");

 page=dove_page();dove_pack(page,dove_label("Document layout: action bar, expanding editor and status line."),FALSE);
 actions=dove_actions();dove_pack(page,actions,FALSE);layout_editor=gtk_text_view_new();gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(layout_editor),GTK_WRAP_WORD_CHAR);
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(layout_editor));layout_clear=dove_action(actions,"_Clear text",G_CALLBACK(layout_clear_clicked),buffer);
 dove_pack(page,dove_scroll(layout_editor),TRUE);layout_count=dove_label("");dove_pack(page,layout_count,FALSE);
 g_signal_connect(buffer,"changed",G_CALLBACK(layout_text_changed),layout_count);
 gtk_text_buffer_set_text(buffer,"Write a few lines here. Resize the window to test wrapping and scrolling.\nThis is a temporary demo document; it is not saved.",-1);
 dove_tab_add(layout_tabs,page,"Editor");return outer;
}
