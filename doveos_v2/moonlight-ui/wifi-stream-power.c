#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/stat.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <linux/nl80211.h>
#include <net/if.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopped;
static void stop(int sig){stopped=sig;}
static double now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))exit(40);return t.tv_sec+t.tv_nsec/1e9;}
#ifndef PS_TESTING
static uint32_t sequence;
static int attr(char *b,size_t *len,uint16_t type,const void *data,size_t n){
    size_t total=NLA_ALIGN(NLA_HDRLEN+n);if(*len+total>4096)return -1;
    struct nlattr a={.nla_len=NLA_HDRLEN+n,.nla_type=type};
    memcpy(b+*len,&a,sizeof a);memcpy(b+*len+NLA_HDRLEN,data,n);*len+=total;return 0;
}
static int request(int family,uint8_t cmd,unsigned index,int set,uint32_t *value){
    int fd=socket(AF_NETLINK,SOCK_RAW|SOCK_CLOEXEC,NETLINK_GENERIC);if(fd<0)return -1;
    struct timeval tv={2,0};if(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv))goto fail;
    struct sockaddr_nl local={.nl_family=AF_NETLINK},kernel={.nl_family=AF_NETLINK};
    if(bind(fd,(void*)&local,sizeof local))goto fail;
    _Alignas(struct nlmsghdr) char buf[4096]={0};size_t len=NLMSG_HDRLEN+GENL_HDRLEN;
    uint32_t seq=++sequence;
    struct nlmsghdr *h=(void*)buf;
    h->nlmsg_type=family;h->nlmsg_flags=NLM_F_REQUEST|(set?NLM_F_ACK:0);h->nlmsg_seq=seq;
    struct genlmsghdr *g=(void*)(buf+NLMSG_HDRLEN);g->cmd=cmd;g->version=1;
    if(family==GENL_ID_CTRL){if(attr(buf,&len,CTRL_ATTR_FAMILY_NAME,"nl80211",8))goto fail;}
    else {
        if(attr(buf,&len,NL80211_ATTR_IFINDEX,&index,4))goto fail;
        if(set && attr(buf,&len,NL80211_ATTR_PS_STATE,value,4))goto fail;
    }
    h->nlmsg_len=len;if(sendto(fd,buf,len,0,(void*)&kernel,sizeof kernel)!=(ssize_t)len)goto fail;
    double deadline=now()+3;
    while(now()<deadline){
        struct sockaddr_nl sender={0};socklen_t slen=sizeof sender;
        int count=recvfrom(fd,buf,sizeof buf,0,(void*)&sender,&slen);
        if(count<0){if(errno==EINTR)continue;goto fail;}
        if(sender.nl_pid!=0)continue;
        for(h=(void*)buf;NLMSG_OK(h,count);h=NLMSG_NEXT(h,count)){
            if(h->nlmsg_seq!=seq)continue;
            if(h->nlmsg_type==NLMSG_ERROR){
                if(h->nlmsg_len<NLMSG_LENGTH(sizeof(struct nlmsgerr))){errno=EPROTO;goto fail;}
                struct nlmsgerr *e=NLMSG_DATA(h);if(e->error){errno=-e->error;goto fail;}
                if(set){close(fd);return 0;}continue;
            }
            if(h->nlmsg_type!=family || h->nlmsg_len<NLMSG_HDRLEN+GENL_HDRLEN)continue;
            int left=h->nlmsg_len-NLMSG_HDRLEN-GENL_HDRLEN;char *at=(char*)NLMSG_DATA(h)+GENL_HDRLEN;
            while(left>=NLA_HDRLEN){
                struct nlattr a;memcpy(&a,at,sizeof a);if(a.nla_len<NLA_HDRLEN || a.nla_len>left)break;
                int wanted=family==GENL_ID_CTRL?CTRL_ATTR_FAMILY_ID:NL80211_ATTR_PS_STATE;
                if((a.nla_type&NLA_TYPE_MASK)==wanted){
                    size_t n=family==GENL_ID_CTRL?2:4;if(a.nla_len<NLA_HDRLEN+n){errno=EPROTO;goto fail;}
                    *value=0;memcpy(value,at+NLA_HDRLEN,n);close(fd);return 0;
                }
                int step=NLA_ALIGN(a.nla_len);left-=step;at+=step;
            }
        }
    }
    errno=ETIMEDOUT;
