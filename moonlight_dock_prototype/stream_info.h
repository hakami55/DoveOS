/* Small OSD on the existing ARGB plane; no extra video/frame buffers. */
#include "../usb_desktop_poc/font.h"
enum { INFO_X=12,INFO_Y=12,INFO_W=576,INFO_H=94 };
static int info_enabled,info_width,info_height,info_target;
static unsigned info_frames,info_bytes,info_queued;
static double info_time;
static double info_now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void info_text(int row,const char *text){
 uint32_t *pixels=(uint32_t*)fb_map;
 for(int c=0;text[c]&&c<70;c++)for(int y=0;y<16;y++)for(int x=0;x<8;x++)
  if(font8x16[(unsigned char)text[c]&127][y]&(0x80>>x))
   pixels[(INFO_Y+7+row*20+y)*1280+INFO_X+8+c*8+x]=0xffffffff;
}
static void stream_info_start(int width,int height,int fps){
 const char *setting=getenv("DRMOSA_STREAM_INFO");
 info_enabled=setting&&!strcmp(setting,"1")&&fb_map;
 info_width=width;info_height=height;info_target=fps;
 info_frames=__atomic_load_n(&frames,__ATOMIC_RELAXED);
 info_bytes=__atomic_load_n(&video_bytes,__ATOMIC_RELAXED);
 info_queued=__atomic_load_n(&displayed,__ATOMIC_RELAXED);info_time=info_now();
 printf("STREAM_INFO_ENABLED=%d\n",info_enabled);
}
static void stream_info_update(void){
 if(!info_enabled||!fb_map)return;
 double now=info_now(),span=now-info_time;if(span<0.5)return;
 unsigned received=__atomic_load_n(&frames,__ATOMIC_RELAXED);
 unsigned bytes=__atomic_load_n(&video_bytes,__ATOMIC_RELAXED);
 unsigned queued=__atomic_load_n(&displayed,__ATOMIC_RELAXED);
 unsigned dropped=__atomic_load_n(&render_dropped,__ATOMIC_RELAXED);
 uint32_t rtt=0,variance=0;char latency[24],line[128];
 if(LiGetEstimatedRttInfo(&rtt,&variance))snprintf(latency,sizeof latency,"%u ms",rtt);
 else snprintf(latency,sizeof latency,"unavailable");
 uint32_t *pixels=(uint32_t*)fb_map;
 for(int y=0;y<INFO_H;y++)for(int x=0;x<INFO_W;x++)pixels[(INFO_Y+y)*1280+INFO_X+x]=0xe0182028;
 snprintf(line,sizeof line,"%dx%d / %s / target %d FPS",info_width,info_height,hw_hevc?"HEVC":"H.264",info_target);info_text(0,line);
 snprintf(line,sizeof line,"Video: %.1f received / %.1f queued FPS",(received-info_frames)/span,(queued-info_queued)/span);info_text(1,line);
 snprintf(line,sizeof line,"Video: %.2f Mbps / RTT: %s",(bytes-info_bytes)*8.0/span/1000000,latency);info_text(2,line);
 snprintf(line,sizeof line,"Queue: %d video / %d ms audio / render drops: %u",LiGetPendingVideoFrames(),LiGetPendingAudioDuration(),dropped);info_text(3,line);
 __sync_synchronize();
 printf("STREAM_INFO received_fps=%.1f queued_fps=%.1f video_mbps=%.2f rtt=%s render_drops=%u\n",(received-info_frames)/span,(queued-info_queued)/span,(bytes-info_bytes)*8.0/span/1000000,latency,dropped);
 info_frames=received;info_bytes=bytes;info_queued=queued;info_time=now;
}
static void stream_info_close(void){
 if(info_enabled&&fb_map){uint32_t *pixels=(uint32_t*)fb_map;for(int y=0;y<INFO_H;y++)memset(pixels+(INFO_Y+y)*1280+INFO_X,0,INFO_W*4);__sync_synchronize();}
 info_enabled=0;
}
