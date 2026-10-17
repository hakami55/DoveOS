#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc,char **argv){
    /* The HP autorun parent blocks termination signals across exec. */
    sigset_t unblocked;sigemptyset(&unblocked);
    sigprocmask(SIG_SETMASK,&unblocked,NULL);
    signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);
    if(argc==2&&!strcmp(argv[1],"--check")){puts("COMBO_CHECK_OK");return 0;}
    const char *stop_path="/tmp/drmosa-moonlight.stop";
    if(argc==3&&!strcmp(argv[1],"--stop-file")&&!strcmp(argv[2],"/tmp/doveos-dex.stop"))stop_path=argv[2];
    else if(argc!=1)return 3;
    int fd=open("/dev/input/event1",O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    if(fd<0)return 1;
    unsigned int keys[KEY_MAX/32+1]={0};
    ioctl(fd,EVIOCGKEY(sizeof keys),keys);
    struct pollfd p={.fd=fd,.events=POLLIN};
    while(poll(&p,1,250)>=0){
        if(!access(stop_path,F_OK)){puts("STREAM_STOP_REQUEST_OBSERVED");close(fd);return 0;}
        struct input_event e;
        while(read(fd,&e,sizeof e)==sizeof e){
            if(e.type!=EV_KEY||e.code>KEY_MAX)continue;
            unsigned int bit=1u<<(e.code%32);
            if(e.value==0)keys[e.code/32]&=~bit;
            else keys[e.code/32]|=bit;
            if(e.code==KEY_Q&&e.value==1&&
               ((keys[KEY_LEFTCTRL/32]>>(KEY_LEFTCTRL%32))&1u || (keys[KEY_RIGHTCTRL/32]>>(KEY_RIGHTCTRL%32))&1u)&&
               ((keys[KEY_LEFTALT/32]>>(KEY_LEFTALT%32))&1u || (keys[KEY_RIGHTALT/32]>>(KEY_RIGHTALT%32))&1u)&&
               ((keys[KEY_LEFTSHIFT/32]>>(KEY_LEFTSHIFT%32))&1u || (keys[KEY_RIGHTSHIFT/32]>>(KEY_RIGHTSHIFT%32))&1u)){
                int stop=open(stop_path,O_WRONLY|O_CREAT|O_CLOEXEC,0600);
                if(stop>=0){close(stop);puts("STREAM_STOP_BY_CTRL_ALT_SHIFT_Q");close(fd);return 0;}
            }
        }
    }
    close(fd);return 2;
}
