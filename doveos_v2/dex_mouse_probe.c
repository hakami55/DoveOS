#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
int main(int argc,char **argv){
 if(argc!=2)return 2;
 char *end;long port=strtol(argv[1],&end,10);if(*end||port<1||port>65535)return 2;
 int s=socket(AF_INET,SOCK_STREAM,0);if(s<0)return 1;
 fcntl(s,F_SETFL,O_NONBLOCK);
 struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons((unsigned short)port)};
 inet_pton(AF_INET,"192.168.49.1",&a.sin_addr);
 int r=connect(s,(void *)&a,sizeof a);
 if(r<0&&errno!=EINPROGRESS){perror("connect");close(s);return 1;}
 struct pollfd p={s,POLLOUT,0};int err=0;socklen_t n=sizeof err;
 if(poll(&p,1,1500)<1||getsockopt(s,SOL_SOCKET,SO_ERROR,&err,&n)||err){puts("MOUSE_SECOND_CONNECTION_FAILED");close(s);return 1;}
 unsigned char packet[]={0,0x11,0,13,1,1,0,0,3,0,0,0,0};
 struct timespec pause={.tv_nsec=300000000};
 if(send(s,packet,sizeof packet,MSG_NOSIGNAL)!=(ssize_t)sizeof packet){close(s);return 1;}
 nanosleep(&pause,NULL);packet[10]=20;packet[11]=10;
 if(send(s,packet,sizeof packet,MSG_NOSIGNAL)!=(ssize_t)sizeof packet){close(s);return 1;}
 nanosleep(&pause,NULL);packet[10]=packet[11]=0;
 if(send(s,packet,sizeof packet,MSG_NOSIGNAL)!=(ssize_t)sizeof packet){close(s);return 1;}
 nanosleep(&pause,NULL);close(s);puts("MOUSE_SECOND_CONNECTION_PROBE_SENT");return 0;
}
