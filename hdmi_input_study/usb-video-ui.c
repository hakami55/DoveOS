/* SDK 1.1 UVC preview: selected camera only, no firmware or GPIO access. */
#define _POSIX_C_SOURCE 200809L
#include "doveos.h"
#include "dove_ui_layout.h"
#include <libuvc/libuvc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
static uvc_context_t *context;
static uvc_device_t **devices;
static uvc_device_handle_t *handle;
static uvc_frame_t *rgb_frame;
static GtkListStore *rows;
static GtkWidget *view,*status,*picture,*start,*refresh,*mode;
static GtkWidget *audio_option,*audio_status;
static GPid audio_pid;
static guint audio_watch;
static gchar *audio_log;
static gboolean audio_test;
static int audio_test_result=5;
static void audio_last_line(char *last,size_t size){
 gchar *text=NULL;gsize length=0;
 if(audio_log&&g_file_get_contents(audio_log,&text,&length,NULL)){
  char *line=text,*next;while(line&&*line){next=strchr(line,'\n');if(next)*next=0;if(*line)snprintf(last,size,"%s",line);line=next?next+1:NULL;}g_free(text);
 }
}
static void audio_done(GPid pid,gint child_status,gpointer unused){
 (void)unused;audio_pid=0;audio_watch=0;g_spawn_close_pid(pid);
 char last[320]="Audio stopped. Video can continue.";audio_last_line(last,sizeof last);
 if(!WIFEXITED(child_status)||WEXITSTATUS(child_status)!=0){
  if(!strncmp(last,"Audio stopped:",14))snprintf(last,sizeof last,"Audio stopped unexpectedly. Video can continue.");
 }
 gtk_label_set_text(GTK_LABEL(audio_status),last);
 if(audio_test){audio_test_result=WIFEXITED(child_status)&&WEXITSTATUS(child_status)==0&&strstr(last,"selftest=1")?0:5;printf("CAPTURE_AUDIO_UI_CHECK status=%d log=%s\n",audio_test_result,last);gtk_main_quit();}
}
static void stop_audio(void){
 if(audio_pid){
  if(audio_watch){g_source_remove(audio_watch);audio_watch=0;}
  kill(audio_pid,SIGTERM);int child_status=0,done=0;
  for(int i=0;i<40;i++){pid_t p=waitpid(audio_pid,&child_status,WNOHANG);if(p==audio_pid||(p<0&&errno==ECHILD)){done=1;break;}g_usleep(25000);}
  if(!done){kill(audio_pid,SIGKILL);while(waitpid(audio_pid,&child_status,0)<0&&errno==EINTR){}}
  g_spawn_close_pid(audio_pid);audio_pid=0;
 }
 if(audio_log){unlink(audio_log);g_free(audio_log);audio_log=NULL;}
 if(audio_status)gtk_label_set_text(GTK_LABEL(audio_status),"Audio stopped.");
}
static gboolean spawn_audio(gchar **args){
 GError *error=NULL;int fd=g_file_open_tmp("doveos-capture-audio-XXXXXX.log",&audio_log,&error);
 if(fd>=0)close(fd);
 unsigned count=0;while(args[count])count++;args[count++]=audio_log?"--log":NULL;args[count++]=audio_log;args[count]=NULL;
 gboolean ok=fd>=0&&g_spawn_async(NULL,args,NULL,G_SPAWN_DO_NOT_REAP_CHILD,NULL,NULL,&audio_pid,&error);
 if(ok){audio_watch=g_child_watch_add(audio_pid,audio_done,NULL);gtk_label_set_text(GTK_LABEL(audio_status),"Connecting capture audio to Lap Dock speakers...");}
 else{audio_pid=0;gtk_label_set_text(GTK_LABEL(audio_status),"Audio unavailable: could not start the audio helper. Video can continue.");if(error)g_error_free(error);}
 return ok;
}
static gboolean audio_stop_check(gpointer unused){
 (void)unused;GPid pid=audio_pid;gchar *log=g_strdup(audio_log);char last[320]="";audio_last_line(last,sizeof last);
 stop_audio();gboolean gone=pid>0&&kill(pid,0)<0&&errno==ESRCH;
 audio_test_result=gone&&log&&!g_file_test(log,G_FILE_TEST_EXISTS)&&strstr(last,"null input/output")?0:5;
 printf("CAPTURE_AUDIO_STOP_CHECK status=%d child_gone=%d log_removed=%d\n",audio_test_result,gone,log&&!g_file_test(log,G_FILE_TEST_EXISTS));
 g_free(log);gtk_main_quit();return FALSE;
}
static void start_audio(uvc_device_t *device){
 stop_audio();if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(audio_option)))return;
 uvc_device_descriptor_t *desc=NULL;
 if(uvc_get_device_descriptor(device,&desc)<0){gtk_label_set_text(GTK_LABEL(audio_status),"Audio unavailable: capture device identity could not be read.");return;}
 gchar *bus=g_strdup_printf("%u",uvc_get_bus_number(device)),*address=g_strdup_printf("%u",uvc_get_device_address(device));
 gchar *vendor=g_strdup_printf("%u",desc->idVendor),*product=g_strdup_printf("%u",desc->idProduct);
 uvc_free_device_descriptor(desc);
 gchar *args[9]={"/tmp/doveos-capture-audio",bus,address,vendor,product,"1800",NULL,NULL,NULL};spawn_audio(args);
 g_free(bus);g_free(address);g_free(vendor);g_free(product);
}
static GMutex frame_mutex;
static unsigned char *pending;
static unsigned pending_w,pending_h;
static unsigned received,shown,dropped,decode_errors;
static gboolean streaming;
static volatile sig_atomic_t ended;
static void stop_signal(int number){(void)number;ended=1;}
static void stop_capture(GtkWidget *widget,gpointer unused);
static int claim_power(void){
 const char *path="/tmp/doveos-usb-video-active";
 int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);
 if(fd<0&&errno==EEXIST){FILE *file=fopen(path,"r");long pid=0;if(file){if(fscanf(file,"%ld",&pid)!=1)pid=0;fclose(file);}
  if(pid>1&&pid<4194304&&kill((pid_t)pid,0)<0&&errno==ESRCH){unlink(path);fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);}
 }
 if(fd<0)return -1;
 char text[40];int count=snprintf(text,sizeof text,"%ld\n",(long)getpid());
 int result=write(fd,text,count)==count?0:-1;close(fd);if(result)unlink(path);return result;
}
static void release_power(void){FILE *file=fopen("/tmp/doveos-usb-video-active","r");long pid=0;if(file){if(fscanf(file,"%ld",&pid)!=1)pid=0;fclose(file);}if(pid==(long)getpid())unlink("/tmp/doveos-usb-video-active");}
static long ordinary_memory_kb(void){
 FILE *file=fopen("/proc/meminfo","r");char line[160];long free_kb=0,buffers=0,cached=0,shared=0,cma=0;
 if(!file)return 0;
 while(fgets(line,sizeof line,file)){long value;if(sscanf(line,"MemFree: %ld",&value)==1)free_kb=value;else if(sscanf(line,"Buffers: %ld",&value)==1)buffers=value;else if(sscanf(line,"Cached: %ld",&value)==1)cached=value;else if(sscanf(line,"Shmem: %ld",&value)==1)shared=value;}
 fclose(file);file=fopen("/proc/vmstat","r");if(!file)return 0;
 while(fgets(line,sizeof line,file)){long value;if(sscanf(line,"nr_free_cma %ld",&value)==1)cma=value*4;}fclose(file);
 return free_kb+buffers+(cached>shared?(cached-shared)/2:0)-cma;
}
static void message(const char *text){gtk_label_set_text(GTK_LABEL(status),text);}
static void error_message(const char *phase,uvc_error_t error){
 char text[240];snprintf(text,sizeof text,"%s: %s (%d). Check the right USB hub, permissions and supported MJPEG modes.",phase,uvc_strerror(error),error);message(text);
}
static void frame_ready(uvc_frame_t *frame,void *unused){
 (void)unused;
 g_mutex_lock(&frame_mutex);
 received++;
 if(pending){dropped++;g_mutex_unlock(&frame_mutex);return;}
 g_mutex_unlock(&frame_mutex);
 if(frame->width>1280||frame->height>720||!frame->width||!frame->height||frame->data_bytes>4*1024*1024||uvc_any2rgb(frame,rgb_frame)<0){
  g_mutex_lock(&frame_mutex);decode_errors++;g_mutex_unlock(&frame_mutex);return;
 }
 size_t size=(size_t)rgb_frame->width*rgb_frame->height*3;
 if(!rgb_frame->width||!rgb_frame->height||rgb_frame->width>frame->width||rgb_frame->height>frame->height||rgb_frame->data_bytes<size)return;
 unsigned char *copy=g_try_malloc(size);if(!copy)return;
 memcpy(copy,rgb_frame->data,size);
 g_mutex_lock(&frame_mutex);pending=copy;pending_w=rgb_frame->width;pending_h=rgb_frame->height;g_mutex_unlock(&frame_mutex);
}
static void free_pixels(guchar *pixels,gpointer unused){(void)unused;g_free(pixels);}
static gboolean show_frame(gpointer unused){
 (void)unused;
 if(ended){stop_capture(NULL,NULL);gtk_main_quit();return FALSE;}
 static unsigned audio_ticks;if(audio_pid&&++audio_ticks%15==0){char last[320]="Connecting capture audio...";audio_last_line(last,sizeof last);gtk_label_set_text(GTK_LABEL(audio_status),last);}
 g_mutex_lock(&frame_mutex);unsigned char *pixels=pending;unsigned w=pending_w,h=pending_h,r=received,d=dropped,e=decode_errors;pending=NULL;g_mutex_unlock(&frame_mutex);
 if(!pixels)return TRUE;
 GdkPixbuf *image=gdk_pixbuf_new_from_data(pixels,GDK_COLORSPACE_RGB,FALSE,8,w,h,w*3,free_pixels,NULL);
 if(w>640||h>240){double scale=MIN(640.0/w,240.0/h);GdkPixbuf *scaled=gdk_pixbuf_scale_simple(image,w*scale,h*scale,GDK_INTERP_BILINEAR);g_object_unref(image);image=scaled;}
 gtk_image_set_from_pixbuf(GTK_IMAGE(picture),image);g_object_unref(image);shown++;
 char text[180];snprintf(text,sizeof text,"MJPEG %ux%u - received %u, shown %u, skipped %u, decode errors %u.",w,h,r,shown,d,e);message(text);return TRUE;
}
static void stop_capture(GtkWidget *widget,gpointer unused){
 (void)widget;(void)unused;
 stop_audio();
 if(handle){if(streaming)uvc_stop_streaming(handle);streaming=FALSE;uvc_close(handle);handle=NULL;}
 release_power();
 if(rgb_frame){uvc_free_frame(rgb_frame);rgb_frame=NULL;}
 g_mutex_lock(&frame_mutex);g_free(pending);pending=NULL;g_mutex_unlock(&frame_mutex);
 if(start){gtk_widget_set_sensitive(start,TRUE);gtk_widget_set_sensitive(refresh,TRUE);gtk_widget_set_sensitive(mode,TRUE);gtk_widget_set_sensitive(audio_option,TRUE);message("Capture stopped. The rest of DoveOS stays running.");}
}
static void rescan(GtkWidget *widget,gpointer unused){
 (void)widget;(void)unused;if(streaming)return;
 gtk_list_store_clear(rows);if(devices){uvc_free_device_list(devices,1);devices=NULL;}
 uvc_error_t error=uvc_get_device_list(context,&devices);if(error<0){error_message("Scan failed",error);return;}
 int count=0;
 for(;devices[count];count++){
  uvc_device_descriptor_t *desc=NULL;GtkTreeIter iter;char text[240];
  error=uvc_get_device_descriptor(devices[count],&desc);
  if(error<0)snprintf(text,sizeof text,"Video device %d — descriptor error %s",count+1,uvc_strerror(error));
  else{snprintf(text,sizeof text,"%s %s  [%04x:%04x, bus %u address %u]",desc->manufacturer?desc->manufacturer:"USB",desc->product?desc->product:"Video",desc->idVendor,desc->idProduct,uvc_get_bus_number(devices[count]),uvc_get_device_address(devices[count]));uvc_free_device_descriptor(desc);}
  gtk_list_store_append(rows,&iter);gtk_list_store_set(rows,&iter,0,text,1,count,-1);
 }
 message(count?"Select a USB video/capture device, then Start video.":"No UVC device found. Connect the capture card to the RIGHT USB hub, then Refresh. The Lap Dock's micro-HDMI connector remains an output.");
}
static void start_capture(GtkWidget *widget,gpointer unused){
 (void)widget;(void)unused;if(streaming)return;
 if(ordinary_memory_kb()<49152){message("Close other apps first. Capture needs 48 MiB of estimated ordinary-memory headroom.");return;}
 GtkTreeIter iter;GtkTreeModel *model;int index;
 if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),&model,&iter)){message("Select a capture device first.");return;}
 gtk_tree_model_get(model,&iter,1,&index,-1);
 uvc_error_t error=uvc_open(devices[index],&handle);if(error<0){error_message("Open failed",error);return;}
 int choice=gtk_combo_box_get_active(GTK_COMBO_BOX(mode));int w=choice?1280:640,h=choice?720:480,fps=choice==2?15:30;
 uvc_stream_ctrl_t control;
 error=uvc_get_stream_ctrl_format_size(handle,&control,UVC_FRAME_FORMAT_MJPEG,w,h,fps);
 if(error<0){uvc_close(handle);handle=NULL;error_message("MJPEG mode unavailable",error);return;}
 if(!control.dwMaxVideoFrameSize||control.dwMaxVideoFrameSize>4*1024*1024||control.dwMaxPayloadTransferSize>128*1024){uvc_close(handle);handle=NULL;message("Device asks for oversized buffers. Capture was not started.");return;}
 rgb_frame=uvc_allocate_frame((size_t)w*h*3);
 if(!rgb_frame){stop_capture(NULL,NULL);message("Not enough memory for a video frame.");return;}
 received=shown=dropped=decode_errors=0;
 error=uvc_start_streaming(handle,&control,frame_ready,NULL,0);
 if(error<0){stop_capture(NULL,NULL);error_message("Stream failed",error);return;}
 streaming=TRUE;gtk_widget_set_sensitive(start,FALSE);gtk_widget_set_sensitive(refresh,FALSE);gtk_widget_set_sensitive(mode,FALSE);message("Waiting for video. Connect a powered source to the capture card's HDMI IN.");
 if(claim_power()){stop_capture(NULL,NULL);message("Could not reserve the media power setting. Capture stopped.");}
 else{gtk_widget_set_sensitive(audio_option,FALSE);start_audio(devices[index]);}
}
static gboolean close_window(GtkWidget *widget,GdkEvent *event,gpointer unused){(void)widget;(void)event;(void)unused;stop_capture(NULL,NULL);gtk_main_quit();return FALSE;}
static int decode_check(int argc,char **argv){
 if(argc!=6)return 2;
 int w=atoi(argv[3]),h=atoi(argv[4]),count=atoi(argv[5]);if(w<1||h<1||w>1280||h>720||count<1||count>500)return 2;
 FILE *file=fopen(argv[2],"rb");if(!file)return 3;fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);if(size<=0||size>4*1024*1024){fclose(file);return 3;}
 uvc_frame_t *in=uvc_allocate_frame(size),*out=uvc_allocate_frame((size_t)w*h*3);if(!in||!out)return 4;
 if(fread(in->data,size,1,file)!=1)return 3;
 fclose(file);in->data_bytes=size;in->width=w;in->height=h;in->frame_format=UVC_FRAME_FORMAT_MJPEG;
 struct timespec before,after;clock_gettime(CLOCK_MONOTONIC,&before);
 for(int i=0;i<count;i++)if(uvc_any2rgb(in,out)<0)return 5;
 clock_gettime(CLOCK_MONOTONIC,&after);double ms=(after.tv_sec-before.tv_sec)*1000+(after.tv_nsec-before.tv_nsec)*1e-6;
 uint32_t hash=2166136261u;unsigned char *data=out->data;for(size_t i=0;i<out->data_bytes;i++)hash=(hash^data[i])*16777619u;
 printf("MJPEG_DECODE_CHECK frames=%d size=%ux%u mean_ms=%.3f hash=%08x\n",count,out->width,out->height,ms/count,hash);uvc_free_frame(in);uvc_free_frame(out);return 0;
}
int main(int argc,char **argv){
 if(argc>1&&!strcmp(argv[1],"--decode-check"))return decode_check(argc,argv);
 uvc_error_t error=uvc_init(&context,NULL);if(error<0){fprintf(stderr,"UVC_INIT_ERROR %s (%d)\n",uvc_strerror(error),error);return 3;}
 if(argc>1&&!strcmp(argv[1],"--scan")){error=uvc_get_device_list(context,&devices);if(error<0)return 4;int count=0;for(;devices[count];count++)printf("UVC_DEVICE bus=%u address=%u\n",uvc_get_bus_number(devices[count]),uvc_get_device_address(devices[count]));printf("UVC_SCAN_COUNT=%d\n",count);uvc_free_device_list(devices,1);uvc_exit(context);return 0;}
 gtk_init(&argc,&argv);g_mutex_init(&frame_mutex);
 signal(SIGTERM,stop_signal);signal(SIGINT,stop_signal);
 GtkWidget *window=dove_form("HDMI In",680,540),*box=gtk_vbox_new(FALSE,8);gtk_container_set_border_width(GTK_CONTAINER(box),12);gtk_container_add(GTK_CONTAINER(window),box);
 gtk_window_set_wmclass(GTK_WINDOW(window),"doveos-capture","DoveCapture");
 ui_header(box,"HDMI In","View a source using a USB HDMI capture card.","/etc/doveos/classic-icons/32/video-display.png");
 GtkWidget *instructions=ui_text("Connect the capture card to USB and the source to its HDMI input.");ui_pack(box,instructions);
 rows=gtk_list_store_new(2,G_TYPE_STRING,G_TYPE_INT);view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(rows));g_object_unref(rows);
 gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("USB video devices",gtk_cell_renderer_text_new(),"text",0,NULL));
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);gtk_container_add(GTK_CONTAINER(scroll),view);gtk_widget_set_size_request(scroll,-1,90);ui_pack(box,scroll);
 GtkWidget *row=gtk_hbox_new(FALSE,6);mode=gtk_combo_box_new_text();gtk_combo_box_append_text(GTK_COMBO_BOX(mode),"640x480 / 30 fps");gtk_combo_box_append_text(GTK_COMBO_BOX(mode),"1280x720 / 30 fps");gtk_combo_box_append_text(GTK_COMBO_BOX(mode),"1280x720 / 15 fps");gtk_combo_box_set_active(GTK_COMBO_BOX(mode),0);
 refresh=dove_button("Refresh");start=dove_button("Start video");GtkWidget *stop=dove_button("Stop");dove_add(row,mode);dove_add(row,refresh);dove_add(row,start);dove_add(row,stop);ui_pack(box,row);
 audio_option=dove_checkbox("Play capture-card sound through the dock",FALSE);ui_pack(box,audio_option);
 audio_status=dove_label("Audio off.");gtk_misc_set_alignment(GTK_MISC(audio_status),0,0);gtk_label_set_line_wrap(GTK_LABEL(audio_status),TRUE);ui_pack(box,audio_status);
 status=ui_status(box,"Select a capture card, then click Start video.");picture=gtk_image_new();gtk_box_pack_start(GTK_BOX(box),picture,TRUE,TRUE,0);
 g_signal_connect(refresh,"clicked",G_CALLBACK(rescan),NULL);g_signal_connect(start,"clicked",G_CALLBACK(start_capture),NULL);g_signal_connect(stop,"clicked",G_CALLBACK(stop_capture),NULL);g_signal_connect(window,"delete-event",G_CALLBACK(close_window),NULL);
 dove_timer(33,show_frame,NULL);rescan(NULL,NULL);gtk_widget_show_all(window);
 if(argc>1&&!strcmp(argv[1],"--audio-check")){audio_test=TRUE;gchar *args[5]={"/tmp/doveos-capture-audio","--self-test",NULL,NULL,NULL};if(!spawn_audio(args))return 5;}
 if(argc>1&&!strcmp(argv[1],"--audio-check-stop")){audio_test=TRUE;gchar *args[5]={"/tmp/doveos-capture-audio","--self-test-hold",NULL,NULL,NULL};if(!spawn_audio(args))return 5;g_timeout_add(500,audio_stop_check,NULL);}
 gtk_main();stop_audio();if(devices)uvc_free_device_list(devices,1);uvc_exit(context);g_mutex_clear(&frame_mutex);return audio_test?audio_test_result:0;
}
