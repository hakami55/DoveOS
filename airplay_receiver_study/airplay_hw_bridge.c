/* Bounded AirPlay RTP-to-Realtek decoder trial; no firmware writes. */
#define _GNU_SOURCE
#define AIRPLAY_STANDALONE 1
#define DOVE_AIRPLAY_PADDED_STRIDE 1
#define DOVE_AIRPLAY_ASPECT_FIT 1
#include <Limelight.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <poll.h>
#include <limits.h>

static volatile sig_atomic_t ended;
static volatile sig_atomic_t bridge_failed;
static int airplay_visible_width,airplay_visible_height;
static void finish_signal(int signal_number) {(void)signal_number;ended=1;}
static void network_diagnostics(const char *phase) {(void)phase;}
uint64_t LiGetMicroseconds(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u;
}
#include "../moonlight_dock_prototype/hw_video.h"
#include "airplay_sps.h"

static unsigned input_frames;
static pid_t decoder_pid;
static int decoder_fd=-1, decoder_width,decoder_height;
static char bridge_executable[PATH_MAX];
static unsigned decoder_generations;
/* The parent owns the display lease and GStreamer. Only exec'd workers load
   OMX/VOUT, so each geometry starts with a fresh vendor process context. */
static int socket_io(int fd,void *buffer,size_t bytes,int writing) {
    unsigned char *p=buffer;
    uint64_t deadline=LiGetMicroseconds()+3000000u;
    while(bytes&&!ended){
        struct pollfd q={fd,writing?POLLOUT:POLLIN,0};
        int ready=poll(&q,1,100);
        if(ready<0&&errno==EINTR)continue;
        if(ready<0 || LiGetMicroseconds()>deadline)return -1;
        if(!ready)continue;
        ssize_t n=writing?send(fd,p,bytes,MSG_NOSIGNAL|MSG_DONTWAIT):
                          recv(fd,p,bytes,MSG_DONTWAIT);
        if(n<0&&(errno==EINTR||errno==EAGAIN))continue;
        if(n<=0)return -1;
        p+=n;bytes-=(size_t)n;
    }
    return bytes?-1:0;
}
static int stop_decoder(void) {
    if(decoder_fd>=0){close(decoder_fd);decoder_fd=-1;}
    if(!decoder_pid)return 0;
    int status=0;
    for(int i=0;i<70;i++){
        pid_t result=waitpid(decoder_pid,&status,WNOHANG);
        if(result==decoder_pid){
            printf("AIRPLAY_DECODER_REAPED pid=%ld status=%d\n",(long)decoder_pid,status);
            decoder_pid=0;
            return WIFEXITED(status)&&WEXITSTATUS(status)==0?0:-1;
        }
        if(result<0&&errno!=EINTR){decoder_pid=0;return -1;}
        if(i==40)kill(decoder_pid,SIGTERM);
        usleep(100000);
    }
    kill(decoder_pid,SIGKILL);waitpid(decoder_pid,&status,0);decoder_pid=0;
    puts("AIRPLAY_DECODER_CLEANUP_TIMEOUT");return -1;
}
static int start_decoder(int width,int height) {
    int pair[2];if(socketpair(AF_UNIX,SOCK_STREAM,0,pair))return -1;
    fcntl(pair[0],F_SETFD,FD_CLOEXEC);
    char w[16],h[16];snprintf(w,sizeof w,"%d",width);snprintf(h,sizeof h,"%d",height);
    const char *loader="/tmp/doveos-airplay-runtime/lib/ld-linux-armhf.so.3";
    char *args[]={(char*)loader,"--library-path",
        "/tmp/doveos-airplay-runtime/lib:/usr/lib:/lib",bridge_executable,
        "--decode",w,h,NULL};
    pid_t parent=getpid();pid_t pid=fork();
    if(!pid){
        /* No library work after fork: set descriptors and immediately exec. */
        if(pair[1]!=3&&dup2(pair[1],3)<0)_exit(126);
        if(pair[0]!=3)close(pair[0]);
        if(pair[1]!=3)close(pair[1]);
        fcntl(3,F_SETFD,0);
        prctl(PR_SET_PDEATHSIG,SIGTERM);
        if(getppid()!=parent)_exit(125);
        execv(loader,args);_exit(127);
    }
    close(pair[1]);
    if(pid<0){close(pair[0]);return -1;}
    decoder_pid=pid;decoder_fd=pair[0];decoder_width=width;decoder_height=height;
    decoder_generations++;
    printf("AIRPLAY_DECODER_STARTED pid=%ld generation=%u visible=%dx%d\n",
        (long)pid,decoder_generations,width,height);
    return 0;
}
static int decoder_main(int width,int height) {
    airplay_visible_width=width;airplay_visible_height=height;
    if(width<320||height<240||width>1280||height>720||(width&1)||(height&1))return 2;
    if(video_open((width+15)&~15,(height+15)&~15)){video_close();return 3;}
    uint8_t *frame=malloc(2*1024*1024);int failed=!frame;
    while(!ended&&!failed){
        /* Wait indefinitely between frames; payload reads themselves are bounded. */
        struct pollfd q={3,POLLIN,0};int ready=poll(&q,1,100);
        if(ready<0&&errno==EINTR)continue;
        if(ready<0){failed=1;break;}if(!ready)continue;
        uint32_t size=0;
        ssize_t n=recv(3,&size,sizeof size,MSG_PEEK);
        if(!n)break;
        if(socket_io(3,&size,sizeof size,0)||!size||size>2*1024*1024 ||
           socket_io(3,frame,size,0)){failed=1;break;}
        if(!configured&&port_change&&resize_output()){failed=1;break;}
        uint8_t *coded=NULL;size_t coded_size=0;
        failed=airplay_normalize_sps(frame,size,&coded,&coded_size);
        if(!failed)failed=hw_submit(coded,coded_size);
        free(coded);
        if(hw_failed)failed=1;
        if(!failed)input_frames++;
    }
    failed=failed||hw_failed;
    free(frame);close(3);video_close();
    printf("AIRPLAY_SLICE_COUNTS P=%u B=%u I=%u SP=%u SI=%u\n",
        airplay_slice_counts[0],airplay_slice_counts[1],airplay_slice_counts[2],
        airplay_slice_counts[3],airplay_slice_counts[4]);
    printf("AIRPLAY_DECODER_DONE visible=%dx%d input=%u output=%u failed=%d\n",
        width,height,input_frames,displayed,failed);
    return failed?4:0;
}
static int claimed;
static uint64_t last_packet_us;
static int claim_display(void) {
    if(!getenv("DOVE_AIRPLAY_CLAIM"))return 0;
    int fd=open("/tmp/doveos-direct-display",O_WRONLY|O_CREAT|O_EXCL,0600);
    if(fd<0)return -1;
    char text[40];int size=snprintf(text,sizeof text,"%ld\n",(long)getpid());
    if(write(fd,text,(size_t)size)!=size){close(fd);unlink("/tmp/doveos-direct-display");return -1;}
    close(fd);claimed=1;
    for(int i=0;i<100&&!ended;i++){
        FILE *file=fopen("/tmp/doveos-direct-display.ready","r");long owner=0;
        if(file){if(fscanf(file,"%ld",&owner)!=1)owner=0;fclose(file);}
        if(owner==(long)getpid()){puts("AIRPLAY_DISPLAY_ACQUIRED");return 0;}
        usleep(100000);
    }
    return -1;
}
static void release_display(void) {
    if(!claimed)return;
    FILE *file=fopen("/tmp/doveos-direct-display","r");long owner=0;
    if(file){if(fscanf(file,"%ld",&owner)!=1)owner=0;fclose(file);}
    if(owner==(long)getpid())unlink("/tmp/doveos-direct-display");
    claimed=0;puts("AIRPLAY_DISPLAY_RELEASED");
}
static GstFlowReturn sample_ready(GstAppSink *sink, gpointer unused) {
    (void)unused;
    GstSample *sample=gst_app_sink_pull_sample(sink);
    if(!sample)return GST_FLOW_EOS;
    GstBuffer *buffer=gst_sample_get_buffer(sample);
    static uint64_t previous_arrival;
    uint64_t arrival=LiGetMicroseconds();
    if(previous_arrival && arrival-previous_arrival>100000u)
        printf("AIRPLAY_INPUT_GAP uptime_us=%llu gap_ms=%.3f frames=%u\n",
            (unsigned long long)arrival,(arrival-previous_arrival)/1000.0,input_frames);
    previous_arrival=arrival;
    GstMapInfo map;
    if(!gst_buffer_map(buffer,&map,GST_MAP_READ)){
        gst_sample_unref(sample);bridge_failed=1;ended=1;return GST_FLOW_ERROR;
    }
    GstCaps *caps=gst_sample_get_caps(sample);
    GstStructure *structure=caps?gst_caps_get_structure(caps,0):NULL;
    int width=0,height=0;
    if(!structure||!gst_structure_get_int(structure,"width",&width)||
       !gst_structure_get_int(structure,"height",&height)||
       width<320||height<240||width>1280||height>720||(width&1)||(height&1)){
        printf("AIRPLAY_HW_DIMENSIONS_UNSUPPORTED=%dx%d\n",width,height);bridge_failed=1;ended=1;
    }
    if(!ended&&(!decoder_pid||width!=decoder_width||height!=decoder_height)){
        if(!claimed&&claim_display()){puts("AIRPLAY_DISPLAY_ACQUIRE_FAILED");bridge_failed=1;ended=1;}
        if(!ended&&decoder_pid){
            printf("AIRPLAY_ROTATION %dx%d -> %dx%d\n",decoder_width,decoder_height,width,height);
            if(stop_decoder()){bridge_failed=1;ended=1;}
        }
        if(!ended&&start_decoder(width,height)){puts("AIRPLAY_DECODER_START_FAILED");bridge_failed=1;ended=1;}
    }
    if(!ended&&map.size>0&&map.size<=2*1024*1024){
        uint32_t size=(uint32_t)map.size;
        if(socket_io(decoder_fd,&size,sizeof size,1)||
           socket_io(decoder_fd,map.data,map.size,1)){
            puts("AIRPLAY_DECODER_SEND_FAILED");bridge_failed=1;ended=1;
        }else{
            input_frames++;
            __atomic_store_n(&last_packet_us,LiGetMicroseconds(),__ATOMIC_RELEASE);
            if(input_frames==1||input_frames%60==0)printf("AIRPLAY_HW_INPUT_FRAMES=%u\n",input_frames);
        }
    }
    gst_buffer_unmap(buffer,&map);
    gst_sample_unref(sample);
    return ended?GST_FLOW_EOS:GST_FLOW_OK;
}

