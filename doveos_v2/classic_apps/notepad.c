#define _POSIX_C_SOURCE 200809L
#include "doveos.h"
#include <gdk/gdkkeysyms.h>
#include <string.h>
#include <time.h>

enum {UTF8,UTF8_BOM,UTF16_LE,UTF16_BE,ANSI1252};
typedef struct {gchar *text;gint cursor;} Snapshot;
static GtkWidget *window,*editor,*undo_item,*redo_item,*find_dialog,*find_entry,*match_case,*search_up;
static GtkTextBuffer *buffer;static DovePreferences *prefs;static GPtrArray *history;
static gchar *filename,*saved_text,*font_name;static gint encoding,history_at;
static gboolean windows_lines=TRUE,restoring,word_wrap;static guint checkpoint_id;
static const gsize max_file=8*1024*1024,history_limit=8*1024*1024;
static gchar *contents(void){GtkTextIter a,b;gtk_text_buffer_get_bounds(buffer,&a,&b);return gtk_text_buffer_get_text(buffer,&a,&b,TRUE);}
static void title(void){gchar *name=filename?g_filename_display_basename(filename):g_strdup("Untitled");gchar *text=g_strdup_printf("%s%s - Notepad",gtk_text_buffer_get_modified(buffer)?"*":"",name);gtk_window_set_title(GTK_WINDOW(window),text);g_free(text);g_free(name);}
static void error_message(const char *text,GError *error){gchar *message=error?g_strdup_printf("%s\n\n%s",text,error->message):g_strdup(text);GtkWidget *dialog=dove_messagebox_classic(GTK_WINDOW(window),message,GTK_MESSAGE_ERROR,DOVE_BUTTONS_OK);gtk_window_set_title(GTK_WINDOW(dialog),"Notepad");gtk_dialog_run(GTK_DIALOG(dialog));gtk_widget_destroy(dialog);g_free(message);}
static gchar *decode(const gchar *data,gsize size,gint *kind,gboolean *crlf,GError **error){
 gchar *utf=NULL;gsize utf_size=0;*kind=UTF8;
 if(size>max_file){g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_FAILED,"The file exceeds Notepad's 8 MiB text-file limit.");return NULL;}
 if(size>=2&&((guchar)data[0]==0xff&&(guchar)data[1]==0xfe)){*kind=UTF16_LE;utf=g_convert(data+2,size-2,"UTF-8","UTF-16LE",NULL,&utf_size,error);}
 else if(size>=2&&((guchar)data[0]==0xfe&&(guchar)data[1]==0xff)){*kind=UTF16_BE;utf=g_convert(data+2,size-2,"UTF-8","UTF-16BE",NULL,&utf_size,error);}
 else{
  if(memchr(data,0,size)){g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_FAILED,"This file contains binary data, rather than plain text.");return NULL;}
  if(size>=3&&!memcmp(data,"\xef\xbb\xbf",3)){*kind=UTF8_BOM;data+=3;size-=3;}
  if(g_utf8_validate(data,size,NULL))utf=g_strndup(data,size);
  else if(*kind==UTF8_BOM){g_set_error_literal(error,G_CONVERT_ERROR,G_CONVERT_ERROR_ILLEGAL_SEQUENCE,"The UTF-8 file contains invalid text.");return NULL;}
  else{*kind=ANSI1252;utf=g_convert(data,size,"UTF-8","WINDOWS-1252",NULL,NULL,error);}
 }
 if(!utf)return NULL;
 if(utf_size&&memchr(utf,0,utf_size)){g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_FAILED,"This file contains binary data, rather than plain text.");g_free(utf);return NULL;}
 *crlf=strstr(utf,"\r\n")!=NULL;GString *normal=g_string_new(NULL);
 for(const gchar *p=utf;*p;p++){if(*p=='\r'){g_string_append_c(normal,'\n');if(p[1]=='\n')p++;}else g_string_append_c(normal,*p);}
 g_free(utf);return g_string_free(normal,FALSE);
}
static gchar *encode(const gchar *text,gint kind,gboolean crlf,gsize *size,GError **error){
 GString *lines=g_string_new(NULL);for(const gchar *p=text;*p;p++){if(*p=='\n'&&crlf)g_string_append_c(lines,'\r');g_string_append_c(lines,*p);}
 gchar *out=NULL;gsize n=0;
 if(kind==UTF8||kind==UTF8_BOM){n=lines->len;out=g_strdup(lines->str);}
 else out=g_convert(lines->str,lines->len,kind==ANSI1252?"WINDOWS-1252":kind==UTF16_LE?"UTF-16LE":"UTF-16BE","UTF-8",NULL,&n,error);
 g_string_free(lines,TRUE);if(!out)return NULL;
 const gchar *bom=kind==UTF8_BOM?"\xef\xbb\xbf":kind==UTF16_LE?"\xff\xfe":kind==UTF16_BE?"\xfe\xff":"";
 gsize prefix=kind==UTF8_BOM?3:(kind==UTF16_LE||kind==UTF16_BE)?2:0;
 gchar *result=g_malloc(n+prefix+1);memcpy(result,bom,prefix);memcpy(result+prefix,out,n);result[n+prefix]=0;*size=n+prefix;g_free(out);return result;
}
static void snapshot_free(gpointer value){Snapshot *s=value;g_free(s->text);g_free(s);}
static void history_buttons(void){gtk_widget_set_sensitive(undo_item,history_at>0||checkpoint_id!=0);gtk_widget_set_sensitive(redo_item,history_at+1<(gint)history->len&&checkpoint_id==0);}
static gboolean checkpoint(gpointer data){(void)data;checkpoint_id=0;gchar *text=contents();
 if(history->len&&!strcmp(text,((Snapshot*)g_ptr_array_index(history,history_at))->text)){g_free(text);history_buttons();return FALSE;}
 while((gint)history->len>history_at+1)g_ptr_array_remove_index(history,history->len-1);
 Snapshot *s=g_new0(Snapshot,1);s->text=text;GtkTextIter cursor;gtk_text_buffer_get_iter_at_mark(buffer,&cursor,gtk_text_buffer_get_insert(buffer));s->cursor=gtk_text_iter_get_offset(&cursor);g_ptr_array_add(history,s);history_at=history->len-1;
 gsize bytes=0;for(guint i=0;i<history->len;i++)bytes+=strlen(((Snapshot*)g_ptr_array_index(history,i))->text);
 while(history->len>1&&(history->len>32||bytes>history_limit)){bytes-=strlen(((Snapshot*)g_ptr_array_index(history,0))->text);g_ptr_array_remove_index(history,0);history_at--;}
 history_buttons();return FALSE;
}
static void flush_checkpoint(void){if(checkpoint_id){g_source_remove(checkpoint_id);checkpoint_id=0;checkpoint(NULL);}}
static void reset_history(void){if(checkpoint_id){g_source_remove(checkpoint_id);checkpoint_id=0;}g_ptr_array_set_size(history,0);history_at=-1;checkpoint(NULL);}
static void changed(GtkTextBuffer *text,gpointer data){(void)text;(void)data;if(restoring)return;if(checkpoint_id)g_source_remove(checkpoint_id);checkpoint_id=g_timeout_add(700,checkpoint,NULL);title();history_buttons();}
static void undo_redo(GtkWidget *widget,gpointer data){(void)widget;flush_checkpoint();gint next=history_at+GPOINTER_TO_INT(data);if(next<0||next>=(gint)history->len)return;history_at=next;Snapshot *s=g_ptr_array_index(history,next);restoring=TRUE;gtk_text_buffer_set_text(buffer,s->text,-1);GtkTextIter at;gtk_text_buffer_get_iter_at_offset(buffer,&at,s->cursor);gtk_text_buffer_place_cursor(buffer,&at);gtk_text_buffer_set_modified(buffer,g_strcmp0(s->text,saved_text)!=0);restoring=FALSE;title();history_buttons();gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(editor),gtk_text_buffer_get_insert(buffer));}
static void set_document(const gchar *text,const gchar *path,gint kind,gboolean crlf){restoring=TRUE;gtk_text_buffer_set_text(buffer,text,-1);gtk_text_buffer_set_modified(buffer,FALSE);restoring=FALSE;g_free(filename);filename=g_strdup(path);g_free(saved_text);saved_text=g_strdup(text);encoding=kind;windows_lines=crlf;reset_history();title();}
static gboolean load_file(const gchar *path){GError *error=NULL;gchar *data=NULL,*text=NULL;gsize size;gint kind;gboolean crlf;GStatBuf file_stat;
 if(g_stat(path,&file_stat)==0&&(guint64)file_stat.st_size>max_file){error_message("This text file is too large. Notepad supports files up to 8 MiB.",NULL);return FALSE;}
 if(g_file_get_contents(path,&data,&size,&error))text=decode(data,size,&kind,&crlf,&error);
 if(text){set_document(text,path,kind,crlf);g_free(text);g_free(data);return TRUE;}
 error_message("Cannot open this file.",error);g_clear_error(&error);g_free(data);return FALSE;
}
static gboolean write_file(const gchar *path,gint kind){gchar *text=contents();gsize size;GError *error=NULL;gchar *bytes=encode(text,kind,windows_lines,&size,&error);gboolean ok=bytes&&g_file_set_contents(path,bytes,size,&error);
 if(ok){gchar *newname=g_strdup(path);g_free(filename);filename=newname;g_free(saved_text);saved_text=g_strdup(text);encoding=kind;gtk_text_buffer_set_modified(buffer,FALSE);title();}
 else error_message("Cannot save this file. If its original encoding cannot represent the text, use Save As and choose UTF-8.",error);
 g_free(bytes);g_free(text);g_clear_error(&error);return ok;
}
static void filters(GtkWidget *dialog){GtkFileFilter *f=gtk_file_filter_new();gtk_file_filter_set_name(f,"Text Documents (*.txt)");gtk_file_filter_add_pattern(f,"*.txt");gtk_file_filter_add_pattern(f,"*.TXT");gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),f);f=gtk_file_filter_new();gtk_file_filter_set_name(f,"All Files");gtk_file_filter_add_pattern(f,"*");gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),f);}
static gboolean save_as(void){GtkWidget *dialog=dove_savefiledialog(GTK_WINDOW(window),"Save As");filters(dialog);gchar *base=filename?g_path_get_basename(filename):g_strdup("Untitled.txt");gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),base);g_free(base);
 if(filename){gchar *dir=g_path_get_dirname(filename);gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),dir);g_free(dir);}
 GtkWidget *row=dove_layout(TRUE),*choice=gtk_combo_box_new_text();const char *names[]={"UTF-8","UTF-8 with BOM","UTF-16 little-endian","UTF-16 big-endian","Windows ANSI (1252)"};for(int i=0;i<5;i++)gtk_combo_box_append_text(GTK_COMBO_BOX(choice),names[i]);gtk_combo_box_set_active(GTK_COMBO_BOX(choice),encoding);dove_pack(row,dove_label("Encoding:"),FALSE);dove_pack(row,choice,TRUE);gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dialog),row);gtk_widget_show_all(row);
 gboolean ok=FALSE;if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT){gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));if(path)ok=write_file(path,gtk_combo_box_get_active(GTK_COMBO_BOX(choice)));g_free(path);}gtk_widget_destroy(dialog);return ok;
}
static gboolean save(void){return filename?write_file(filename,encoding):save_as();}
static gboolean may_discard(void){if(!gtk_text_buffer_get_modified(buffer))return TRUE;gchar *base=filename?g_filename_display_basename(filename):g_strdup("Untitled");gchar *question=g_strdup_printf("The text in %s has changed.\n\nDo you want to save the changes?",base);GtkWidget *dialog=dove_messagebox_classic(GTK_WINDOW(window),question,GTK_MESSAGE_QUESTION,DOVE_BUTTONS_YES_NO_CANCEL);gtk_window_set_title(GTK_WINDOW(dialog),"Notepad");gint response=gtk_dialog_run(GTK_DIALOG(dialog));gtk_widget_destroy(dialog);g_free(base);g_free(question);return response==GTK_RESPONSE_NO||(response==GTK_RESPONSE_YES&&save());}
static void new_file(GtkWidget *widget,gpointer data){(void)widget;(void)data;if(may_discard())set_document("",NULL,UTF8,TRUE);}
static void open_file(GtkWidget *widget,gpointer data){(void)widget;(void)data;if(!may_discard())return;GtkWidget *dialog=dove_openfiledialog(GTK_WINDOW(window),"Open");filters(dialog);if(filename){gchar *dir=g_path_get_dirname(filename);gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),dir);g_free(dir);}if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT){gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));if(path)load_file(path);g_free(path);}gtk_widget_destroy(dialog);}
static void save_action(GtkWidget *widget,gpointer data){(void)widget;if(data)save_as();else save();}
static gboolean close_window(GtkWidget *widget,GdkEvent *event,gpointer data){(void)widget;(void)event;(void)data;return !may_discard();}
static void exit_action(GtkWidget *widget,gpointer data){(void)widget;(void)data;if(may_discard())gtk_widget_destroy(window);}
static void edit_action(GtkWidget *widget,gpointer data){(void)widget;GtkClipboard *clip=gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);const char *action=data;
 if(!strcmp(action,"cut"))gtk_text_buffer_cut_clipboard(buffer,clip,TRUE);else if(!strcmp(action,"copy"))gtk_text_buffer_copy_clipboard(buffer,clip);else if(!strcmp(action,"paste"))gtk_text_buffer_paste_clipboard(buffer,clip,NULL,TRUE);else if(!strcmp(action,"delete"))gtk_text_buffer_delete_selection(buffer,TRUE,TRUE);else if(!strcmp(action,"all")){GtkTextIter a,b;gtk_text_buffer_get_bounds(buffer,&a,&b);gtk_text_buffer_select_range(buffer,&a,&b);}else if(!strcmp(action,"time")){time_t now=time(NULL);struct tm local;localtime_r(&now,&local);char stamp[64];strftime(stamp,sizeof stamp,"%H:%M %Y-%m-%d",&local);gtk_text_buffer_begin_user_action(buffer);gtk_text_buffer_delete_selection(buffer,TRUE,TRUE);gtk_text_buffer_insert_at_cursor(buffer,stamp,-1);gtk_text_buffer_end_user_action(buffer);}}
