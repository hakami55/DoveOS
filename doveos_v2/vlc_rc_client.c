/* Send a media file to the already running VLC; no shell commands or network. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
int main(int argc,char **argv){
    if(argc!=2 || argv[1][0]!='/')return 2;
    size_t len=strlen(argv[1]);if(len>4096)return 2;
    char *cmd=malloc(len*3+64);if(!cmd)return 2;
    strcpy(cmd,"stop\nclear\nadd file://");size_t pos=strlen(cmd);
    for(size_t i=0;i<len;i++){
        unsigned char c=argv[1][i];
        if(isalnum(c) || strchr("/-_.~",c))cmd[pos++]=c;
        else{snprintf(cmd+pos,4,"%%%02X",c);pos+=3;}
    }
    cmd[pos++]='\n';
    int fd=-1;struct sockaddr_un addr={.sun_family=AF_UNIX};strcpy(addr.sun_path,"/tmp/doveos-optional-runtime/vlc-control.sock");
    for(unsigned attempt=0;attempt<80;attempt++){
        fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd<0)break;
        if(!connect(fd,(struct sockaddr*)&addr,sizeof addr))break;
        close(fd);fd=-1;usleep(100000);
    }
    if(fd<0){free(cmd);return 3;}
    struct timeval limit={2,0};setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&limit,sizeof limit);
    size_t off=0;
    while(off<pos){ssize_t n=send(fd,cmd+off,pos-off,MSG_NOSIGNAL);if(n<0 && errno==EINTR)continue;if(n<=0){close(fd);free(cmd);return 4;}off+=n;}
    usleep(200000);close(fd);free(cmd);return 0;
}
