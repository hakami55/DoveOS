/* SPDX-License-Identifier: GPL-2.0-or-later
 * scrcpy 4.1 framing; Realtek decoding remains in an isolated host broker.
 * GTK owns input normally: no grabs, USB resets or firmware writes. */
#define _POSIX_C_SOURCE 200809L
#include "doveos.h"
#include <gdk/gdkkeysyms.h>
#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "vlc_rtk_protocol.h"
static GtkWidget *window,*area,*label;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static _Atomic int video=-1,control=-1,decoder=-1,ended;
static int ready,failed,direct;
static unsigned width,height,frames,shown,buttons;
static int view_x,view_y,view_w=640,view_h=360;
static const unsigned dw=640,dh=360;
static unsigned char *latest,*drawing;
/* The video stays on VOUT. Only this tiny cursor uses the transparent fb0
 * graphics plane, which is cleared when the desktop regains the display. */
static int cursor_fd=-1,cursor_attempted,cursor_x=-1,cursor_y=-1;
static unsigned cursor_moves;
static uint32_t *cursor_fb;
static const char *cursor_arrow[]={
 "#","##","#.#","#..#","#...#","#....#","#.....#","#......#",
 "#.......#","#........#","#.........#","#..........#","#......#####",
 "#...#..#","#..##..#","#.# #...#","##  #...#","#    #...#",
 "     #...#","      #.#","       #"};
