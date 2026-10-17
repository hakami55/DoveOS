/* RAM-only Moonlight network probe; receives H.264 and Opus without display. */
#include "client.h"
#include "errors.h"
#include <Limelight.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef DOCK_INPUT
#include <errno.h>
#include <linux/input.h>
#include <poll.h>
#include <fcntl.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/mount.h>
#include <sys/time.h>
#include "src/src/input/keyboard.h"
#include "gamepad_input.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include "clickpad_stream.h"
static long click_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000L+t.tv_nsec/1000000L;}
static unsigned key_events, pointer_events;
static void click_send(int button,int pressed,void *data){(void)data;LiSendMouseButtonEvent(pressed?BUTTON_ACTION_PRESS:BUTTON_ACTION_RELEASE,button==2?BUTTON_RIGHT:BUTTON_LEFT);pointer_events++;}

static pthread_t input_thread;
static int input_started, input_stop;
/* Discovery can open USB devices: never perform it on the pointer thread. */
static int gamepad_scan_stop, gamepad_scan_needed=1, gamepad_pending=-1;
static void *gamepad_discovery_loop(void *unused) {
    (void)unused;
    while (!__atomic_load_n(&gamepad_scan_stop,__ATOMIC_ACQUIRE)) {
        if (__atomic_load_n(&gamepad_scan_needed,__ATOMIC_ACQUIRE) &&
            __atomic_load_n(&gamepad_pending,__ATOMIC_ACQUIRE)<0) {
            int fd=gamepad_find();
            if (fd>=0) {
                __atomic_store_n(&gamepad_scan_needed,0,__ATOMIC_RELEASE);
                __atomic_store_n(&gamepad_pending,fd,__ATOMIC_RELEASE);
            }
        }
        for (int i=0;i<20 && !__atomic_load_n(&gamepad_scan_stop,__ATOMIC_ACQUIRE);i++)
            usleep(100000);
    }
    return NULL;
}
static int hid_button_pipe[2] = {-1,-1};
static volatile int hid_button_active;
static void *hid_button_loop(void *unused) {
    (void)unused;
    if (access("/sys/kernel/debug/hid",R_OK))
        mount("debugfs","/sys/kernel/debug","debugfs",0,NULL);
    DIR *dir=opendir("/sys/bus/hid/devices");
    if (!dir) return NULL;
    char event_path[256]={0};
    struct dirent *entry;
    while ((entry=readdir(dir))) {
        if (strncmp(entry->d_name,"0003:03F0:0B51.",15) || strlen(entry->d_name)!=19) continue;
        char desc_path[256];
        snprintf(desc_path,sizeof desc_path,"/sys/bus/hid/devices/%.19s/report_descriptor",entry->d_name);
        FILE *desc=fopen(desc_path,"rb");
        if (!desc) continue;
        unsigned char head[4]={0};
        size_t n=fread(head,1,sizeof head,desc);
        fclose(desc);
        if (n==4 && head[0]==0x05 && head[1]==0x01 && head[2]==0x09 && head[3]==0x02) {
            snprintf(event_path,sizeof event_path,"/sys/kernel/debug/hid/%.19s/events",entry->d_name);
            break;
        }
    }
    closedir(dir);
    if (!event_path[0]) return NULL;
    FILE *events=fopen(event_path,"r");
    if (!events) return NULL;
    hid_button_active=1;
    puts("PHYSICAL_BUTTON_INPUT_READY");
    fflush(stdout);
    char line[256];
    while (!input_stop && fgets(line,sizeof line,events)) {
        unsigned report,buttons;
        if (sscanf(line,"report (size %*u) (numbered) = %x %x",&report,&buttons)==2 && report==1) {
            unsigned char state=(unsigned char)(buttons&3);
            if (write(hid_button_pipe[1],&state,1)!=1) break;
        }
    }
    hid_button_active=0;
    fclose(events);
    return NULL;
}
static void *input_loop(void *unused) {
    (void)unused;
    pthread_t hid_thread;
    int hid_started=0;
    pthread_t discovery_thread;
    int discovery_started=0;
    struct dock_gamepad gamepad={.fd=-1};
    int kb=open("/dev/input/event1",O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    int tp=open("/dev/input/event2",O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    if (kb<0 || tp<0) { printf("INPUT_OPEN_FAILED kb=%d tp=%d\n",kb,tp); goto done; }
    puts("INPUT_READY=KEYBOARD_TOUCHPAD");
    struct pollfd fds[5]={{.fd=kb,.events=POLLIN},{.fd=tp,.events=POLLIN},
                          {.fd=-1,.events=POLLIN},{.fd=-1,.events=POLLIN},{.fd=-1,.events=POLLIN}};
    if (!pthread_create(&discovery_thread,NULL,gamepad_discovery_loop,NULL)) {
        discovery_started=1;
        puts("INPUT_ASYNC_DISCOVERY=1");
    }
    if (!pipe(hid_button_pipe)) {
        fcntl(hid_button_pipe[0],F_SETFL,O_NONBLOCK);
        fcntl(hid_button_pipe[1],F_SETFL,O_NONBLOCK);
        fds[2].fd=hid_button_pipe[0];
        if (!pthread_create(&hid_thread,NULL,hid_button_loop,NULL)) hid_started=1;
    }
    int raw_socket=socket(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    struct sockaddr_in raw_address={.sin_family=AF_INET,.sin_port=htons(18829),.sin_addr={.s_addr=htonl(INADDR_LOOPBACK)}};
    if(raw_socket>=0&&bind(raw_socket,(struct sockaddr*)&raw_address,sizeof raw_address)){close(raw_socket);raw_socket=-1;}
    fds[4].fd=raw_socket;DockClickpad raw={0};unsigned raw_packets=0;
    puts(raw_socket>=0?"CLICKPAD_WIDE_LISTENING right_zone=x>=2302,y>=1265":"CLICKPAD_WIDE_UNAVAILABLE_NATIVE_FALLBACK");
    unsigned char last_buttons=0;
    unsigned char modifiers=0;
    int x=0,y=0,have_x=0,have_y=0,dx=0,dy=0;
    int touching=0,touch_travel=0,physical_click=0,saw_two_fingers=0,moving=0;
    int mt_slot=0,mt_active[2]={0,0},mt_y[2]={0,0},mt_have_y[2]={0,0};
    int two_fingers=0,scroll_have_center=0,scroll_center=0,scroll_moved=0;
    int pending_dx=0,pending_dy=0,scaled_x=0,scaled_y=0;
    int pointer_percent=50;
    const char *speed=getenv("DRMOSA_POINTER_PERCENT");
    if (speed) {
        int requested=atoi(speed);
        if (requested>=20 && requested<=100) pointer_percent=requested;
    }
    printf("POINTER_SPEED=%d TAP_DEADZONE=28\n",pointer_percent);
    int start_x=0,start_y=0,have_start_x=0,have_start_y=0;
    struct timespec touch_started={0};
    unsigned timing_samples=0;
    long max_input_lag_us=0;
    time_t next_timing_report=time(NULL)+10;
    time_t next_rumble_refresh=0;
    while (!input_stop) {
        if (time(NULL)>=next_rumble_refresh) {
            if (__atomic_load_n(&gamepad_rumble_available,__ATOMIC_ACQUIRE)) {
                unsigned value=__atomic_load_n(&gamepad_rumble_value,__ATOMIC_ACQUIRE);
                if (value) gamepad_rumble_send(value);
            }
            next_rumble_refresh=time(NULL)+1;
        }
        if (time(NULL)>=next_timing_report) {
            printf("INPUT_TIMING reports=%u MAX_QUEUE_MS=%.3f\n",
                   timing_samples,max_input_lag_us/1000.0);
            fflush(stdout);
            timing_samples=0;max_input_lag_us=0;next_timing_report=time(NULL)+10;
        }
        if (gamepad.fd<0) {
            gamepad.fd=__atomic_exchange_n(&gamepad_pending,-1,__ATOMIC_ACQ_REL);
            if (gamepad.fd>=0) {
                gamepad_layout(&gamepad);
                __atomic_store_n(&gamepad_rumble_value,0,__ATOMIC_RELEASE);
                __atomic_store_n(&gamepad_rumble_available,gamepad.bridge_rumble,__ATOMIC_RELEASE);
                __atomic_store_n(&gamepad_scan_needed,0,__ATOMIC_RELEASE);
                fds[3].fd=gamepad.fd;
                LiSendControllerArrivalEvent(0,1,LI_CTYPE_XBOX,
                    A_FLAG|B_FLAG|X_FLAG|Y_FLAG|UP_FLAG|DOWN_FLAG|LEFT_FLAG|
                    RIGHT_FLAG|LB_FLAG|RB_FLAG|PLAY_FLAG|BACK_FLAG|
                    LS_CLK_FLAG|RS_CLK_FLAG|SPECIAL_FLAG,LI_CCAP_ANALOG_TRIGGERS|
                    (gamepad.bridge_rumble?LI_CCAP_RUMBLE:0));
                gamepad_send(&gamepad,1);
            }
        }
        int polled=poll(fds,5,100);
        if(dock_clickpad_expire(&raw,click_ms(),click_send,NULL)){last_buttons=0;physical_click=1;puts("CLICKPAD_WIDE_EXPIRED_NATIVE_FALLBACK");}
        if(polled<=0)continue;
        if(fds[4].revents&POLLIN){unsigned char packet[12];ssize_t n;while((n=recv(raw_socket,packet,sizeof packet,MSG_TRUNC))>0){
            int was_live=raw.live;
            /* Release any native hold before handing ownership to the raw reader. */
            if(!was_live&&n==12&&packet[0]==0x43&&packet[1]==0x50&&packet[2]==1&&packet[3]<=1){if(last_buttons&1)click_send(1,0,NULL);if(last_buttons&2)click_send(2,0,NULL);last_buttons=0;}
            if(dock_clickpad_packet(&raw,packet,(unsigned)n,click_ms(),click_send,NULL)){raw_packets++;if(!was_live)puts("CLICKPAD_WIDE_ACTIVE");if(packet[3])physical_click=1;}
        }}
        for (int dev=0;dev<2;dev++) {
            if (!(fds[dev].revents&POLLIN)) continue;
            struct input_event ev;
            while (read(fds[dev].fd,&ev,sizeof ev)==sizeof ev) {
                if (dev==0 && ev.type==EV_KEY && ev.code<sizeof(keyCodes)/sizeof(keyCodes[0])) {
                    unsigned char bit=0;
                    switch(ev.code) {
                        case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT: bit=MODIFIER_SHIFT; break;
                        case KEY_LEFTCTRL: case KEY_RIGHTCTRL: bit=MODIFIER_CTRL; break;
                        case KEY_LEFTALT: case KEY_RIGHTALT: bit=MODIFIER_ALT; break;
                        case KEY_LEFTMETA: case KEY_RIGHTMETA: bit=MODIFIER_META; break;
                    }
                    if (bit) { if (ev.value) modifiers|=bit; else modifiers&=(unsigned char)~bit; }
                    if (keyCodes[ev.code] && ev.value!=2) {
                        LiSendKeyboardEvent((short)(0x8000|keyCodes[ev.code]),
                              ev.value?KEY_ACTION_DOWN:KEY_ACTION_UP,modifiers);
                        key_events++;
                    }
                } else if (dev==1) {
                    if (ev.type==EV_ABS && ev.code==ABS_MT_SLOT) {
                        mt_slot=ev.value;
                    } else if (ev.type==EV_ABS && ev.code==ABS_MT_TRACKING_ID &&
                               mt_slot>=0 && mt_slot<2) {
                        mt_active[mt_slot]=(ev.value>=0);
                        mt_have_y[mt_slot]=0;
                        scroll_have_center=0;
                    } else if (ev.type==EV_ABS && ev.code==ABS_MT_POSITION_Y &&
                               mt_slot>=0 && mt_slot<2) {
                        mt_y[mt_slot]=ev.value;
                        mt_have_y[mt_slot]=1;
                    }
                    if (ev.type==EV_SYN && ev.code==SYN_REPORT) {
                        struct timeval now;gettimeofday(&now,NULL);
                        long lag=(now.tv_sec-ev.time.tv_sec)*1000000L+now.tv_usec-ev.time.tv_usec;
                        if (lag>=0 && lag<10000000L) {
                            timing_samples++;
                            if (lag>max_input_lag_us) max_input_lag_us=lag;
                        }
                    }
                    if (ev.type==EV_ABS && ev.code==ABS_X && touching) {
                        if (have_x) { int d=ev.value-x; dx+=d; touch_travel+=abs(d); }
                        x=ev.value; have_x=1;
                        if (!have_start_x) { start_x=x; have_start_x=1; }
                    } else if (ev.type==EV_ABS && ev.code==ABS_Y && touching) {
                        if (have_y) { int d=ev.value-y; dy+=d; touch_travel+=abs(d); }
                        y=ev.value; have_y=1;
                        if (!have_start_y) { start_y=y; have_start_y=1; }
                    } else if (ev.type==EV_KEY && ev.code==BTN_TOOL_DOUBLETAP && ev.value) {
                        saw_two_fingers=1;
                        two_fingers=1;scroll_have_center=0;
                        touch_travel=0; dx=dy=0; pending_dx=pending_dy=0; have_x=have_y=0;
                    } else if (ev.type==EV_KEY && ev.code==BTN_TOOL_DOUBLETAP && !ev.value) {
                        two_fingers=0;scroll_have_center=0;
                    } else if (ev.type==EV_KEY && ev.code==BTN_TOUCH) {
                        if (ev.value) {
                            touching=1; have_x=have_y=0; dx=dy=0;
                            have_start_x=have_start_y=0;
                            pending_dx=pending_dy=scaled_x=scaled_y=0;
                            touch_travel=0; physical_click=0; saw_two_fingers=0; moving=0;
                            scroll_moved=0;scroll_have_center=0;
                            clock_gettime(CLOCK_MONOTONIC,&touch_started);
                        } else if (touching) {
                            struct timespec now;
                            clock_gettime(CLOCK_MONOTONIC,&now);
                            long ms=(now.tv_sec-touch_started.tv_sec)*1000L+
                                    (now.tv_nsec-touch_started.tv_nsec)/1000000L;
                            touching=0; have_x=have_y=0; dx=dy=0;
                            if (!(raw.live&&(raw.button||click_ms()-raw.press<150)) && !physical_click && !moving && !scroll_moved &&
                                touch_travel<80 && ms<700) {
                                int button=(saw_two_fingers ||
                                   (have_start_x && have_start_y &&
                                    start_x>1582 && start_y>1053)) ? BUTTON_RIGHT : BUTTON_LEFT;
                                LiSendMouseButtonEvent(BUTTON_ACTION_PRESS,button);
                                LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE,button);
                                pointer_events+=2;
                            }
                        }
                    } else if (!raw.live && !hid_button_active && ev.type==EV_KEY && (ev.code==BTN_LEFT || ev.code==BTN_RIGHT)) {
                        LiSendMouseButtonEvent(ev.value?BUTTON_ACTION_PRESS:BUTTON_ACTION_RELEASE,
                              ev.code==BTN_LEFT?BUTTON_LEFT:BUTTON_RIGHT);
                        if (ev.value) physical_click=1;
                        pointer_events++;
                    } else if (ev.type==EV_SYN && ev.code==SYN_REPORT && (dx||dy)) {
                        if (!saw_two_fingers) {
                            if (!moving) {
                                pending_dx+=dx; pending_dy+=dy;
                                int distance=abs(pending_dx)+abs(pending_dy);
                                if (have_start_x && have_start_y && distance>=28) {
                                    moving=1;
                                    pending_dx=pending_dx*(distance-28)/distance;
                                    pending_dy=pending_dy*(distance-28)/distance;
                                    dx=pending_dx; dy=pending_dy;
                                } else dx=dy=0;
                            }
                            if (moving) {
                                scaled_x+=dx*pointer_percent;
                                scaled_y+=dy*pointer_percent;
                                int move_x=scaled_x/100,move_y=scaled_y/100;
                                scaled_x-=move_x*100; scaled_y-=move_y*100;
                                if (move_x>32767) move_x=32767;
                                if (move_x< -32768) move_x=-32768;
                                if (move_y>32767) move_y=32767;
                                if (move_y< -32768) move_y=-32768;
                                if (move_x || move_y) {
                                    LiSendMouseMoveEvent((short)move_x,(short)move_y);
                                    pointer_events++;
                                }
                            }
                        }
                        dx=dy=0;
                    }
                    if (ev.type==EV_SYN && ev.code==SYN_REPORT && two_fingers &&
                        mt_active[0] && mt_active[1] && mt_have_y[0] && mt_have_y[1]) {
                        int center=(mt_y[0]+mt_y[1])/2;
                        if (scroll_have_center) {
                            int delta=center-scroll_center;
                            if (delta>100 || delta< -100) {
                                scroll_have_center=0;
                                scroll_center=center;
                            }
                            else if (delta>=4 || delta<= -4) {
                                int wheel=-2*delta;
                                if (wheel>32767) wheel=32767;
                                if (wheel< -32768) wheel=-32768;
                                LiSendHighResScrollEvent((short)wheel);
                                pointer_events++;
                                scroll_moved=1;
                                scroll_center=center;
                            }
                        }
                        if (!scroll_have_center) scroll_center=center;
                        scroll_have_center=1;
                    }
                }
            }
        }
        if (fds[2].revents&POLLIN) {
            unsigned char buttons;
            while (read(fds[2].fd,&buttons,1)==1) {
                if(raw.live)continue;
                unsigned char changed=(unsigned char)(last_buttons^buttons);
                if (changed&1) {
                    LiSendMouseButtonEvent((buttons&1)?BUTTON_ACTION_PRESS:BUTTON_ACTION_RELEASE,BUTTON_LEFT);
                    if (buttons&1) physical_click=1;
                    pointer_events++;
                }
                if (changed&2) {
                    LiSendMouseButtonEvent((buttons&2)?BUTTON_ACTION_PRESS:BUTTON_ACTION_RELEASE,BUTTON_RIGHT);
                    if (buttons&2) physical_click=1;
                    pointer_events++;
                }
                last_buttons=buttons;
            }
        }
        if (gamepad.fd>=0 && (fds[3].revents&(POLLHUP|POLLERR|POLLNVAL))) {
            gamepad_rumble_stop();
            gamepad_send(&gamepad,0);
            printf("GAMEPAD_DISCONNECTED events=%u\n",gamepad.events);
            close(gamepad.fd);
            memset(&gamepad,0,sizeof gamepad);
            gamepad.fd=-1;
            fds[3].fd=-1;
            __atomic_store_n(&gamepad_scan_needed,1,__ATOMIC_RELEASE);
        } else if (gamepad.fd>=0 && (fds[3].revents&POLLIN)) {
            struct input_event ev;
            int changed=0;
            ssize_t n;
            while ((n=read(gamepad.fd,&ev,sizeof ev))==sizeof ev) {
                changed|=gamepad_process(&gamepad,&ev);
                if (changed && ev.type==EV_SYN && ev.code==SYN_REPORT) {
                    gamepad_send(&gamepad,1);
                    gamepad.events++;
                    changed=0;
                }
            }
            if (changed) { gamepad_send(&gamepad,1); gamepad.events++; }
            if (n==0 || (n<0 && errno!=EAGAIN)) {
                gamepad_rumble_stop();
                gamepad_send(&gamepad,0);
                close(gamepad.fd);
                memset(&gamepad,0,sizeof gamepad);
                gamepad.fd=-1;
                fds[3].fd=-1;
                __atomic_store_n(&gamepad_scan_needed,1,__ATOMIC_RELEASE);
            }
        }
    }
    if(raw.button)click_send(raw.button,0,NULL);
    if(raw_socket>=0)close(raw_socket);
    printf("CLICKPAD_WIDE_PACKETS=%u\n",raw_packets);
done:
    gamepad_rumble_stop();
    __atomic_store_n(&gamepad_scan_stop,1,__ATOMIC_RELEASE);
    if (discovery_started) pthread_join(discovery_thread,NULL);
    int pending=__atomic_exchange_n(&gamepad_pending,-1,__ATOMIC_ACQ_REL);
    if (pending>=0) close(pending);
    if (gamepad.fd>=0) { gamepad_send(&gamepad,0); close(gamepad.fd); }
    if (hid_started) { pthread_cancel(hid_thread); pthread_join(hid_thread,NULL); }
    if (hid_button_pipe[0]>=0) close(hid_button_pipe[0]);
    if (hid_button_pipe[1]>=0) close(hid_button_pipe[1]);
    if(kb>=0)close(kb);
    if(tp>=0)close(tp);
    return NULL;
}
#endif
#ifdef PLAY_AUDIO
#include <opus_multistream.h>
#include <stdint.h>
#include <stdlib.h>
static OpusMSDecoder *opus_decoder;
static FILE *audio_out;
static int16_t *pcm;
static int audio_channels, audio_frame_samples;
static unsigned audio_played, audio_errors;
#endif
static volatile sig_atomic_t ended;
#ifdef VIEW
#define HW_FORCE_UDP_BUFFER 1
#include "hw_network.h"
#include "hw_video.h"
#endif

static volatile sig_atomic_t ended;
static unsigned frames, video_bytes, audio_packets;
#ifdef VIEW
#include "stream_info.h"
#endif
static int setup(int format, int width, int height, int rate, void *context, int flags) {
    (void)context; (void)flags;
    printf("VIDEO_SETUP format=%d %dx%d %dHz\n", format, width, height, rate);
#ifdef VIEW
    if (width!=hw_width || height!=hw_height) { puts("VIDEO_SIZE_MISMATCH"); return -1; } if(hw_hevc && !(format & VIDEO_FORMAT_MASK_H265)){puts("HEVC_HOST_FALLBACK_REJECTED=1");return -1;}if(hw_hevc)__atomic_store_n(&hevc.rate,rate==30?30:60,__ATOMIC_RELEASE);
#endif
    return 0;
}
static int submit(PDECODE_UNIT unit) {
    unsigned size = 0;
    for (PLENTRY part = unit->bufferList; part; part = part->next) size += part->length;
    __atomic_add_fetch(&video_bytes,size,__ATOMIC_RELAXED);
    unsigned received=__atomic_add_fetch(&frames,1,__ATOMIC_RELAXED);
    if (received == 1 || received % 60 == 0)
        printf("VIDEO_FRAMES=%u BYTES=%u\n", received, __atomic_load_n(&video_bytes,__ATOMIC_RELAXED));
#ifdef VIEW
    return video_decode(unit);
#endif
    return DR_OK;
}
static int audio_init(int config, const POPUS_MULTISTREAM_CONFIGURATION opus, void *context, int flags) {
    (void)context; (void)flags;
    printf("AUDIO_SETUP config=%d channels=%d rate=%d samples=%d\n", config,
           opus->channelCount, opus->sampleRate, opus->samplesPerFrame);
#ifdef PLAY_AUDIO
    if (opus->channelCount != 2 || opus->sampleRate != 48000 ||
        opus->samplesPerFrame < 120 || opus->samplesPerFrame > 5760) return -1;
    int error=0;
    opus_decoder=opus_multistream_decoder_create(opus->sampleRate,opus->channelCount,
                   opus->streams,opus->coupledStreams,opus->mapping,&error);
    audio_channels=opus->channelCount; audio_frame_samples=opus->samplesPerFrame;
    pcm=calloc((size_t)audio_channels*audio_frame_samples,sizeof *pcm);
    if (!opus_decoder || !pcm) return -1;
    if (audio_mode==0 || audio_mode==1) {
        audio_out=popen("aplay -q -D hw:0,0 -t raw -f S16_LE -c 2 -r 48000 --buffer-time=120000 --period-time=20000", "w");
        if (!audio_out) return -1;
        setvbuf(audio_out,NULL,_IONBF,0);
        puts("AUDIO_OUTPUT=REALTEK_SPEAKERS");
    } else puts("AUDIO_OUTPUT=PC_OR_MUTED");
#endif
    return 0;
}
static unsigned audio_peak;
static unsigned long long audio_above_256,audio_pcm_total;
static void audio_sample(char *sample, int length) {
    __atomic_add_fetch(&audio_packets,1,__ATOMIC_RELAXED);
#ifdef PLAY_AUDIO
    if (!opus_decoder || !audio_out) return;
    int n=opus_multistream_decode(opus_decoder,(unsigned char *)sample,length,
                                  pcm,audio_frame_samples,0);
    if (n<=0) { audio_errors++; return; }
    for (int i=0;i<n*audio_channels;i++) { int v=pcm[i]; unsigned a=(unsigned)(v<0?-v:v); if(a>audio_peak)audio_peak=a; if(a>256)audio_above_256++; audio_pcm_total++; pcm[i]=(int16_t)(v*audio_volume/100); }
    if (fwrite(pcm,sizeof *pcm,(size_t)n*audio_channels,audio_out)!=(size_t)n*audio_channels)
        audio_errors++;
    else audio_played++;
#else
    (void)sample; (void)length;
#endif
}
static void audio_close(void) {
#ifdef PLAY_AUDIO
    if (audio_out) pclose(audio_out);
    if (opus_decoder) opus_multistream_decoder_destroy(opus_decoder);
    free(pcm); audio_out=NULL; opus_decoder=NULL; pcm=NULL;
    printf("AUDIO_PLAYED=%u AUDIO_ERRORS=%u AUDIO_PEAK=%u AUDIO_ABOVE_256=%llu AUDIO_SAMPLES=%llu\n",audio_played,audio_errors,audio_peak,audio_above_256,audio_pcm_total);
#endif
}
static void terminated(int code) { printf("TERMINATED=%d\n", code); ended = 1; }
static void failed(int stage, int code) { printf("STAGE_FAILED=%d:%d\n", stage, code); ended = 1; }
static void log_message(const char *fmt, ...) {
    va_list args; va_start(args, fmt); vprintf(fmt, args); va_end(args);
}
int main(int argc, char **argv) {
    if (argc != 3 && argc != 4 && argc != 10 && argc != 11) return 2;
    setvbuf(stdout, NULL, _IONBF, 0);
    int duration=argc>=4 ? atoi(argv[3]) : 30;
    if (duration<1 || duration>3600) return 2;
    int width=1280,height=720,fps=60,bitrate=3000,app_id=881448767;
    if (argc>=10) {
        width=atoi(argv[4]); height=atoi(argv[5]); fps=atoi(argv[6]);
        bitrate=atoi(argv[7]); audio_mode=atoi(argv[8]); audio_volume=atoi(argv[9]);
        if (!((width==1280 && height==720) || (width==1920 && height==1080)) ||
            (fps!=30 && fps!=60) ||
            bitrate<1000 || bitrate>20000 || audio_mode<0 || audio_mode>3 ||
            audio_volume<0 || audio_volume>100) return 2;
    }
    if (argc==11) {
        char *end=NULL;long id=strtol(argv[10],&end,10);
        if (!end||*end||id<=0||id>2147483647) return 2;
        app_id=(int)id;
    }
    printf("STREAM_SETTINGS=%dx%d@%d bitrate=%d audio_mode=%d volume=%d\n",
           width,height,fps,bitrate,audio_mode,audio_volume);
    alarm((unsigned)duration+30);
    SERVER_DATA server;
    int result = gs_init(&server, argv[1], 0, argv[2], 0, true);
    if (result != GS_OK || !server.paired) {
        printf("HOST_NOT_PAIRED result=%d error=%s\n", result, gs_error ? gs_error : "none");
        return 1;
    }
    STREAM_CONFIGURATION cfg;
    LiInitializeStreamConfiguration(&cfg);
    cfg.width = width; cfg.height = height; cfg.fps = fps;
    cfg.bitrate = bitrate; cfg.packetSize = 1392;
    printf("HW_PACKET_SIZE=%d\n",cfg.packetSize);
    cfg.streamingRemotely = STREAM_CFG_LOCAL;
    cfg.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
    hw_hevc=getenv("DRMOSA_VIDEO_CODEC") && !strcmp(getenv("DRMOSA_VIDEO_CODEC"),"hevc");
    cfg.supportedVideoFormats = hw_hevc ? VIDEO_FORMAT_H265 : VIDEO_FORMAT_H264;
    printf("VIDEO_CODEC=%s\n",hw_hevc?"HEVC_EXPERIMENTAL":"H264");
    cfg.encryptionFlags = ENCFLG_NONE;
    network_diagnostics("start");
    if(video_open(width,height)){puts("VIDEO_OPEN_FAILED");video_close();return 1;}
    result = gs_start_app(&server, &cfg, app_id, false,
                          audio_mode==1 || audio_mode==2, 0);
    printf("START_APP=%d\n", result);
    if (result != GS_OK) {video_close();return 1;}
    DECODER_RENDERER_CALLBACKS video = {.setup=setup, .submitDecodeUnit=submit
#ifdef VIEW
      , .cleanup=video_close
#endif
    };
    AUDIO_RENDERER_CALLBACKS audio = {.init=audio_init, .decodeAndPlaySample=audio_sample,
                                      .cleanup=audio_close};
    CONNECTION_LISTENER_CALLBACKS conn = {.stageFailed=failed, .connectionTerminated=terminated, .logMessage=log_message};
#ifdef DOCK_INPUT
    conn.rumble=gamepad_rumble;
#endif
    result = LiStartConnection(&server.serverInfo, &cfg, &conn, &video, &audio, NULL, 0, NULL, 0);
    printf("START_CONNECTION=%d\n", result);
    int missing_audio=0;
    if (result == 0) {
#ifdef VIEW
        stream_info_start(width,height,fps);
#endif
#ifdef DOCK_INPUT
        if (!pthread_create(&input_thread,NULL,input_loop,NULL)) input_started=1;
#endif
        for (int i=0; i<duration && !ended; i++) {
            if (access("/tmp/drmosa-moonlight.stop",F_OK)==0) break;
            if (audio_mode==0 && i==8 &&
                __atomic_load_n(&audio_packets,__ATOMIC_ACQUIRE)==0) {
                puts("AUDIO_STARTUP_MISSING=1");missing_audio=1;break;
            }
            sleep(1);
#ifdef VIEW
            stream_info_update();
#endif
        }
#ifdef DOCK_INPUT
        input_stop=1;
        if (input_started) pthread_join(input_thread,NULL);
        printf("INPUT_EVENTS keys=%u pointer=%u\n",key_events,pointer_events);
#endif
#ifdef VIEW
        stream_info_close();
#endif
        LiStopConnection();
    }
    printf("RESULT frames=%u video_bytes=%u audio_packets=%u", frames, video_bytes, audio_packets);
#ifdef VIEW
    printf(" displayed=%u render_dropped=%u", displayed, render_dropped);
#endif
    putchar('\n');
    if(missing_audio)return 25;
    return hw_hevc ? (displayed && !hw_failed && !hevc.failed ? 0 : 1) : (frames ? 0 : 1);
}
