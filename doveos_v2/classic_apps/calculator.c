#include "doveos.h"
#include <gdk/gdkkeysyms.h>
#include <math.h>
#include <string.h>
typedef struct {char text[64];double accumulator,memory,operand;char operation,repeat;gboolean fresh,error;} Calculator;
static Calculator calc;
static GtkWidget *window,*display,*memory_label;
static void update(void){if(display)gtk_entry_set_text(GTK_ENTRY(display),calc.error?"Cannot divide by zero or calculate this value":calc.text);if(memory_label)gtk_label_set_text(GTK_LABEL(memory_label),calc.memory!=0?"M":"");}
static void value(double v){if(!isfinite(v)){calc.error=TRUE;calc.fresh=TRUE;return;}g_snprintf(calc.text,sizeof calc.text,"%.14g",v==0?0:v);calc.fresh=TRUE;}
static double number(void){return g_ascii_strtod(calc.text,NULL);}
static void reset(void){double memory=calc.memory;memset(&calc,0,sizeof calc);calc.memory=memory;strcpy(calc.text,"0");calc.fresh=TRUE;}
static double operation(double a,double b,char op){switch(op){case '+':return a+b;case '-':return a-b;case '*':return a*b;case '/':if(b==0){calc.error=TRUE;return 0;}return a/b;default:return b;}}
static void input(const char *key){
 if(!strcmp(key,"C")){reset();update();return;}
 if(!strcmp(key,"MC")){calc.memory=0;update();return;}
 if(calc.error){if(!strcmp(key,"CE")||(strlen(key)==1&&(g_ascii_isdigit(key[0])||key[0]=='.')))reset();else return;}
 if(strlen(key)==1&&(g_ascii_isdigit(key[0])||key[0]=='.')){
  if(calc.fresh){strcpy(calc.text,"0");calc.fresh=FALSE;calc.repeat=0;}
  if(key[0]=='.'){if(!strchr(calc.text,'.'))strcat(calc.text,".");}
  else if(strlen(calc.text)<16){if(!strcmp(calc.text,"0"))calc.text[0]=0;strcat(calc.text,key);}
 }else if(!strcmp(key,"Backspace")){
  if(!calc.fresh){size_t len=strlen(calc.text);if(len)calc.text[len-1]=0;if(!calc.text[0]||!strcmp(calc.text,"-"))strcpy(calc.text,"0");}
 }else if(!strcmp(key,"CE")){strcpy(calc.text,"0");calc.fresh=TRUE;}
 else if(!strcmp(key,"+/-")){double v=number();gboolean fresh=calc.fresh;value(-v);calc.fresh=fresh;}
 else if(!strcmp(key,"MS"))calc.memory=number();
 else if(!strcmp(key,"MR"))value(calc.memory);
 else if(!strcmp(key,"M+"))calc.memory+=number();
 else if(!strcmp(key,"sqrt")){double v=number();if(v<0)calc.error=TRUE;else value(sqrt(v));}
 else if(!strcmp(key,"1/x")){double v=number();if(v==0)calc.error=TRUE;else value(1/v);}
 else if(!strcmp(key,"%")){double v=number()/100;if(calc.operation=='+'||calc.operation=='-')v*=calc.accumulator;value(v);}
 else if(!strcmp(key,"=")){
  if(calc.operation){calc.operand=number();calc.repeat=calc.operation;value(operation(calc.accumulator,calc.operand,calc.operation));calc.operation=0;}
  else if(calc.repeat)value(operation(number(),calc.operand,calc.repeat));
 }else if(strlen(key)==1&&strchr("+-*/",key[0])){
  double v=number();if(calc.operation&&!calc.fresh)v=operation(calc.accumulator,v,calc.operation);
  calc.accumulator=v;calc.operation=key[0];calc.repeat=0;value(v);
 }
 update();
}
static void click(GtkWidget *widget,gpointer data){(void)widget;input(data);}
static gboolean keys(GtkWidget *widget,GdkEventKey *event,gpointer data){
 (void)widget;(void)data;guint k=event->keyval;char key[2]={0,0};
 if(event->state&GDK_CONTROL_MASK)return FALSE;
 if(k>=GDK_0&&k<=GDK_9)key[0]='0'+k-GDK_0;
 else if(k>=GDK_KP_0&&k<=GDK_KP_9)key[0]='0'+k-GDK_KP_0;
 else if(k==GDK_period||k==GDK_KP_Decimal)key[0]='.';
 else if(k==GDK_plus||k==GDK_KP_Add)key[0]='+';
 else if(k==GDK_minus||k==GDK_KP_Subtract)key[0]='-';
 else if(k==GDK_asterisk||k==GDK_KP_Multiply)key[0]='*';
 else if(k==GDK_slash||k==GDK_KP_Divide)key[0]='/';
 else if(k==GDK_Return||k==GDK_KP_Enter||k==GDK_equal)key[0]='=';
 else if(k==GDK_percent)key[0]='%';
 else if(k==GDK_BackSpace){input("Backspace");return TRUE;}
 else if(k==GDK_Delete){input("CE");return TRUE;}
 else if(k==GDK_Escape){input("C");return TRUE;}else return FALSE;
 input(key);return TRUE;
}
static void quit(GtkWidget *widget,gpointer data){(void)widget;(void)data;gtk_main_quit();}
static void about(GtkWidget *widget,gpointer data){(void)widget;(void)data;dove_message(GTK_WINDOW(window),"DoveOS Calculator\nDoveOS standard calculator\n\nKeyboard: digits, + - * /, Enter, Backspace, Delete (CE), Esc (C).\nBuilt with DoveOS SDK " DOVEOS_SDK_VERSION);}
static void copy(GtkWidget *widget,gpointer data){(void)widget;(void)data;gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),calc.text,-1);}
static void pasted(GtkClipboard *clipboard,const gchar *text,gpointer data){(void)clipboard;(void)data;if(!text)return;gchar *trim=g_strdup(text);g_strstrip(trim);char *end;double v=g_ascii_strtod(trim,&end);if(end!=trim&&!*end&&isfinite(v)){calc.error=FALSE;value(v);update();}g_free(trim);}
static void paste(GtkWidget *widget,gpointer data){(void)widget;(void)data;gtk_clipboard_request_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),pasted,NULL);}
static GtkWidget *menu(GtkWidget *bar,const char *name){GtkWidget *item=dove_menuitem(name),*m=dove_contextmenu();gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),m);gtk_menu_shell_append(GTK_MENU_SHELL(bar),item);return m;}
static GtkWidget *item(GtkWidget *m,const char *name,GCallback callback){GtkWidget *w=dove_menuitem(name);gtk_menu_shell_append(GTK_MENU_SHELL(m),w);if(callback)g_signal_connect(w,"activate",callback,NULL);return w;}
static GtkWidget *button(const char *label){GtkWidget *b=dove_button(label);gtk_widget_set_size_request(b,45,29);GdkColor color;gdk_color_parse(strchr("0123456789.",label[0])?"#000080":"#800000",&color);gtk_widget_modify_fg(gtk_bin_get_child(GTK_BIN(b)),GTK_STATE_NORMAL,&color);g_signal_connect(b,"clicked",G_CALLBACK(click),(gpointer)label);gtk_widget_set_can_focus(b,FALSE);return b;}
static int self_test(void){int failed=0;
 #define EXPECT(v) do{if(calc.error||fabs(number()-(v))>1e-9)failed++;}while(0)
 reset();input("2");input("+");input("3");input("=");EXPECT(5);input("=");EXPECT(8);
 reset();input("2");input("+");input("3");input("*");input("4");input("=");EXPECT(20);
 reset();input("2");input("0");input("0");input("+");input("1");input("0");input("%");input("=");EXPECT(220);
 reset();input("9");input("sqrt");EXPECT(3);input("MS");input("C");input("MR");EXPECT(3);input("M+");input("MR");EXPECT(6);
 reset();input("1");input("/");input("0");input("=");if(!calc.error)failed++;input("7");EXPECT(7);
 reset();input("1");input(".");input("2");input("Backspace");EXPECT(1);input("+/-");EXPECT(-1);
 reset();input("8");input("1/x");EXPECT(.125);input("+/-");input("sqrt");if(!calc.error)failed++;
 g_print("CALCULATOR_CHECK failures=%d\n",failed);return failed?1:0;
}
int main(int argc,char **argv){if(argc>1&&!strcmp(argv[1],"--self-test"))return self_test();gtk_init(&argc,&argv);reset();window=dove_form("Calculator",334,267);gtk_window_set_resizable(GTK_WINDOW(window),FALSE);gtk_window_set_wmclass(GTK_WINDOW(window),"DoveCalculator","DoveCalculator");gtk_window_set_icon_from_file(GTK_WINDOW(window),"/media/usb0/DoveOS-v2/ClassicApps/icons/calculator-48.png",NULL);g_signal_connect(window,"destroy",G_CALLBACK(quit),NULL);g_signal_connect(window,"key-press-event",G_CALLBACK(keys),NULL);
 GtkWidget *box=dove_layout(FALSE);gtk_container_set_border_width(GTK_CONTAINER(box),6);gtk_container_add(GTK_CONTAINER(window),box);GtkWidget *bar=dove_menubar();dove_pack(box,bar,FALSE);GtkWidget *m=menu(bar,"_Edit");GtkWidget *cp=item(m,"_Copy",G_CALLBACK(copy)),*ps=item(m,"_Paste",G_CALLBACK(paste));dove_shortcut(window,cp,"activate","<Control>c");dove_shortcut(window,ps,"activate","<Control>v");m=menu(bar,"_View");GtkWidget *standard=item(m,"Standard",NULL);gtk_widget_set_sensitive(standard,FALSE);m=menu(bar,"_Help");item(m,"_About Calculator",G_CALLBACK(about));
 display=dove_textbox("0");gtk_editable_set_editable(GTK_EDITABLE(display),FALSE);gtk_entry_set_alignment(GTK_ENTRY(display),1);gtk_widget_set_can_focus(display,FALSE);dove_pack(box,display,FALSE);
 GtkWidget *row=dove_layout(TRUE);dove_pack(box,row,FALSE);memory_label=dove_label("");gtk_widget_set_size_request(memory_label,40,-1);dove_pack(row,memory_label,FALSE);const char *top[]={"Backspace","CE","C"};for(int i=0;i<3;i++)dove_pack(row,button(top[i]),TRUE);
 GtkWidget *grid=gtk_table_new(4,6,TRUE);gtk_table_set_row_spacings(GTK_TABLE(grid),4);gtk_table_set_col_spacings(GTK_TABLE(grid),4);dove_pack(box,grid,TRUE);const char *labels[]={"MC","7","8","9","/","sqrt","MR","4","5","6","*","%","MS","1","2","3","-","1/x","M+","0","+/-",".","+","="};for(int i=0;i<24;i++)gtk_table_attach_defaults(GTK_TABLE(grid),button(labels[i]),i%6,i%6+1,i/6,i/6+1);
 gtk_widget_show_all(window);gtk_main();return 0;}
