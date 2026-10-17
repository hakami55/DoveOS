#include "doveos.h"
/* Explorer-style dialogs preserve GtkFileChooser APIs and do not write files. */
int main(int argc,char **argv){
 gtk_init(&argc,&argv);gboolean save=argc>1&&!strcmp(argv[1],"--save");
 GtkWidget *dialog=save?dove_savefiledialog(NULL,"Save File"):dove_openfiledialog(NULL,"Open File");
 if(save)gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),"Document.txt");
 else if(argc>1&&!strcmp(argv[1],"--multiple"))gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog),TRUE);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT){GSList *files=gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));for(GSList *i=files;i;i=i->next)g_print("%s\n",(char*)i->data);g_slist_free_full(files,g_free);}
 gtk_widget_destroy(dialog);return 0;
}