int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    sigset_t mask;sigemptyset(&mask);sigprocmask(SIG_SETMASK,&mask,NULL);
    unsigned duration=argc>1?(unsigned)atoi(argv[1]):20;
    if(duration<1||duration>1800)duration=20;
    signal(SIGTERM,finish_signal);signal(SIGINT,finish_signal);
    signal(SIGPIPE,SIG_IGN);
    if(argc==4&&!strcmp(argv[1],"--decode"))return decoder_main(atoi(argv[2]),atoi(argv[3]));
    if(!realpath(argv[0],bridge_executable)){perror("AIRPLAY_EXEC_PATH");return 2;}
    gst_init(&argc,&argv);
    GError *error=NULL;
    const char *pipeline_text=
      "udpsrc address=127.0.0.1 port=17004 caps=\"application/x-rtp,media=(string)video,"
      "clock-rate=(int)90000,encoding-name=(string)H264,payload=(int)96\" "
      "! rtph264depay ! h264parse config-interval=-1 "
      "! video/x-h264,stream-format=(string)byte-stream,alignment=(string)au "
      "! appsink name=out emit-signals=true sync=false max-buffers=2 drop=true";
    GstElement *pipeline=gst_parse_launch(pipeline_text,&error);
    if(!pipeline||error){
        printf("AIRPLAY_RTP_PIPELINE_ERROR=%s\n",error?error->message:"unknown");
        if(error)g_error_free(error);
        if(pipeline)gst_object_unref(pipeline);
        return 2;
    }
    if(argc>1&&!strcmp(argv[1],"--check")){
        gst_object_unref(pipeline);puts("AIRPLAY_RTP_PIPELINE_CHECK_OK");return 0;
    }
    GstElement *sink=gst_bin_get_by_name(GST_BIN(pipeline),"out");
    g_signal_connect(sink,"new-sample",G_CALLBACK(sample_ready),NULL);
    gst_object_unref(sink);
    if(gst_element_set_state(pipeline,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE){
        puts("AIRPLAY_RTP_START_FAILED");gst_object_unref(pipeline);return 3;
    }
    GstBus *bus=gst_element_get_bus(pipeline);
    puts("AIRPLAY_RTP_HW_READY");
    for(unsigned i=0;i<duration*20&&!ended;i++){
        GstMessage *message;
        while((message=gst_bus_pop_filtered(bus,GST_MESSAGE_ERROR|GST_MESSAGE_EOS))){
            if(GST_MESSAGE_TYPE(message)==GST_MESSAGE_ERROR){
                GError *failure=NULL;gchar *detail=NULL;
                gst_message_parse_error(message,&failure,&detail);
                printf("AIRPLAY_RTP_RUNTIME_ERROR=%s\n",failure?failure->message:"unknown");
                if(failure)g_error_free(failure);
                g_free(detail);bridge_failed=1;
            }else puts("AIRPLAY_RTP_END_OF_STREAM");
            ended=1;gst_message_unref(message);
        }
        uint64_t last=__atomic_load_n(&last_packet_us,__ATOMIC_ACQUIRE);
        if(last&&getenv("DOVE_AIRPLAY_CLAIM")&&LiGetMicroseconds()-last>15000000u){
            puts("AIRPLAY_VIDEO_IDLE_RETURN");ended=1;break;
        }
        usleep(50000);
    }
    gst_element_set_state(pipeline,GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(pipeline);
    int failed=stop_decoder()||bridge_failed;
    release_display();
    printf("AIRPLAY_RTP_HW_DONE input=%u generations=%u\n",input_frames,decoder_generations);
    return failed?4:0;
}
