#ifndef DOVE_KEYBOARD_SETTINGS_H
#define DOVE_KEYBOARD_SETTINGS_H
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifndef DOVE_KEYBOARD_FILE
#define DOVE_KEYBOARD_FILE "/media/usb0/DoveOS-v2/home/keyboard-settings.txt"
#endif
#define DOVE_KEY_BINDINGS 48
typedef struct {int source,action;} DoveKeyBinding;
typedef struct {int swap,delay,interval,layout,toggle,fn[12],count;DoveKeyBinding bindings[DOVE_KEY_BINDINGS];} DoveKeyboard;
/* Negative actions are fixed helpers; never arbitrary shell commands. */
enum {DOVE_KEY_IGNORE=-1,DOVE_KEY_SETTINGS=-2,DOVE_KEY_FILES=-3,DOVE_KEY_TERMINAL=-4,
 DOVE_KEY_ALT_TAB=-20,DOVE_KEY_ALT_F4=-21,DOVE_KEY_COPY=-22,DOVE_KEY_PASTE=-23,DOVE_KEY_SELECT_ALL=-24};
static inline int dove_function_code(int i){return i<10?KEY_F1+i:i==10?KEY_F11:KEY_F12;}
static inline int dove_function_index(int code){return code>=KEY_F1&&code<=KEY_F10?code-KEY_F1:code==KEY_F11?10:code==KEY_F12?11:-1;}
static inline int dove_key_source_valid(int code){
 return code>0&&code<248&&code!=KEY_LEFTCTRL&&code!=KEY_RIGHTCTRL&&code!=KEY_LEFTALT&&code!=KEY_RIGHTALT&&code!=KEY_LEFTSHIFT&&code!=KEY_RIGHTSHIFT&&code!=KEY_LEFTMETA&&code!=KEY_RIGHTMETA&&code!=KEY_POWER&&code!=KEY_SLEEP&&code!=KEY_WAKEUP;
}
static inline int dove_key_action_valid(int action){return action==0||dove_key_source_valid(action)||action==DOVE_KEY_IGNORE||action==DOVE_KEY_SETTINGS||action==DOVE_KEY_FILES||action==DOVE_KEY_TERMINAL||(action<=DOVE_KEY_ALT_TAB&&action>=DOVE_KEY_SELECT_ALL);}
static inline DoveKeyboard dove_keyboard_default(void){DoveKeyboard k={0};k.delay=500;k.interval=40;k.fn[3]=KEY_MUTE;return k;}
static inline DoveKeyboard dove_keyboard_load(void){
 DoveKeyboard k=dove_keyboard_default(),v=k;FILE *f=fopen(DOVE_KEYBOARD_FILE,"r");if(!f)return k;
 char tag[32];int n=fscanf(f,"%31s %d %d %d %d %d",tag,&v.swap,&v.delay,&v.interval,&v.layout,&v.toggle);
 if(n!=6||strcmp(tag,"DOVE_KEYBOARD1")||v.swap<0||v.swap>1||v.delay<200||v.delay>2000||v.interval<20||v.interval>250||v.layout<0||v.layout>2||v.toggle<0||v.toggle>1){fclose(f);return k;}
 for(int i=0;i<12;i++)if(fscanf(f,"%d",&v.fn[i])!=1||(v.fn[i]&&!dove_key_source_valid(v.fn[i]))||dove_function_index(v.fn[i])>=0){fclose(f);return k;}
 for(int i=0;i<12;i++)for(int j=i+1;j<12;j++)if(v.fn[i]&&v.fn[i]==v.fn[j]){fclose(f);return k;}
 int source,action;while(fscanf(f,"%d %d",&source,&action)==2){
  if(v.count>=DOVE_KEY_BINDINGS||!dove_key_source_valid(source)||!dove_key_action_valid(action)){fclose(f);return k;}
  for(int i=0;i<v.count;i++)if(v.bindings[i].source==source){fclose(f);return k;}
  v.bindings[v.count++]=(DoveKeyBinding){source,action};
 }fclose(f);return v;
}
static inline int dove_keyboard_save(DoveKeyboard k){
 char path[200];snprintf(path,sizeof path,"%s.%ld.new",DOVE_KEYBOARD_FILE,(long)getpid());FILE *f=fopen(path,"w");if(!f)return -1;
 int bad=fprintf(f,"DOVE_KEYBOARD1 %d %d %d %d %d\n",k.swap,k.delay,k.interval,k.layout,k.toggle)<0;
 for(int i=0;i<12;i++)bad|=fprintf(f,"%d%c",k.fn[i],i==11?'\n':' ')<0;
 for(int i=0;i<k.count;i++)bad|=fprintf(f,"%d %d\n",k.bindings[i].source,k.bindings[i].action)<0;
 bad|=fflush(f)!=0;bad|=fsync(fileno(f))!=0;bad|=fclose(f)!=0;
 if(!bad&&!rename(path,DOVE_KEYBOARD_FILE))return 0;
 unlink(path);return -1;
}
static inline int dove_keyboard_action(const DoveKeyboard *k,int code,int modified){
 if(modified)return code; /* Preserve Alt+F4, app shortcuts and the streaming quit chord. */
 for(int i=0;i<k->count;i++)if(k->bindings[i].source==code)return k->bindings[i].action?k->bindings[i].action:code;
 if(k->swap){int i=dove_function_index(code);if(i>=0&&k->fn[i])return k->fn[i];for(i=0;i<12;i++)if(k->fn[i]&&k->fn[i]==code)return dove_function_code(i);}
 return code;
}
static inline const char *dove_key_name(int code,char *buf,size_t size){
 int i=dove_function_index(code);if(i>=0){snprintf(buf,size,"F%d",i+1);return buf;}
 static const struct {int code;const char *name;} keys[]={
  {KEY_A,"A"},{KEY_B,"B"},{KEY_C,"C"},{KEY_D,"D"},{KEY_E,"E"},{KEY_F,"F"},{KEY_G,"G"},{KEY_H,"H"},{KEY_I,"I"},{KEY_J,"J"},{KEY_K,"K"},{KEY_L,"L"},{KEY_M,"M"},{KEY_N,"N"},{KEY_O,"O"},{KEY_P,"P"},{KEY_Q,"Q"},{KEY_R,"R"},{KEY_S,"S"},{KEY_T,"T"},{KEY_U,"U"},{KEY_V,"V"},{KEY_W,"W"},{KEY_X,"X"},{KEY_Y,"Y"},{KEY_Z,"Z"},
  {KEY_0,"0"},{KEY_1,"1"},{KEY_2,"2"},{KEY_3,"3"},{KEY_4,"4"},{KEY_5,"5"},{KEY_6,"6"},{KEY_7,"7"},{KEY_8,"8"},{KEY_9,"9"},
  {KEY_LEFTCTRL,"Left Ctrl"},{KEY_RIGHTCTRL,"Right Ctrl"},{KEY_LEFTALT,"Left Alt"},{KEY_RIGHTALT,"Right Alt"},{KEY_LEFTSHIFT,"Left Shift"},{KEY_RIGHTSHIFT,"Right Shift"},{KEY_LEFTMETA,"Left Windows"},{KEY_RIGHTMETA,"Right Windows"},{KEY_FN,"Fn"},
  {KEY_BACKSPACE,"Backspace"},{KEY_DELETE,"Delete"},{KEY_INSERT,"Insert"},{KEY_HOME,"Home"},{KEY_END,"End"},{KEY_CAPSLOCK,"Caps Lock"},{KEY_NUMLOCK,"Num Lock"},{KEY_SCROLLLOCK,"Scroll Lock"},{KEY_SYSRQ,"Print Screen"},{KEY_PAUSE,"Pause"},{KEY_COMPOSE,"Menu"},
  {KEY_MINUS,"Minus"},{KEY_EQUAL,"Equal"},{KEY_LEFTBRACE,"Left bracket"},{KEY_RIGHTBRACE,"Right bracket"},{KEY_SEMICOLON,"Semicolon"},{KEY_APOSTROPHE,"Apostrophe"},{KEY_GRAVE,"Backtick"},{KEY_BACKSLASH,"Backslash"},{KEY_COMMA,"Comma"},{KEY_DOT,"Period"},{KEY_SLASH,"Slash"}};
 for(unsigned n=0;n<sizeof keys/sizeof keys[0];n++)if(keys[n].code==code)return keys[n].name;
 switch(code){case 0:return "Original behavior";case KEY_MUTE:return "Mute";case KEY_VOLUMEUP:return "Volume up";case KEY_VOLUMEDOWN:return "Volume down";case KEY_BRIGHTNESSUP:return "Brightness up";case KEY_BRIGHTNESSDOWN:return "Brightness down";case KEY_PLAYPAUSE:return "Play / pause";case KEY_NEXTSONG:return "Next track";case KEY_PREVIOUSSONG:return "Previous track";case KEY_ENTER:return "Enter";case KEY_ESC:return "Escape";case KEY_TAB:return "Tab";case KEY_SPACE:return "Space";case KEY_UP:return "Up";case KEY_DOWN:return "Down";case KEY_LEFT:return "Left";case KEY_RIGHT:return "Right";case KEY_PAGEUP:return "Page Up";case KEY_PAGEDOWN:return "Page Down";case DOVE_KEY_IGNORE:return "Disabled";case DOVE_KEY_SETTINGS:return "Open Control Panel";case DOVE_KEY_FILES:return "Open Files";case DOVE_KEY_TERMINAL:return "Open Terminal";case DOVE_KEY_ALT_TAB:return "Switch windows (Alt+Tab)";case DOVE_KEY_ALT_F4:return "Close window (Alt+F4)";case DOVE_KEY_COPY:return "Copy (Ctrl+C)";case DOVE_KEY_PASTE:return "Paste (Ctrl+V)";case DOVE_KEY_SELECT_ALL:return "Select all (Ctrl+A)";}
 snprintf(buf,size,"Key code %d",code);return buf;
}
#endif
