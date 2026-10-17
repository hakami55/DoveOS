#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <libyuv/convert_from_argb.h>
#include <libyuv/scale_argb.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define SRC_W 640
#define SRC_H 360
#define OUT_W 1280
#define OUT_H 720
#define FRAME_BYTES ((size_t)SRC_W*SRC_H*4)

typedef struct {
    uint8_t *data;
    uint32_t physical,bytes,width,height,stride,aligned_height;
    uint32_t magic1,ion_handle,magic2,allocated,magic3,magic4;
} vo_buffer;
_Static_assert(sizeof(vo_buffer)==52,"Vendor VOUT ABI");

static int key_stopped(int keyboard){
    struct input_event e;
    while(read(keyboard,&e,sizeof e)==sizeof e)
        if(e.type==EV_KEY&&e.code==KEY_F10&&e.value==1)return 1;
    return 0;
}

static void start_audio(const char *path,pid_t *decode_pid,pid_t *play_pid){
    int audio[2];
    *decode_pid=-1;*play_pid=-1;
    if(pipe(audio))return;
    *play_pid=fork();
    if(*play_pid==0){
        dup2(audio[0],STDIN_FILENO);
        close(audio[0]);close(audio[1]);
        execl("/usr/bin/aplay","aplay","-q","-D","plughw:RTKAlsaCard,0",
              "-t","raw","-f","S16_LE","-r","48000","-c","2",(char*)NULL);
        _exit(127);
    }
    *decode_pid=fork();
    if(*decode_pid==0){
        dup2(audio[1],STDOUT_FILENO);
        close(audio[0]);close(audio[1]);
        execl("/usr/bin/ffmpeg","ffmpeg","-re","-nostdin","-loglevel","error",
              "-i",path,"-map","0:a:0","-vn","-af","volume=0.2",
              "-ac","2","-ar","48000","-f","s16le","pipe:1",(char*)NULL);
        _exit(127);
    }
    close(audio[0]);close(audio[1]);
    if(*play_pid>0&&*decode_pid>0)puts("VIDEO_AUDIO_STARTED");
}

static void end_audio(pid_t pid){
    if(pid>0){kill(pid,SIGTERM);waitpid(pid,NULL,0);}
}

int play_raw_video(const char *path,int keyboard){
    int stream[2];
    if(pipe(stream)){perror("VIDEO_PIPE");return 1;}
    pid_t decoder=fork();
    if(decoder<0){close(stream[0]);close(stream[1]);return 1;}
    if(decoder==0){
        dup2(stream[1],STDOUT_FILENO);
        close(stream[0]);close(stream[1]);
        execl("/usr/bin/ffmpeg","ffmpeg","-re","-nostdin","-loglevel","error",
              "-i",path,"-map","0:v:0","-an","-sn","-vf","scale=640:360",
              "-pix_fmt","bgra","-f","rawvideo","pipe:1",(char*)NULL);
        _exit(127);
    }
    close(stream[1]);
    void *lib=dlopen("/usr/lib/libVOutWrapper.so",RTLD_LAZY|RTLD_LOCAL|RTLD_DEEPBIND);
    int result=1,ready=0,allocated=0,frames=0;
    pid_t audio_decode=-1,audio_play=-1;
    vo_buffer buffer={.width=OUT_W,.height=OUT_H,.stride=OUT_W,.aligned_height=OUT_H};
    int (*init)(void)=lib?dlsym(lib,"vo_wrap_init"):NULL;
    int (*force)(void)=lib?dlsym(lib,"vo_wrap_v1_force_on"):NULL;
    int (*alloc)(vo_buffer*)=lib?dlsym(lib,"vo_wrap_alloc_nv12_buffer"):NULL;
    int (*render)(vo_buffer*)=lib?dlsym(lib,"vo_wrap_render"):NULL;
    int (*stop)(void)=lib?dlsym(lib,"vo_wrap_v1_stop"):NULL;
    int (*deinit)(void)=lib?dlsym(lib,"vo_wrap_deinit"):NULL;
    int (*release)(vo_buffer*)=lib?dlsym(lib,"vo_wrap_free_nv12_buffer"):NULL;
    uint8_t *src=NULL,*scaled=NULL;
    if(!init||!force||!alloc||!render||!stop||!deinit||!release||init())goto done;
    ready=1;
    if(alloc(&buffer)||!buffer.data||buffer.bytes<(size_t)OUT_W*OUT_H*3/2)goto done;
    allocated=1;
    if((unsigned)force()!=0x10000000u)goto done;
    src=malloc(FRAME_BYTES);scaled=malloc((size_t)OUT_W*OUT_H*4);
    if(!src||!scaled)goto done;
    puts("VIDEO_DIRECT_READY");
    start_audio(path,&audio_decode,&audio_play);
    for(;;){
        size_t got=0;
        while(got<FRAME_BYTES){
            struct pollfd p[2]={{.fd=stream[0],.events=POLLIN|POLLHUP},
                                {.fd=keyboard,.events=POLLIN}};
            int n=poll(p,2,200);
            if(n<0&&errno==EINTR)continue;
            if(n<0)goto done;
            if(p[1].revents&POLLIN&&key_stopped(keyboard)){result=0;goto done;}
            if(n==0)continue;
            if(p[0].revents&(POLLIN|POLLHUP)){
                ssize_t amount=read(stream[0],src+got,FRAME_BYTES-got);
                if(amount==0){result=frames?0:1;goto done;}
                if(amount<0){if(errno==EINTR)continue;goto done;}
                got+=(size_t)amount;
            }
        }
        if(ARGBScale(src,SRC_W*4,SRC_W,SRC_H,scaled,OUT_W*4,OUT_W,OUT_H,kFilterNone)||
           ARGBToNV12(scaled,OUT_W*4,buffer.data,OUT_W,
                      buffer.data+(size_t)OUT_W*OUT_H,OUT_W,OUT_W,OUT_H))goto done;
        __sync_synchronize();
        if(render(&buffer))goto done;
        frames++;
        if(frames%24==0)printf("VIDEO_DIRECT_FRAMES=%d\n",frames);
        if(key_stopped(keyboard)){result=0;goto done;}
    }
done:
    close(stream[0]);
    kill(decoder,SIGTERM);waitpid(decoder,NULL,0);
    end_audio(audio_decode);end_audio(audio_play);
    if(ready){
        if((unsigned)stop()==0x10000000u&&allocated)release(&buffer);
        deinit();
    }
    if(lib)dlclose(lib);
    free(src);free(scaled);
    printf("VIDEO_DIRECT_DONE frames=%d result=%d\n",frames,result);
    return result;
}
