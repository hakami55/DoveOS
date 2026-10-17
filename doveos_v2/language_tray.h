/* Language badge is a normal slot in the main status tray window. */
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include "keyboard_layouts.h"
static Window language_window,language_menu;
static XftDraw *language_draw,*language_menu_draw;
static XftFont *language_font;
static XftColor language_white;
static DoveLayouts language_layouts;
static DoveLayoutInfo language_info[2];
static int language_group,language_menu_open,language_choice,language_event_base;
static int language_menu_w,language_menu_h;
static unsigned long language_blue;
static double language_next;
static double language_now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void language_close_menu(void){if(!language_menu_open)return;XUngrabPointer(display,CurrentTime);XUngrabKeyboard(display,CurrentTime);XUnmapWindow(display,language_menu);language_menu_open=0;}
static void language_render(void){
 if(!language_window)return;
 XWindowAttributes a;XGetWindowAttributes(display,language_window,&a);int y=(a.height-H)/2;if(y<0)y=0;
 XSetForeground(display,gc,language_blue);XFillRectangle(display,language_window,gc,language_left,3+y,18,18);
 const char *badge=language_info[language_group].badge;XGlyphInfo extent;XftTextExtentsUtf8(display,language_font,(const FcChar8*)badge,2,&extent);
 XftDrawStringUtf8(language_draw,&language_white,language_font,language_left+(18-extent.xOff)/2,3+y+(18-language_font->ascent-language_font->descent)/2+language_font->ascent,(const FcChar8*)badge,2);
 XFlush(display);
}
static int language_refresh(int force){
 int changed=0;XkbStateRec state;if(XkbGetState(display,XkbUseCoreKbd,&state)!=Success)return 0;
 if(force||language_now()>=language_next){
  language_next=language_now()+1;DoveLayouts l=dove_layout_load();Atom type;int format;unsigned long n,left;unsigned char *value=NULL;
  if(XGetWindowProperty(display,DefaultRootWindow(display),XInternAtom(display,"_XKB_RULES_NAMES",False),0,256,False,XA_STRING,&type,&format,&n,&left,&value)==Success&&format==8&&value){
   unsigned char *at=value,*end=value+n;for(int i=0;i<2&&at<end;i++){unsigned char *z=memchr(at,0,(size_t)(end-at));if(!z){at=end;break;}at=z+1;}
   if(at<end){unsigned char *z=memchr(at,0,(size_t)(end-at));if(z&&z-at<40){char ids[40];memcpy(ids,at,(size_t)(z-at));ids[z-at]=0;DoveLayouts live={0};char *save=NULL,*id=strtok_r(ids,",",&save);while(id&&live.count<2){if(strlen(id)>=16)break;strcpy(live.ids[live.count++],id);id=strtok_r(NULL,",",&save);}if(!id&&dove_layout_valid(&live))l=live;}}
  }if(value)XFree(value);
  if(memcmp(&l,&language_layouts,sizeof l)){language_layouts=l;for(int i=0;i<l.count;i++){if(!dove_layout_info(l.ids[i],&language_info[i])){strcpy(language_info[i].badge,"EN");strcpy(language_info[i].name,"English (US)");}}changed=1;language_close_menu();}
 }
 int group=state.group<language_layouts.count?state.group:0;if(group!=language_group){language_group=group;changed=1;}
 snprintf(hints[5],sizeof hints[5],"%s - click to switch",language_info[language_group].name);return changed;
}
static void language_menu_render(void){
 XSetForeground(display,gc,dark);XFillRectangle(display,language_menu,gc,0,0,(unsigned)language_menu_w,(unsigned)language_menu_h);
 XSetForeground(display,gc,color("#FFFFFF"));XDrawLine(display,language_menu,gc,0,0,language_menu_w-1,0);XDrawLine(display,language_menu,gc,0,0,0,language_menu_h-1);
 XSetForeground(display,gc,color("#404040"));XDrawLine(display,language_menu,gc,0,language_menu_h-1,language_menu_w-1,language_menu_h-1);XDrawLine(display,language_menu,gc,language_menu_w-1,0,language_menu_w-1,language_menu_h-1);
 for(int i=0;i<=language_layouts.count;i++){
  int y=2+i*24+(i==language_layouts.count?4:0);int selected=i==language_choice;
  if(selected){XSetForeground(display,gc,language_blue);XFillRectangle(display,language_menu,gc,2,y,(unsigned)language_menu_w-4,24);}
  const char *name=i==language_layouts.count?"Keyboard settings...":language_info[i].name;
  XftDrawStringUtf8(language_menu_draw,selected?&language_white:&text_color,text_font,28,y+16,(const FcChar8*)name,(int)strlen(name));
  if(i<language_layouts.count&&i==language_group){XSetForeground(display,gc,selected?color("#FFFFFF"):white);XFillArc(display,language_menu,gc,10,y+9,5,5,0,360*64);}
 }
 int y=2+language_layouts.count*24+1;XSetForeground(display,gc,color("#808080"));XDrawLine(display,language_menu,gc,5,y,language_menu_w-6,y);XSetForeground(display,gc,color("#FFFFFF"));XDrawLine(display,language_menu,gc,5,y+1,language_menu_w-6,y+1);XFlush(display);
}
static void language_select(void){
 int row=language_choice;language_close_menu();
 if(row<0)return;
 if(row<language_layouts.count){XkbLockGroup(display,XkbUseCoreKbd,(unsigned)row);XSync(display,False);language_refresh(1);language_render();}
 else if(fork()==0){execl("/tmp/doveos-theme/doveos-open","doveos-open","DoveSettings","--page","9","/usr/bin/doveos-settings-ui","language",(char*)NULL);_exit(127);}
}
static void language_open_menu(void){
 language_refresh(1);language_choice=language_group;language_menu_w=180;
 for(int i=0;i<language_layouts.count;i++){XGlyphInfo e;XftTextExtentsUtf8(display,text_font,(const FcChar8*)language_info[i].name,(int)strlen(language_info[i].name),&e);if(e.xOff+40>language_menu_w)language_menu_w=e.xOff+40;}
 language_menu_h=language_layouts.count*24+32;Window child;int x,y;XTranslateCoordinates(display,language_window,DefaultRootWindow(display),0,0,&x,&y,&child);x+=language_left;y-=language_menu_h+4;if(y<2)y=2;if(x+language_menu_w>DisplayWidth(display,DefaultScreen(display)))x=DisplayWidth(display,DefaultScreen(display))-language_menu_w-2;
 XMoveResizeWindow(display,language_menu,x,y,(unsigned)language_menu_w,(unsigned)language_menu_h);XMapRaised(display,language_menu);language_menu_open=1;language_menu_render();
 XGrabPointer(display,language_menu,False,ButtonPressMask|PointerMotionMask,GrabModeAsync,GrabModeAsync,None,None,CurrentTime);XGrabKeyboard(display,language_menu,False,GrabModeAsync,GrabModeAsync,CurrentTime);
}
static int language_handle(XEvent *e){
 if(e->type==language_event_base){if(language_refresh(1))language_render();return 1;}
 if(e->xany.window!=language_menu)return 0;
 if(e->type==Expose)language_menu_render();
 if(e->type==ButtonPress){int y=e->xbutton.y;language_choice=y<2? -1:y>=2+language_layouts.count*24+4?language_layouts.count:(y-2)/24;if(e->xbutton.x<0||e->xbutton.x>=language_menu_w||y<0||y>=language_menu_h)language_choice=-1;language_select();}
 if(e->type==MotionNotify){int y=e->xmotion.y,next=y>=2+language_layouts.count*24+4?language_layouts.count:(y-2)/24;if(next<0||next>language_layouts.count)next=-1;if(next!=language_choice){language_choice=next;language_menu_render();}}
 if(e->type==KeyPress){KeySym key=XLookupKeysym(&e->xkey,0);if(key==XK_Escape)language_close_menu();else if(key==XK_Return)language_select();else if(key==XK_Up||key==XK_Down){language_choice=(language_choice+(key==XK_Down?1:language_layouts.count))%(language_layouts.count+1);language_menu_render();}}
 return 1;
}
static void language_init(void){
 int opcode,error,major=XkbMajorVersion,minor=XkbMinorVersion;XkbQueryExtension(display,&opcode,&language_event_base,&error,&major,&minor);XkbSelectEvents(display,XkbUseCoreKbd,XkbStateNotifyMask|XkbMapNotifyMask|XkbNewKeyboardNotifyMask,XkbStateNotifyMask|XkbMapNotifyMask|XkbNewKeyboardNotifyMask);
 language_blue=color("#000080");language_font=XftFontOpenName(display,DefaultScreen(display),"DejaVu Sans-8:bold:antialias=false");if(!language_font)language_font=text_font;XftColorAllocName(display,DefaultVisual(display,DefaultScreen(display)),DefaultColormap(display,DefaultScreen(display)),"#FFFFFF",&language_white);
 language_window=window;
 XSetWindowAttributes a={0};a.override_redirect=True;a.background_pixel=dark;language_menu=XCreateWindow(display,DefaultRootWindow(display),0,0,180,80,0,CopyFromParent,InputOutput,CopyFromParent,CWOverrideRedirect|CWBackPixel,&a);XStoreName(display,language_menu,"DoveOS layout menu");XSelectInput(display,language_menu,ExposureMask|ButtonPressMask|PointerMotionMask|KeyPressMask);
 language_draw=XftDrawCreate(display,language_window,DefaultVisual(display,DefaultScreen(display)),DefaultColormap(display,DefaultScreen(display)));language_menu_draw=XftDrawCreate(display,language_menu,DefaultVisual(display,DefaultScreen(display)),DefaultColormap(display,DefaultScreen(display)));language_refresh(1);
}