static void wrap(GtkCheckMenuItem *item,gpointer data){(void)data;word_wrap=gtk_check_menu_item_get_active(item);gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(editor),word_wrap?GTK_WRAP_WORD_CHAR:GTK_WRAP_NONE);}
static void choose_font(GtkWidget *widget,gpointer data){(void)widget;(void)data;GtkWidget *dialog=gtk_font_selection_dialog_new("Font");dove_dialog_style(dialog,GTK_WINDOW(window),"Font");gtk_font_selection_dialog_set_font_name(GTK_FONT_SELECTION_DIALOG(dialog),font_name);if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_OK){gchar *name=gtk_font_selection_dialog_get_font_name(GTK_FONT_SELECTION_DIALOG(dialog));PangoFontDescription *font=pango_font_description_from_string(name);gtk_widget_modify_font(editor,font);pango_font_description_free(font);g_free(font_name);font_name=name;}gtk_widget_destroy(dialog);}
static gboolean locate(const gchar *text,const gchar *needle,gint cursor,gboolean backwards,gboolean sensitive,gint *a,gint *b){if(!*needle)return FALSE;gchar *escaped=g_regex_escape_string(needle,-1);GRegex *regex=g_regex_new(escaped,sensitive?0:G_REGEX_CASELESS,0,NULL);g_free(escaped);if(!regex)return FALSE;GMatchInfo *match=NULL;gboolean found=FALSE;gint limit=g_utf8_offset_to_pointer(text,cursor)-text;
 if(!backwards){if(g_regex_match_full(regex,text,-1,limit,0,&match,NULL)){g_match_info_fetch_pos(match,0,a,b);found=TRUE;}}
 else{g_regex_match(regex,text,0,&match);while(g_match_info_matches(match)){gint x,y;g_match_info_fetch_pos(match,0,&x,&y);if(y>limit)break;*a=x;*b=y;found=TRUE;g_match_info_next(match,NULL);}}
 if(match){g_match_info_free(match);}g_regex_unref(regex);if(found){*a=g_utf8_pointer_to_offset(text,text+*a);*b=g_utf8_pointer_to_offset(text,text+*b);}return found;
}
static void find_next(GtkWidget *widget,gpointer data){(void)widget;(void)data;if(!find_dialog)return;const gchar *needle=gtk_entry_get_text(GTK_ENTRY(find_entry));if(!*needle)return;gboolean up=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(search_up));GtkTextIter a,b;if(!gtk_text_buffer_get_selection_bounds(buffer,&a,&b))gtk_text_buffer_get_iter_at_mark(buffer,&a,gtk_text_buffer_get_insert(buffer)),b=a;gint start=gtk_text_iter_get_offset(up?&a:&b),x,y;gchar *text=contents();
 if(locate(text,needle,start,up,gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(match_case)),&x,&y)){gtk_text_buffer_get_iter_at_offset(buffer,&a,x);gtk_text_buffer_get_iter_at_offset(buffer,&b,y);gtk_text_buffer_select_range(buffer,&a,&b);gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(editor),&a,.1,FALSE,0,0);}
 else{gchar *message=g_strdup_printf("Cannot find \"%s\".",needle);dove_message(GTK_WINDOW(find_dialog),message);g_free(message);}g_free(text);
}
static void find_destroy(GtkWidget *widget,gpointer data){(void)widget;(void)data;find_dialog=NULL;}
static void find_response(GtkDialog *dialog,gint response,gpointer data){(void)data;if(response==GTK_RESPONSE_ACCEPT)find_next(NULL,NULL);else gtk_widget_destroy(GTK_WIDGET(dialog));}
static void show_find(GtkWidget *widget,gpointer data){(void)widget;if(find_dialog){if(data)find_next(NULL,NULL);else gtk_window_present(GTK_WINDOW(find_dialog));return;}
 find_dialog=gtk_dialog_new_with_buttons("Find",GTK_WINDOW(window),GTK_DIALOG_DESTROY_WITH_PARENT,"Find _Next",GTK_RESPONSE_ACCEPT,"Cancel",GTK_RESPONSE_CANCEL,NULL);dove_dialog_style(find_dialog,GTK_WINDOW(window),"Find");GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(find_dialog));gtk_container_set_border_width(GTK_CONTAINER(box),10);GtkWidget *row=dove_layout(TRUE);dove_pack(row,dove_label("Find what:"),FALSE);find_entry=dove_textbox("");dove_pack(row,find_entry,TRUE);dove_pack(box,row,FALSE);gtk_entry_set_activates_default(GTK_ENTRY(find_entry),TRUE);gtk_dialog_set_default_response(GTK_DIALOG(find_dialog),GTK_RESPONSE_ACCEPT);match_case=gtk_check_button_new_with_mnemonic("Match _case");dove_pack(box,match_case,FALSE);row=dove_layout(TRUE);dove_pack(row,dove_label("Direction:"),FALSE);search_up=gtk_radio_button_new_with_mnemonic(NULL,"_Up");GtkWidget *down=gtk_radio_button_new_with_mnemonic_from_widget(GTK_RADIO_BUTTON(search_up),"_Down");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(down),TRUE);dove_pack(row,search_up,FALSE);dove_pack(row,down,FALSE);dove_pack(box,row,FALSE);g_signal_connect(find_dialog,"response",G_CALLBACK(find_response),NULL);g_signal_connect(find_dialog,"destroy",G_CALLBACK(find_destroy),NULL);gtk_widget_show_all(find_dialog);gtk_widget_grab_focus(find_entry);
}
static void about(GtkWidget *widget,gpointer data){(void)widget;(void)data;dove_message(GTK_WINDOW(window),"DoveOS Notepad\nDoveOS plain-text editor\n\nOpen/save, undo/redo, find, word wrap and font selection.\nBuilt with DoveOS SDK " DOVEOS_SDK_VERSION);}
static GtkWidget *add(GtkWidget *menu,const char *label,GCallback action,gpointer data,const char *shortcut){GtkWidget *item=dove_menuitem(label);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",action,data);if(shortcut)dove_shortcut(window,item,"activate",shortcut);return item;}
static GtkWidget *menu(GtkWidget *bar,const char *name){GtkWidget *item=dove_menuitem(name),*child=dove_contextmenu();gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),child);gtk_menu_shell_append(GTK_MENU_SHELL(bar),item);return child;}
static void separator(GtkWidget *menu){gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());}
static void modified(GtkTextBuffer *text,gpointer data){(void)text;(void)data;title();}
static void destroyed(GtkWidget *widget,gpointer data){(void)widget;(void)data;if(checkpoint_id){g_source_remove(checkpoint_id);checkpoint_id=0;}if(prefs){g_key_file_set_boolean(prefs->values,"View","Wrap",word_wrap);g_key_file_set_string(prefs->values,"View","Font",font_name);GError *error=NULL;if(!dove_preferences_save(prefs,&error)){g_warning("Could not save Notepad preferences: %s",error->message);g_clear_error(&error);}}gtk_main_quit();}
int main(int argc,char **argv){gtk_init(&argc,&argv);prefs=dove_preferences_open("notepad",NULL,NULL);font_name=prefs?g_key_file_get_string(prefs->values,"View","Font",NULL):NULL;if(!font_name)font_name=g_strdup("Monospace 11");history=g_ptr_array_new_with_free_func(snapshot_free);history_at=-1;window=dove_form("Untitled - Notepad",760,480);gtk_window_set_wmclass(GTK_WINDOW(window),"DoveNotepad","DoveNotepad");gtk_window_set_icon_from_file(GTK_WINDOW(window),"/media/usb0/DoveOS-v2/ClassicApps/icons/notepad-48.png",NULL);GtkWidget *box=dove_layout(FALSE);gtk_box_set_spacing(GTK_BOX(box),0);gtk_container_add(GTK_CONTAINER(window),box);GtkWidget *bar=dove_menubar();dove_pack(box,bar,FALSE);
 GtkWidget *file=menu(bar,"_File");add(file,"_New",G_CALLBACK(new_file),NULL,"<Control>n");add(file,"_Open...",G_CALLBACK(open_file),NULL,"<Control>o");add(file,"_Save",G_CALLBACK(save_action),NULL,"<Control>s");add(file,"Save _As...",G_CALLBACK(save_action),GINT_TO_POINTER(1),"<Control><Shift>s");separator(file);add(file,"E_xit",G_CALLBACK(exit_action),NULL,NULL);
 GtkWidget *edit=menu(bar,"_Edit");undo_item=add(edit,"_Undo",G_CALLBACK(undo_redo),GINT_TO_POINTER(-1),"<Control>z");redo_item=add(edit,"_Redo",G_CALLBACK(undo_redo),GINT_TO_POINTER(1),"<Control>y");separator(edit);add(edit,"Cu_t",G_CALLBACK(edit_action),"cut","<Control>x");add(edit,"_Copy",G_CALLBACK(edit_action),"copy","<Control>c");add(edit,"_Paste",G_CALLBACK(edit_action),"paste","<Control>v");add(edit,"_Delete",G_CALLBACK(edit_action),"delete",NULL);separator(edit);add(edit,"Select _All",G_CALLBACK(edit_action),"all","<Control>a");add(edit,"Time/_Date",G_CALLBACK(edit_action),"time","F5");separator(edit);GtkWidget *wrap_item=gtk_check_menu_item_new_with_mnemonic("_Word Wrap");gtk_menu_shell_append(GTK_MENU_SHELL(edit),wrap_item);add(edit,"Set _Font...",G_CALLBACK(choose_font),NULL,NULL);
 GtkWidget *search=menu(bar,"_Search");add(search,"_Find...",G_CALLBACK(show_find),NULL,"<Control>f");add(search,"Find _Next",G_CALLBACK(show_find),GINT_TO_POINTER(1),"F3");GtkWidget *help=menu(bar,"_Help");add(help,"_About Notepad",G_CALLBACK(about),NULL,NULL);
 editor=gtk_text_view_new();buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));gtk_text_view_set_left_margin(GTK_TEXT_VIEW(editor),2);gtk_text_view_set_right_margin(GTK_TEXT_VIEW(editor),2);PangoFontDescription *font=pango_font_description_from_string(font_name);gtk_widget_modify_font(editor,font);pango_font_description_free(font);GdkColor white,black;gdk_color_parse("#ffffff",&white);gdk_color_parse("#000000",&black);gtk_widget_modify_base(editor,GTK_STATE_NORMAL,&white);gtk_widget_modify_text(editor,GTK_STATE_NORMAL,&black);GtkWidget *scroll=dove_scrolled(editor,-1,-1);gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll),GTK_SHADOW_IN);dove_pack(box,scroll,TRUE);g_signal_connect(wrap_item,"toggled",G_CALLBACK(wrap),NULL);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wrap_item),prefs&&dove_preferences_bool(prefs,"View","Wrap",FALSE));
 g_signal_connect(buffer,"changed",G_CALLBACK(changed),NULL);g_signal_connect(buffer,"modified-changed",G_CALLBACK(modified),NULL);g_signal_connect(window,"delete-event",G_CALLBACK(close_window),NULL);g_signal_connect(window,"destroy",G_CALLBACK(destroyed),NULL);set_document("",NULL,UTF8,TRUE);if(argc>1)load_file(argv[1]);gtk_widget_show_all(window);gtk_widget_grab_focus(editor);gtk_main();dove_preferences_free(prefs);g_ptr_array_free(history,TRUE);g_free(filename);g_free(saved_text);g_free(font_name);return 0;}