fail: {int saved=errno;close(fd);errno=saved;return -1;}
}
static int access_ps(const char *iface,int set,int *value){
    uint32_t family=0,v=*value;unsigned index=if_nametoindex(iface);if(!index){errno=ENODEV;return -1;}
    if(request(GENL_ID_CTRL,CTRL_CMD_GETFAMILY,0,0,&family))return -1;
    if(!family || family>65535){errno=EPROTO;return -1;}
    if(request(family,set?NL80211_CMD_SET_POWER_SAVE:NL80211_CMD_GET_POWER_SAVE,index,set,&v))return -1;
    if(v>1){errno=EPROTO;return -1;}*value=v;return 0;
}
#else
static int fake_value=1,sets;
static int access_ps(const char *iface,int set,int *value){
    (void)iface;const char *mode=getenv("PS_FIXTURE");if(!mode)mode="normal";
    if(!strcmp(mode,"unsupported")){errno=EOPNOTSUPP;return -1;}
    if(!strcmp(mode,"already-off"))fake_value=0;
    if(set){sets++;printf("FIXTURE_SET=%d\n",*value);
        if(!strcmp(mode,"set-failure") && sets==1){errno=EIO;return -1;}
        if(!strcmp(mode,"restore-failure") && *value==1){errno=EIO;return -1;}
        if(!strcmp(mode,"verify-failure") && sets==1)return 0;
        fake_value=*value;
    }else *value=fake_value;
    return 0;
}
#endif

static unsigned long long parent_start(pid_t pid){
    char path[64],line[1024];snprintf(path,sizeof path,"/proc/%ld/stat",(long)pid);
    FILE *f=fopen(path,"r");if(!f)return 0;
    char *ok=fgets(line,sizeof line,f);fclose(f);if(!ok)return 0;
    char *end=strrchr(line,')');if(!end)return 0;
    char *save=NULL,*word=strtok_r(end+2," ",&save);
    for(int field=3;word;field++,word=strtok_r(NULL," ",&save))
        if(field==22)return strtoull(word,NULL,10);
    return 0;
}
int main(int argc,char **argv){
    setvbuf(stdout,NULL,_IONBF,0);
    if(argc==2 && !strcmp(argv[1],"--inspect")){
        for(int i=0;i<2;i++){const char *name=i?"p2p0":"wlan0";int v=0;
            if(access_ps(name,0,&v))printf("PS_QUERY iface=%s errno=%d\n",name,errno);
            else printf("PS_QUERY iface=%s requested_state=%d\n",name,v);
        }return 0;
    }

    if(argc!=3 || strcmp(argv[1],"--guard"))return 10;
    char *end=NULL;long parent=strtol(argv[2],&end,10);
    if(!end||*end||parent<=1||parent!=getppid())return 11;
    unsigned long long identity=parent_start((pid_t)parent);if(!identity)return 12;
#ifndef PS_TESTING
    char mac[64]={0};FILE *f=fopen("/sys/class/net/wlan0/address","r");
    if(!f)return 15;
    char *read=fgets(mac,sizeof mac,f);fclose(f);
    /* Public runtime: require the target interface, not an owner's address. */
    if(!read||strlen(mac)<17||!if_nametoindex("wlan0"))return 15;
#endif
    signal(SIGINT,stop);signal(SIGTERM,stop);signal(SIGHUP,stop);
    int original=0,changed=0,result=1;
    if(access_ps("wlan0",0,&original)){printf("PS_QUERY_FAILED errno=%d\nPS_RESTORE_REQUIRED=0\n",errno);return 13;}
    printf("PS_ORIGINAL=%d\n",original);
    if(!original){puts("PS_ALREADY_DISABLED=1\nPS_RESTORE_REQUIRED=0\nPS_COMPLETE=1");return 0;}
    /* Mark restore required before SET: an interrupted ACK may follow a successful write. */
    changed=1;puts("PS_RESTORE_REQUIRED=1");int off=0;
    if(access_ps("wlan0",1,&off)){printf("PS_SET_FAILED errno=%d\n",errno);goto restore;}
    int observed=1;
    if(access_ps("wlan0",0,&observed) || observed!=0){puts("PS_VERIFY_FAILED=1");goto restore;}
    puts("PS_DISABLED_VERIFIED=1\nPS_READY=1");
    double deadline=now()+1860;
#ifdef PS_TESTING
    deadline=now()+1;
#endif
    while(!stopped && now()<deadline && parent_start((pid_t)parent)==identity){struct timespec wait={0,100000000};nanosleep(&wait,NULL);}
    puts(stopped?"PS_STOP=SIGNAL":now()>=deadline?"PS_STOP=DEADLINE":"PS_STOP=PARENT_EXIT");result=0;
restore:
    if(changed){
        int restored=0;
        for(int attempt=0;attempt<2;attempt++){
            int v=original,check=-1;
            if(!access_ps("wlan0",1,&v) && !access_ps("wlan0",0,&check) && check==original){restored=1;break;}
        }
        printf("PS_RESTORED=%d\n",restored);if(!restored)result=14;
    }
    printf("PS_COMPLETE=%d\n",result==0);return result;
}
