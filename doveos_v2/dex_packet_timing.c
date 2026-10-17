#define _GNU_SOURCE
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
static unsigned be16(const unsigned char *p){return ((unsigned)p[0]<<8)|p[1];}
int main(void){
 int fd=socket(AF_PACKET,SOCK_RAW,htons(ETH_P_ALL));if(fd<0)return 1;
 struct sockaddr_ll a={.sll_family=AF_PACKET,.sll_protocol=htons(ETH_P_ALL),.sll_ifindex=if_nametoindex("p2p0")};
 if(!a.sll_ifindex||bind(fd,(void*)&a,sizeof a))return 2;
 int one=1;if(setsockopt(fd,SOL_SOCKET,SO_TIMESTAMPNS,&one,sizeof one))return 3;
 setvbuf(stdout,NULL,_IONBF,0);puts("DEX_PACKET_TIMING_READY metadata_only=1");
 struct timespec begin,now;clock_gettime(CLOCK_MONOTONIC,&begin);
 struct timespec wall;clock_gettime(CLOCK_REALTIME,&wall);
 printf("CLOCK mono=%ld.%09ld wall=%ld.%09ld\n",(long)begin.tv_sec,begin.tv_nsec,(long)wall.tv_sec,wall.tv_nsec);
 struct pollfd p={fd,POLLIN,0};unsigned char b[4096];char control[128];
 while(1){clock_gettime(CLOCK_MONOTONIC,&now);if(now.tv_sec-begin.tv_sec>=8)break;
  if(poll(&p,1,100)<=0)continue;
  struct sockaddr_ll peer;struct iovec iov={b,sizeof b};
  struct msghdr m={.msg_name=&peer,.msg_namelen=sizeof peer,.msg_iov=&iov,.msg_iovlen=1,.msg_control=control,.msg_controllen=sizeof control};
  ssize_t n=recvmsg(fd,&m,0);if(n<54||be16(b+12)!=ETH_P_IP)continue;
  unsigned char *ip=b+14;int ihl=(ip[0]&15)*4;
  if(ihl<20||ip[9]!=6||n<14+ihl+20)continue;
  unsigned char *tcp=ip+ihl;int thl=(tcp[12]>>4)*4,len=(int)be16(ip+2)-ihl-thl;if(thl<20||len<0)continue;
  int from_phone=ip[12]==192&&ip[13]==168&&ip[14]==49&&ip[15]==1;
  int to_phone=ip[16]==192&&ip[17]==168&&ip[18]==49&&ip[19]==1;
  if(!from_phone&&!to_phone)continue;
  struct timespec stamp={0};
  for(struct cmsghdr *c=CMSG_FIRSTHDR(&m);c;c=CMSG_NXTHDR(&m,c))if(c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_TIMESTAMPNS)stamp=*(struct timespec*)CMSG_DATA(c);
  unsigned seq=((unsigned)tcp[4]<<24)|((unsigned)tcp[5]<<16)|((unsigned)tcp[6]<<8)|tcp[7];
  unsigned ack=((unsigned)tcp[8]<<24)|((unsigned)tcp[9]<<16)|((unsigned)tcp[10]<<8)|tcp[11];
  clock_gettime(CLOCK_MONOTONIC,&now);
  printf("PACKET ts=%ld.%09ld mono=%ld.%09ld dir=%s src=%u port=%u seq=%u ack=%u flags=%u bytes=%d\n",
    (long)stamp.tv_sec,stamp.tv_nsec,(long)now.tv_sec,now.tv_nsec,peer.sll_pkttype==PACKET_OUTGOING?"out":"in",
    be16(tcp),be16(tcp+2),seq,ack,tcp[13],len);
 }
 close(fd);puts("DEX_PACKET_TIMING_DONE");return 0;
}
