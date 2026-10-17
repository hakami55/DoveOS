#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "file_types.h"

#define MEDIA_ROOT "/media/usb0/"
int play_raw_video(const char *path,int keyboard);
static volatile sig_atomic_t stop;
static void on_signal(int unused){(void)unused;stop=1;}
static void stop_child(pid_t pid,int *status){
    if(pid<=0)return;
    kill(pid,SIGTERM);
    for(int i=0;i<40;i++){
        if(waitpid(pid,status,WNOHANG)==pid)return;
        usleep(50000);
    }
    kill(pid,SIGKILL);
    for(int i=0;i<20;i++){
        if(waitpid(pid,status,WNOHANG)==pid)return;
        usleep(50000);
    }
    puts("MEDIA_CHILD_REAP_TIMEOUT");
}
static int supported(const char *path,enum dove_file_type *type){
    if(strncmp(path,MEDIA_ROOT,strlen(MEDIA_ROOT)))return 0;
    const char *name=path+strlen(MEDIA_ROOT);
    if(!*name||strstr(name,"/../")||strstr(name,"/./")||
       !strncmp(name,"../",3)||strstr(name,"//"))return 0;
    *type=dove_file_type(name);
    return *type==DOVE_PICTURE||*type==DOVE_AUDIO||*type==DOVE_VIDEO;
}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--check")){
        enum dove_file_type type=DOVE_UNSUPPORTED;
        if(!supported(MEDIA_ROOT "DrMoSa/Media/sample-video.mp4",&type)||type!=DOVE_VIDEO||
           !supported(MEDIA_ROOT "DrMoSa/Media/sample-audio.wav",&type)||type!=DOVE_AUDIO||
           !supported(MEDIA_ROOT "DrMoSa/Media/photo.png",&type)||type!=DOVE_PICTURE||
           supported("/tmp/other.mp4",&type)||supported(MEDIA_ROOT "other.bin",&type))return 1;
        puts("MEDIA_PLAYER_CHECK_OK");return 0;
    }
    enum dove_file_type type=DOVE_UNSUPPORTED;
    if(argc!=2||!supported(argv[1],&type)){
        puts("MEDIA_PATH_INVALID");return 2;
    }
    char *resolved=realpath(argv[1],NULL);
    struct stat st;if(!resolved||strncmp(resolved,MEDIA_ROOT,strlen(MEDIA_ROOT))||
                      lstat(argv[1],&st)||!S_ISREG(st.st_mode)){
        free(resolved);
        puts("MEDIA_FILE_MISSING");return 3;
    }
    free(resolved);
    setvbuf(stdout,NULL,_IONBF,0);
    signal(SIGINT,on_signal);signal(SIGTERM,on_signal);
    int keyboard=open("/dev/input/event1",O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    if(keyboard<0){perror("MEDIA_KEYBOARD");return 4;}
    if(type==DOVE_VIDEO){
        printf("MEDIA_PLAY_STARTED type=video file=%s\n",argv[1]);
        int result=play_raw_video(argv[1],keyboard);
        close(keyboard);
        printf("MEDIA_PLAY_DONE type=video result=%d\n",result);
        return result?6:0;
    }
    pid_t mirror=-1;
    if(type==DOVE_PICTURE){
        mirror=fork();
        if(mirror<0){close(keyboard);return 5;}
        if(mirror==0){
            execl("/tmp/drmosa-usb-menu-runtime/lib/ld-linux-armhf.so.3","ld-linux-armhf.so.3",
                  "--library-path","/tmp/drmosa-usb-menu-runtime/lib",
                  "/tmp/drmosa-usb-menu-runtime/media-fb-mirror",(char*)NULL);
            _exit(127);
        }
        usleep(300000);
    }
    pid_t child=fork();
    if(child<0){
        if(mirror>0){kill(mirror,SIGTERM);waitpid(mirror,NULL,0);}
        close(keyboard);return 5;
    }
    if(child==0){
        if(type==DOVE_PICTURE){
            setenv("SDL_VIDEODRIVER","fbcon",1);
            setenv("SDL_AUDIODRIVER","alsa",1);
            setenv("AUDIODEV","plughw:RTKAlsaCard,0",1);
            if(type==DOVE_PICTURE)
                execl("/usr/bin/ffplay","ffplay","-loop","0","-fs","-an",
                      "-loglevel","warning",argv[1],(char*)NULL);
        }else if(!strcasecmp(strrchr(argv[1],'.'),".wav")){
            execl("/usr/bin/aplay","aplay","-q","-D","plughw:RTKAlsaCard,0",
                  argv[1],(char*)NULL);
        }else{
            setenv("SDL_AUDIODRIVER","alsa",1);
            setenv("AUDIODEV","plughw:RTKAlsaCard,0",1);
            execl("/usr/bin/ffplay","ffplay","-nodisp","-autoexit",
                  "-loglevel","warning",argv[1],(char*)NULL);
        }
        _exit(127);
    }
    printf("MEDIA_PLAY_STARTED type=%s file=%s\n",type==DOVE_VIDEO?"video":
           type==DOVE_PICTURE?"picture":"audio",argv[1]);
    time_t end=time(NULL)+3600;int status=0,finished=0;
    while(!stop&&time(NULL)<end){
        if(waitpid(child,&status,WNOHANG)==child){finished=1;break;}
        struct input_event event;
        while(read(keyboard,&event,sizeof event)==sizeof event)
            if(event.type==EV_KEY&&event.code==KEY_F10&&event.value==1)stop=1;
        usleep(30000);
    }
    if(!finished)stop_child(child,&status);
    if(mirror>0)stop_child(mirror,NULL);
    close(keyboard);
    printf("MEDIA_PLAY_DONE reason=%s exit=%d\n",stop?"F10":finished?"ended":"timeout",
           WIFEXITED(status)?WEXITSTATUS(status):-1);
    return finished&&WIFEXITED(status)&&WEXITSTATUS(status)!=0?6:0;
}
