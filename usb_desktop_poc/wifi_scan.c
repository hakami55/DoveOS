#define _POSIX_C_SOURCE 200809L
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <poll.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <net/if.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <linux/nl80211.h>
static int fd=-1;
static int events=-1;
static unsigned scan_family;
static char local[108];
static void cleanup(void){if(fd>=0)close(fd);if(events>=0)close(events);if(local[0])unlink(local);}
static void interrupted(int sig){cleanup();_exit(128+sig);}
static long long millis(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static int receive(char *buf,size_t cap,long long deadline){
 long long left=deadline-millis();if(left<=0)return -1;
 struct pollfd p={fd,POLLIN,0};if(poll(&p,1,(int)left)<=0)return -1;
 ssize_t n=recv(fd,buf,cap-1,0);if(n<=0)return -1;buf[n]=0;return (int)n;
}
static int command(const char *cmd,char *buf,size_t cap){
 if(send(fd,cmd,strlen(cmd),0)<0)return -1;
 long long end=millis()+2000;
 while(receive(buf,cap,end)>0){if(buf[0]!='<')return 0;}
 return -1;
}
static int valid_attr(const struct nlattr *a,int left){return left>=(int)sizeof *a&&a->nla_len>=sizeof *a&&a->nla_len<=left;}
static const void *attr_data(const struct nlattr*a){return (const char*)a+NLA_HDRLEN;}
/* Observe the kernel's scan result. Old supplicants also emit SCAN-RESULTS
   after an aborted scan, so their event alone cannot establish success. */
static int attach_scan_events(void){
 int s=socket(AF_NETLINK,SOCK_RAW,NETLINK_GENERIC);if(s<0)return -1;
 struct sockaddr_nl address={.nl_family=AF_NETLINK};
 if(bind(s,(struct sockaddr*)&address,sizeof address)){close(s);return -1;}
 char request[128]={0};struct nlmsghdr*h=(void*)request;struct genlmsghdr*g=(void*)(request+NLMSG_HDRLEN);
 h->nlmsg_len=NLMSG_HDRLEN+GENL_HDRLEN;h->nlmsg_type=GENL_ID_CTRL;h->nlmsg_flags=NLM_F_REQUEST;h->nlmsg_seq=1;
 g->cmd=CTRL_CMD_GETFAMILY;g->version=1;
 struct nlattr*a=(void*)(request+h->nlmsg_len);a->nla_type=CTRL_ATTR_FAMILY_NAME;a->nla_len=NLA_HDRLEN+sizeof("nl80211");
 memcpy((char*)a+NLA_HDRLEN,"nl80211",sizeof("nl80211"));h->nlmsg_len+=NLA_ALIGN(a->nla_len);
 if(sendto(s,request,h->nlmsg_len,0,(struct sockaddr*)&address,sizeof address)<0){close(s);return -1;}
 struct pollfd p={s,POLLIN,0};char buffer[8192];
 if(poll(&p,1,2000)<=0){close(s);return -1;}
 int size=(int)recv(s,buffer,sizeof buffer,0);unsigned group=0;
 for(h=(void*)buffer;NLMSG_OK(h,size);h=NLMSG_NEXT(h,size)){
  if(h->nlmsg_type!=GENL_ID_CTRL||h->nlmsg_len<NLMSG_HDRLEN+GENL_HDRLEN)continue;
  int left=h->nlmsg_len-NLMSG_HDRLEN-GENL_HDRLEN;
  for(a=(void*)((char*)h+NLMSG_HDRLEN+GENL_HDRLEN);valid_attr(a,left);left-=NLA_ALIGN(a->nla_len),a=(void*)((char*)a+NLA_ALIGN(a->nla_len))){
   if((a->nla_type&NLA_TYPE_MASK)==CTRL_ATTR_FAMILY_ID&&a->nla_len==NLA_HDRLEN+2){uint16_t id;memcpy(&id,attr_data(a),2);scan_family=id;}
   if((a->nla_type&NLA_TYPE_MASK)!=CTRL_ATTR_MCAST_GROUPS)continue;
   int groups=a->nla_len-NLA_HDRLEN;struct nlattr *entry=(void*)attr_data(a);
   for(;valid_attr(entry,groups);groups-=NLA_ALIGN(entry->nla_len),entry=(void*)((char*)entry+NLA_ALIGN(entry->nla_len))){
    int fields=entry->nla_len-NLA_HDRLEN;struct nlattr *v=(void*)attr_data(entry);unsigned id=0;int scan=0;
    for(;valid_attr(v,fields);fields-=NLA_ALIGN(v->nla_len),v=(void*)((char*)v+NLA_ALIGN(v->nla_len))){
     unsigned type=v->nla_type&NLA_TYPE_MASK,len=v->nla_len-NLA_HDRLEN;
     if(type==CTRL_ATTR_MCAST_GRP_NAME&&len==sizeof("scan")&&!memcmp(attr_data(v),"scan",len))scan=1;
     if(type==CTRL_ATTR_MCAST_GRP_ID&&len==4)memcpy(&id,attr_data(v),4);
    }
    if(scan)group=id;
   }
  }
 }
 if(!scan_family||!group||setsockopt(s,SOL_NETLINK,NETLINK_ADD_MEMBERSHIP,&group,sizeof group)){close(s);return -1;}
 return s;
}
static int kernel_scan_result(unsigned interface){
 char buffer[8192];int size=(int)recv(events,buffer,sizeof buffer,MSG_DONTWAIT);
 if(size<0)return 0;
 struct nlmsghdr*h;int result=0;
 for(h=(void*)buffer;NLMSG_OK(h,size);h=NLMSG_NEXT(h,size)){
  if(h->nlmsg_type!=scan_family||h->nlmsg_len<NLMSG_HDRLEN+GENL_HDRLEN)continue;
  struct genlmsghdr*g=(void*)((char*)h+NLMSG_HDRLEN);
  if(g->cmd!=NL80211_CMD_SCAN_ABORTED&&g->cmd!=NL80211_CMD_NEW_SCAN_RESULTS)continue;
  unsigned index=0;int left=h->nlmsg_len-NLMSG_HDRLEN-GENL_HDRLEN;
  struct nlattr*a=(void*)((char*)h+NLMSG_HDRLEN+GENL_HDRLEN);
  for(;valid_attr(a,left);left-=NLA_ALIGN(a->nla_len),a=(void*)((char*)a+NLA_ALIGN(a->nla_len)))
   if((a->nla_type&NLA_TYPE_MASK)==NL80211_ATTR_IFINDEX&&a->nla_len==NLA_HDRLEN+4)memcpy(&index,attr_data(a),4);
  if(index==interface){if(g->cmd==NL80211_CMD_SCAN_ABORTED)return -1;result=1;}
 }
 return result;
}
int main(int argc,char **argv){
 if(argc!=2)return 2;
 int all=!strcmp(argv[1],"all");
 if(!all)for(const char *p=argv[1];*p;p++)if((*p<'0'||*p>'9')&&*p!=',')return 2;
 if(!*argv[1]||strlen(argv[1])>160)return 2;
 sigset_t clear;sigemptyset(&clear);sigprocmask(SIG_SETMASK,&clear,NULL);umask(077);
 signal(SIGTERM,interrupted);signal(SIGINT,interrupted);signal(SIGALRM,interrupted);alarm(22);
 unsigned interface=if_nametoindex("wlan0");if(!interface)return 3;
 events=attach_scan_events();if(events<0){fputs("Cannot monitor kernel scan completion.\n",stderr);return 9;}
 fd=socket(AF_UNIX,SOCK_DGRAM,0);if(fd<0)return 3;atexit(cleanup);
 snprintf(local,sizeof local,"/tmp/doveos-wifi/scan-socket-%ld",(long)getpid());
 struct sockaddr_un a={0};a.sun_family=AF_UNIX;snprintf(a.sun_path,sizeof a.sun_path,"%s",local);
 if(bind(fd,(struct sockaddr*)&a,sizeof a))return 3;
 snprintf(a.sun_path,sizeof a.sun_path,"/tmp/doveos-wifi/ctrl/wlan0");
 if(connect(fd,(struct sockaddr*)&a,sizeof a))return 3;
 char buf[32768],cmd[192];
 if(command("ATTACH",buf,sizeof buf)||strncmp(buf,"OK",2))return 4;
 if(all)snprintf(cmd,sizeof cmd,"SCAN");else snprintf(cmd,sizeof cmd,"SCAN freq=%s",argv[1]);
 if(command(cmd,buf,sizeof buf)||strncmp(buf,"OK",2))return 5;
 long long end=millis()+15000;int complete=0,supplicant_complete=0;
 while(millis()<end){
  struct pollfd pollers[2]={{events,POLLIN,0},{fd,POLLIN,0}};
  int n=poll(pollers,2,(int)(end-millis()));if(n<0&&errno==EINTR)continue;if(n<=0)break;
  if(pollers[0].revents&POLLIN){int k=kernel_scan_result(interface);if(k<0){fputs("Kernel aborted the scan.\n",stderr);return 6;}if(k>0)complete=1;}
  if(pollers[1].revents&POLLIN){ssize_t len=recv(fd,buf,sizeof buf-1,0);if(len>0){buf[len]=0;if(strstr(buf,"CTRL-EVENT-SCAN-FAILED"))return 6;if(strstr(buf,"CTRL-EVENT-SCAN-RESULTS"))supplicant_complete=1;}}
  /* The kernel event can precede wpa_supplicant's BSS table update. */
  if(complete&&supplicant_complete)break;
 }
 if(!complete||!supplicant_complete)return 7;
 if(command("SCAN_RESULTS",buf,sizeof buf))return 8;
 fputs(buf,stdout);return 0;
}