static void cursor_clear(void){
 if(!cursor_fb||cursor_x<0)return;
 for(int y=0;y<21;y++)for(int x=0;x<13;x++)
  if(cursor_x+x<1280&&cursor_y+y<720)cursor_fb[(cursor_y+y)*1280+cursor_x+x]=0;
}
static void cursor_close(void){
 cursor_clear();if(cursor_fb)munmap(cursor_fb,1280*720*4);cursor_fb=NULL;
 if(cursor_fd>=0)close(cursor_fd);
 cursor_fd=-1;
 if(cursor_attempted)printf("DOVEDEX_CURSOR_CLOSED moves=%u\n",cursor_moves);
}
static void cursor_update(void){
 if(!direct)return;
 if(!cursor_attempted){
  cursor_attempted=1;struct fb_var_screeninfo v={0};struct fb_fix_screeninfo f={0};
  cursor_fd=open("/dev/fb0",O_RDWR|O_CLOEXEC);
  if(cursor_fd<0||ioctl(cursor_fd,FBIOGET_VSCREENINFO,&v)||ioctl(cursor_fd,FBIOGET_FSCREENINFO,&f)||
     v.xres!=1280||v.yres!=720||v.bits_per_pixel!=32||f.line_length!=5120||f.smem_len<1280*720*4||
     v.xoffset||v.yoffset||v.red.offset!=16||v.green.offset!=8||v.blue.offset!=0||v.transp.offset!=24||
     v.red.length!=8||v.green.length!=8||v.blue.length!=8||v.transp.length!=8){
   fprintf(stderr,"DOVEDEX_CURSOR_FRAMEBUFFER_UNAVAILABLE errno=%d\n",errno);
   if(cursor_fd>=0)close(cursor_fd);
   cursor_fd=-1;return;
  }
  void *map=mmap(NULL,1280*720*4,PROT_READ|PROT_WRITE,MAP_SHARED,cursor_fd,0);
  if(map==MAP_FAILED){fprintf(stderr,"DOVEDEX_CURSOR_MAP_FAILED errno=%d\n",errno);close(cursor_fd);cursor_fd=-1;return;}
  cursor_fb=map;puts("DOVEDEX_CURSOR_OVERLAY_READY size=1280x720");fflush(stdout);
 }
 if(!cursor_fb)return;
 gint x,y;gdk_window_get_pointer(area->window,&x,&y,NULL);
 int px=CLAMP((int)((x-view_x)*1280.0/view_w),0,1279);
 int py=CLAMP((int)((y-view_y)*720.0/view_h),0,719);
 if(px==cursor_x&&py==cursor_y)return;
 cursor_clear();cursor_x=px;cursor_y=py;
 for(int cy=0;cy<21;cy++)for(int cx=0;cursor_arrow[cy][cx];cx++){
  char c=cursor_arrow[cy][cx];
  if(c!=' '&&px+cx<1280&&py+cy<720)cursor_fb[(py+cy)*1280+px+cx]=c=='#'?0xff101820u:0xffffffffu;
 }
 __sync_synchronize();cursor_moves++;
}
static uint32_t be32(const unsigned char *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint64_t be64(const unsigned char *p){return (uint64_t)be32(p)<<32|be32(p+4);}
static void put32(unsigned char *p,uint32_t v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static void put16(unsigned char *p,unsigned v){p[0]=v>>8;p[1]=v;}
static int io(int fd,void *p,size_t n,int writing){
 while(n){ssize_t k=writing?send(fd,p,n,MSG_NOSIGNAL):recv(fd,p,n,0);if(k<0&&errno==EINTR)continue;if(k<0&&(errno==EAGAIN||errno==EWOULDBLOCK)&&!ended)continue;if(k<=0)return -1;p=(char*)p+k;n-=(size_t)k;}return 0;
}
static int connect_tcp(unsigned port){
 int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return -1;
 struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons(port),.sin_addr={htonl(INADDR_LOOPBACK)}};
 struct timeval timeout={1,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
 if(connect(fd,(struct sockaddr*)&a,sizeof a)){close(fd);return -1;}return fd;
}
#include "dovedex-audio.h"
static void stop_io(void){
 pthread_mutex_lock(&lock);ended=1;int fds[]={video,control,decoder,audio_socket};
 for(unsigned i=0;i<4;i++)if(fds[i]>=0)shutdown(fds[i],SHUT_RDWR);
 pthread_mutex_unlock(&lock);
}
static unsigned char clip(int v){return v<0?0:v>255?255:(unsigned char)v;}
static void *receive_frames(void *unused){
 if(direct){
  (void)unused;rtk_msg m;
  while(!io(decoder,&m,sizeof m,0)){
   if(m.magic!=RTK_MAGIC||m.type!=RTK_FRAME||m.size)break;
   pthread_mutex_lock(&lock);frames++;shown++;pthread_mutex_unlock(&lock);
  }
  stop_io();return NULL;
 }
 (void)unused;size_t n=dw*dh*3/2;unsigned char *nv=malloc(n),*rgb=malloc(dw*dh*3);rtk_msg m;
 if(!nv||!rgb){free(nv);free(rgb);stop_io();return NULL;}
 while(!io(decoder,&m,sizeof m,0)){
  if(m.magic!=RTK_MAGIC||m.type!=RTK_FRAME||m.size!=n||m.width!=dw||m.height!=dh||io(decoder,nv,n,0))break;
  for(unsigned y=0;y<dh;y++)for(unsigned x=0;x<dw;x++){
   int Y=nv[y*dw+x]-16;unsigned uvi=dw*dh+(y/2)*dw+(x&~1u);int U=nv[uvi]-128,V=nv[uvi+1]-128;
   unsigned at=(y*dw+x)*3;rgb[at]=clip((298*Y+409*V+128)>>8);rgb[at+1]=clip((298*Y-100*U-208*V+128)>>8);rgb[at+2]=clip((298*Y+516*U+128)>>8);
  }
  pthread_mutex_lock(&lock);memcpy(latest,rgb,dw*dh*3);ready=1;frames++;pthread_mutex_unlock(&lock);
 }
 free(nv);free(rgb);stop_io();return NULL;
}
static void *receive_video(void *arg){
 unsigned port=(unsigned)(uintptr_t)arg;unsigned char header[64];pthread_t reader,audio_reader;int started=0,audio_started=0;
 video=connect_tcp(port);if(video<0||io(video,header,1,0)||header[0])goto fail;
 if(audio_fifo){audio_socket=connect_tcp(port);if(audio_socket<0)goto fail;}
 control=connect_tcp(port);if(control<0||io(video,header,64,0)||io(video,header,4,0))goto fail;
 if(audio_fifo){if(pthread_create(&audio_reader,NULL,receive_audio,NULL))goto fail;audio_started=1;}
 int is_hevc=be32(header)==0x68323635;
 if(be32(header)!=0x68323634&&(!direct||!is_hevc))goto fail;
 if(io(video,header,12,0)||!(header[0]&0x80))goto fail;
 width=be32(header+4);height=be32(header+8);
 if(width<dw||height<dh||width>1920||height>1088||(width&1)||(height&1))goto fail;
 decoder=socket(AF_UNIX,SOCK_STREAM,0);if(decoder<0)goto fail;
 struct sockaddr_un a={.sun_family=AF_UNIX};strcpy(a.sun_path,RTK_SOCKET);
 if(connect(decoder,(struct sockaddr*)&a,sizeof a))goto fail;
 rtk_msg m={.magic=RTK_MAGIC,.type=RTK_INIT,.width=width,.height=height,.flags=direct?(unsigned)is_hevc:(dw<<16)|dh};
 if(io(decoder,&m,sizeof m,1)||io(decoder,&m,sizeof m,0)||m.type!=RTK_ACK)goto fail;
 printf("DOVEDEX_HARDWARE_CONNECTED size=%ux%u output=%ux%u direct=%d\n",width,height,direct?width:dw,direct?height:dh,direct);fflush(stdout);
 if(pthread_create(&reader,NULL,receive_frames,NULL))goto fail;
 started=1;
 const char *test_app=getenv("DOVEDEX_TEST_APP");
 if(test_app&&!strcmp(test_app,"org.doveos.dovedexmotion")){
  unsigned char command[64]={16,(unsigned char)strlen(test_app)};
  memcpy(command+2,test_app,strlen(test_app));
  if(io(control,command,2+strlen(test_app),1))goto fail;
  puts("DOVEDEX_TEST_APP_REQUESTED");
 }
 unsigned char *packet=malloc(RTK_LIMIT),configuration[65536];size_t config_size=0;if(!packet)goto fail;
 while(!io(video,header,12,0)){
  if(header[0]&0x80){fprintf(stderr,"Display size changed; reconnect required.\n");break;}
  unsigned size=be32(header+8);if(!size||size>RTK_LIMIT||io(video,packet,size,0))break;
  if(is_hevc&&(header[0]&0x40)){if(size>sizeof configuration)break;memcpy(configuration,packet,size);config_size=size;continue;}
  if(is_hevc&&config_size&&(header[0]&0x20)){if(size+config_size>RTK_LIMIT)break;memmove(packet+config_size,packet,size);memcpy(packet,configuration,config_size);size+=config_size;}
  uint64_t stamp=be64(header);m=(rtk_msg){.magic=RTK_MAGIC,.type=RTK_PACKET,.size=size,.pts=(int64_t)(stamp&0x1fffffffffffffffull)};
  if(io(decoder,&m,sizeof m,1)||io(decoder,packet,size,1))break;
 }
 free(packet);
fail:
 pthread_mutex_lock(&lock);failed=frames==0;pthread_mutex_unlock(&lock);stop_io();if(started)pthread_join(reader,NULL);if(audio_started)pthread_join(audio_reader,NULL);return NULL;
}
static gboolean expose(GtkWidget *w,GdkEventExpose *event,gpointer unused){
 (void)event;(void)unused;
 double scale=MIN((double)w->allocation.width/dw,(double)w->allocation.height/dh);
 view_w=MAX(1,(int)(dw*scale));view_h=MAX(1,(int)(dh*scale));view_x=(w->allocation.width-view_w)/2;view_y=(w->allocation.height-view_h)/2;
 if(direct)return FALSE;
 gdk_draw_rectangle(w->window,w->style->black_gc,TRUE,0,0,w->allocation.width,w->allocation.height);
 GdkPixbuf *raw=gdk_pixbuf_new_from_data(drawing,GDK_COLORSPACE_RGB,FALSE,8,dw,dh,dw*3,NULL,NULL);
 if(raw){GdkPixbuf *scaled=gdk_pixbuf_scale_simple(raw,view_w,view_h,GDK_INTERP_NEAREST);
  if(scaled){gdk_draw_pixbuf(w->window,w->style->fg_gc[GTK_STATE_NORMAL],scaled,0,0,view_x,view_y,view_w,view_h,GDK_RGB_DITHER_NONE,0,0);g_object_unref(scaled);}g_object_unref(raw);
 }return FALSE;
}
static gboolean tick(gpointer unused){
 (void)unused;static unsigned wake_ticks;if(++wake_ticks%60==0){int f=open("/tmp/doveos-power-wake",O_CREAT|O_WRONLY,0600);if(f>=0)close(f);}
 if(!access("/tmp/dovedex-runtime-v1/session.stop",F_OK)){puts("DOVEDEX_PHYSICAL_QUIT_COMBO");gtk_widget_destroy(window);return FALSE;}
 unsigned count;int end,bad;
 pthread_mutex_lock(&lock);if(ready){memcpy(drawing,latest,dw*dh*3);ready=0;shown++;gtk_widget_queue_draw(area);}count=frames;end=ended;bad=failed;pthread_mutex_unlock(&lock);
 if(count&&!end)cursor_update();
 char text[160];snprintf(text,sizeof text,"%s | Hardware frames: %u | Displayed: %u | Sound %s",end?(bad?"Connection failed - see diagnostics":"Disconnected"):"Android desktop",count,shown,audio_fifo?"requested":"off");gtk_label_set_text(GTK_LABEL(label),text);if(end)gtk_widget_destroy(window);return !end;
}
static void send_control(unsigned char *message,size_t length){
 if(control<0||ended)return;
 ssize_t n=send(control,message,length,MSG_DONTWAIT|MSG_NOSIGNAL);
 if(n!=(ssize_t)length){fprintf(stderr,"Control queue unavailable; closing session.\n");stop_io();}
}
static void touch(double x,double y,unsigned action,unsigned button){
 if(!width||!height)return;
 x=CLAMP((x-view_x)*dw/view_w,0,dw-1);y=CLAMP((y-view_y)*dh/view_h,0,dh-1);
 unsigned char b[32]={2,(unsigned char)action};memset(b+2,0xff,8);b[9]=0xff; /* mouse id = -1 */
 put32(b+10,(unsigned)(x*width/dw));put32(b+14,(unsigned)(y*height/dh));put16(b+18,width);put16(b+20,height);put16(b+22,buttons?65535:0);put32(b+24,button);put32(b+28,buttons);send_control(b,sizeof b);
}
static gboolean motion(GtkWidget *w,GdkEventMotion *e,gpointer unused){(void)w;(void)unused;touch(e->x,e->y,buttons?2:7,0);return TRUE;}
static gboolean click(GtkWidget *w,GdkEventButton *e,gpointer unused){
 (void)unused;gtk_widget_grab_focus(w);unsigned b=e->button==1?1:e->button==3?2:e->button==2?4:0;if(!b)return FALSE;
 if(e->type==GDK_BUTTON_PRESS)buttons|=b;else buttons&=~b;touch(e->x,e->y,e->type==GDK_BUTTON_PRESS?0:1,b);return TRUE;
}
static gboolean scroll(GtkWidget *w,GdkEventScroll *e,gpointer unused){
 (void)w;(void)unused;if(!width)return TRUE;
 double sx=CLAMP((e->x-view_x)/view_w,0.0,1.0),sy=CLAMP((e->y-view_y)/view_h,0.0,1.0);unsigned char b[21]={3};put32(b+1,(unsigned)(sx*width));put32(b+5,(unsigned)(sy*height));put16(b+9,width);put16(b+11,height);put16(b+13,e->direction==GDK_SCROLL_LEFT?2048:e->direction==GDK_SCROLL_RIGHT?(uint16_t)-2048:0);put16(b+15,e->direction==GDK_SCROLL_UP?2048:(uint16_t)-2048);put32(b+17,buttons);send_control(b,sizeof b);return TRUE;
}
static unsigned keycode(guint k){
 if(k>=GDK_a&&k<=GDK_z)return k-GDK_a+29;
 if(k>=GDK_A&&k<=GDK_Z)return k-GDK_A+29;
 if(k>=GDK_0&&k<=GDK_9)return k-GDK_0+7;
 switch(k){case GDK_space:return 62;case GDK_Return:return 66;case GDK_BackSpace:return 67;case GDK_Tab:return 61;case GDK_Escape:return 111;case GDK_Left:return 21;case GDK_Right:return 22;case GDK_Up:return 19;case GDK_Down:return 20;case GDK_Shift_L:return 59;case GDK_Shift_R:return 60;case GDK_Control_L:return 113;case GDK_Control_R:return 114;case GDK_Alt_L:return 57;case GDK_Alt_R:return 58;default:return 0;}
}
static gboolean key(GtkWidget *w,GdkEventKey *e,gpointer unused){
 (void)w;(void)unused;if((e->state&GDK_MOD1_MASK)&&(e->keyval==GDK_q||e->keyval==GDK_Q)){gtk_widget_destroy(window);return TRUE;}
 unsigned code=keycode(e->keyval);if(!code)return FALSE;unsigned char b[14]={0,e->type==GDK_KEY_PRESS?0:1};put32(b+2,code);
 put32(b+10,((e->state&GDK_SHIFT_MASK)?1:0)|((e->state&GDK_CONTROL_MASK)?4096:0)|((e->state&GDK_MOD1_MASK)?2:0));send_control(b,sizeof b);return TRUE;
}
static void close_window(GtkWidget *w,gpointer unused){(void)w;(void)unused;cursor_close();stop_io();gtk_main_quit();}
int main(int argc,char **argv){
 if(argc<2)return 2;
 direct=argc>2&&!strcmp(argv[2],"--direct");
 audio_fifo=getenv("DOVEDEX_AUDIO_FIFO");
 if(audio_fifo&&strcmp(audio_fifo,"/tmp/dovedex-runtime-v1/audio.fifo"))return 2;
 const char *delay=getenv("DOVEDEX_AUDIO_DELAY_MS");
 if(delay){char *last;long ms=strtol(delay,&last,10);if(!*delay||*last||ms<0||ms>250)return 2;audio_delay_ms=(unsigned)ms;}
 char *end;long port=strtol(argv[1],&end,10);if(*end||port<1024||port>65535)return 2;
 signal(SIGPIPE,SIG_IGN);gtk_init(&argc,&argv);if(!direct){latest=calloc(dw*dh,3);drawing=calloc(dw*dh,3);if(!latest||!drawing)return 3;}
 window=dove_form("DoveDeX - Android desktop",680,425);gtk_window_set_decorated(GTK_WINDOW(window),FALSE);gtk_window_set_keep_above(GTK_WINDOW(window),TRUE);GtkWidget *box=dove_panel(FALSE,0);gtk_container_add(GTK_CONTAINER(window),box);
 area=gtk_drawing_area_new();gtk_widget_set_size_request(area,dw,dh);gtk_widget_set_can_focus(area,TRUE);gtk_widget_add_events(area,GDK_POINTER_MOTION_MASK|GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_SCROLL_MASK|GDK_KEY_PRESS_MASK|GDK_KEY_RELEASE_MASK);gtk_box_pack_start(GTK_BOX(box),area,TRUE,TRUE,0);
 label=dove_label("Connecting hardware decoder. Ctrl+Alt+Shift+Q returns to DoveOS.");dove_add(box,label);
 g_signal_connect(area,"expose-event",G_CALLBACK(expose),NULL);g_signal_connect(area,"motion-notify-event",G_CALLBACK(motion),NULL);g_signal_connect(area,"button-press-event",G_CALLBACK(click),NULL);g_signal_connect(area,"button-release-event",G_CALLBACK(click),NULL);g_signal_connect(area,"scroll-event",G_CALLBACK(scroll),NULL);g_signal_connect(area,"key-press-event",G_CALLBACK(key),NULL);g_signal_connect(area,"key-release-event",G_CALLBACK(key),NULL);g_signal_connect(window,"destroy",G_CALLBACK(close_window),NULL);
 pthread_t worker;if(pthread_create(&worker,NULL,receive_video,(void*)(uintptr_t)port))return 4;gtk_widget_show_all(window);gtk_widget_hide(label);gtk_window_fullscreen(GTK_WINDOW(window));gtk_window_move(GTK_WINDOW(window),0,0);gtk_window_resize(GTK_WINDOW(window),gdk_screen_width(),gdk_screen_height());gtk_widget_grab_focus(area);g_timeout_add(16,tick,NULL);gtk_main();stop_io();pthread_join(worker,NULL);
 printf("DOVEDEX_VIEWER_END frames=%u shown=%u\n",frames,shown);return frames?0:5;
}
