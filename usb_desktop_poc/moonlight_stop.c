#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--check")){puts("MONITOR_CHECK_OK");return 0;}
    int input=open("/dev/input/event1",O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    if(input<0){perror("keyboard");return 1;}
    struct pollfd p={.fd=input,.events=POLLIN};
    for(int elapsed=0;elapsed<1900*2;elapsed++){
        if(poll(&p,1,500)<=0)continue;
        struct input_event ev;
        while(read(input,&ev,sizeof ev)==sizeof ev){
            if(ev.type==EV_KEY&&ev.code==KEY_F10&&ev.value==1){
                int stop=open("/tmp/drmosa-moonlight.stop",O_WRONLY|O_CREAT|O_CLOEXEC,0600);
                if(stop>=0){close(stop);puts("MOONLIGHT_STOP_BY_F10");close(input);return 0;}
            }
        }
    }
    close(input);return 2;
}
