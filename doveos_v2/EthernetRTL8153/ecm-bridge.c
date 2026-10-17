/* SPDX-License-Identifier: GPL-2.0-or-later
 * Owned RTL8153 standard CDC ECM bridge. USB configuration 2 only.
 * No vendor register, EEPROM, flash or forced driver attachment.
 * CDC request/descriptor handling follows Linux drivers/net/usb/cdc_ether.c.
 */
#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <linux/usbdevice_fs.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
static int usb=-1,tap=-1,claim0,claim1,alternate;
static unsigned char mac[6];
static volatile sig_atomic_t stopping;
static atomic_int failed,link_state=-1;
static atomic_uint down_bps,up_bps;
static atomic_ulong received,transmitted,dropped;
static void stop_signal(int s){(void)s;stopping=1;}
static long now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec;}
static unsigned read16(const unsigned char *p){return p[0]|((unsigned)p[1]<<8);}
static unsigned read32(const unsigned char *p){return read16(p)|(read16(p+2)<<16);}
static void notification(const unsigned char *p,unsigned n){
 if(n<8||p[0]!=0xa1||read16(p+4)!=0)return;
 if(p[1]==0&&n==8&&read16(p+6)==0){unsigned value=read16(p+2);if(value<=1)atomic_store(&link_state,value);}
 else if(p[1]==0x2a&&n==16&&read16(p+6)==8){atomic_store(&down_bps,read32(p+8));atomic_store(&up_bps,read32(p+12));}
}
static int control(unsigned type,unsigned req,unsigned value,unsigned index,void *data,unsigned len){
 struct usbdevfs_ctrltransfer c={.bRequestType=type,.bRequest=req,.wValue=value,.wIndex=index,.wLength=len,.timeout=1000,.data=data};
 return ioctl(usb,USBDEVFS_CONTROL,&c);
}
static int attr(const char *dir,const char *name,unsigned *v,int base){
 char path[512];if(snprintf(path,sizeof path,"%s/%s",dir,name)>=(int)sizeof path)return -1;
 FILE *f=fopen(path,"r");if(!f)return -1;int n=fscanf(f,base==16?"%x":"%u",v);fclose(f);return n==1?0:-1;
}
static int prepare(void){
 DIR *dir=opendir("/sys/bus/usb/devices");if(!dir)return -1;struct dirent *e;unsigned count=0;char chosen[512]={0};
 while((e=readdir(dir))){char path[512];unsigned v,p;
  if(e->d_name[0]=='.'||strchr(e->d_name,':'))continue;
  if(snprintf(path,sizeof path,"/sys/bus/usb/devices/%s",e->d_name)>=(int)sizeof path)continue;
  if(!attr(path,"idVendor",&v,16)&&!attr(path,"idProduct",&p,16)&&v==0x0bda&&p==0x8153){count++;snprintf(chosen,sizeof chosen,"%s",path);}
 }
 closedir(dir);if(count!=1){fprintf(stderr,"Expected one RTL8153, got %u\n",count);return -1;}
 unsigned bus,dev,config;if(attr(chosen,"busnum",&bus,10)||attr(chosen,"devnum",&dev,10)||attr(chosen,"bConfigurationValue",&config,10)||config!=2)return -1;
 char path[80];snprintf(path,sizeof path,"/dev/bus/usb/%03u/%03u",bus,dev);usb=open(path,O_RDWR);if(usb<0)return -1;
 unsigned char d[256]={0};int n=control(0x80,6,0x201,0,d,sizeof d);
 /* Exact CDC interface layout read from this owned dock. */
 static const unsigned char expected[]={9,2,80,0,2,2,0,128,90,9,4,0,0,1,2,6,0,5,5,36,0,16,1,5,36,6,0,1,13,36,15,3,0,0,0,0,234,5,0,0,0,7,5,131,3,16,0,8,9,4,1,0,0,10,0,0,0,9,4,1,1,2,10,0,0,4,7,5,129,2,0,2,0,7,5,2,2,0,2,0};
 if(n!=(int)sizeof expected||memcmp(d,expected,sizeof expected)){fputs("Unexpected CDC layout: refusing changes\n",stderr);return -1;}
 n=control(0x80,6,0x303,0x409,d,sizeof d);if(n!=26||d[0]!=26||d[1]!=3)return -1;
 char hex[13];for(int i=0;i<12;i++){if(d[3+i*2])return -1;hex[i]=(char)d[2+i*2];}hex[12]=0;
 for(int i=0;i<6;i++){char b[3]={hex[2*i],hex[2*i+1],0},*end;long v=strtol(b,&end,16);if(*end)return -1;mac[i]=(unsigned char)v;}
 static const unsigned char owned_mac[]={0x00,0xe0,0x4c,0x68,0x0c,0xfb};
 if(memcmp(mac,owned_mac,6))return -1;
 int iface=0;if(ioctl(usb,USBDEVFS_CLAIMINTERFACE,&iface)){perror("claim control");return -1;}claim0=1;
 iface=1;if(ioctl(usb,USBDEVFS_CLAIMINTERFACE,&iface)){perror("claim data");return -1;}claim1=1;
 struct usbdevfs_setinterface set={.interface=1,.altsetting=1};if(ioctl(usb,USBDEVFS_SETINTERFACE,&set))return -1;alternate=1;
 /* Directed, broadcast and multicast reception. */
 if(control(0x21,0x43,0x0e,0,NULL,0)!=0)return -1;
 printf("CDC ECM claimed %s MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",path,mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);return 0;
}
static int make_tap(void){
 if(if_nametoindex("rtdebug0"))return -1;
 int fd=open("/dev/net/tun",O_RDWR|O_NONBLOCK);if(fd<0)return -1;
 struct ifreq r={0};strcpy(r.ifr_name,"rtdebug0");r.ifr_flags=IFF_TAP|IFF_NO_PI|IFF_TUN_EXCL;
 if(ioctl(fd,TUNSETIFF,&r)){close(fd);return -1;}
 int s=socket(AF_INET,SOCK_DGRAM,0);if(s<0){close(fd);return -1;}
 r.ifr_hwaddr.sa_family=ARPHRD_ETHER;memcpy(r.ifr_hwaddr.sa_data,mac,6);
 if(ioctl(s,SIOCSIFHWADDR,&r)||ioctl(s,SIOCGIFFLAGS,&r))goto fail;
 r.ifr_flags|=IFF_UP;if(ioctl(s,SIOCSIFFLAGS,&r))goto fail;
 close(s);return fd;
fail:close(s);close(fd);return -1;
}
static void *receive_usb(void *arg){
 (void)arg;unsigned char buf[2048];
 while(!stopping&&!atomic_load(&failed)){
  struct usbdevfs_bulktransfer b={.ep=0x81,.len=sizeof buf,.timeout=1000,.data=buf};int n=ioctl(usb,USBDEVFS_BULK,&b);
  if(n>=14&&n<=1518){if(atomic_load(&link_state)<0)atomic_store(&link_state,1);if(write(tap,buf,n)==n)atomic_fetch_add(&received,1);else atomic_fetch_add(&dropped,1);}
  else if(n<0&&errno!=ETIMEDOUT&&errno!=EINTR){perror("ECM receive");atomic_store(&failed,1);}
  else if(n>0)atomic_fetch_add(&dropped,1);
 }
 return NULL;
}
static int send_frame(unsigned char *buf,unsigned n){
 if(n<14||n>1518)return -1;
 struct usbdevfs_bulktransfer b={.ep=2,.len=n,.timeout=1000,.data=buf};int result=ioctl(usb,USBDEVFS_BULK,&b);if(result!=(int)n)return -1;
 if(!(n%512)){b.len=0;if(ioctl(usb,USBDEVFS_BULK,&b)!=0)return -1;}
 atomic_fetch_add(&transmitted,1);return 0;
}
static int self_test(void){
 unsigned char up[]={0xa1,0,1,0,0,0,0,0},down[]={0xa1,0,0,0,0,0,0,0};
 notification(up,8);if(atomic_load(&link_state)!=1)return 1;
 notification(down,8);if(atomic_load(&link_state)!=0)return 1;
 for(unsigned n=0;n<8;n++)notification(up,n);
 if(atomic_load(&link_state)!=0)return 1;
 unsigned char speed[]={0xa1,0x2a,0,0,0,0,8,0,0,0xca,0x9a,0x3b,0,0xca,0x9a,0x3b};
 notification(speed,16);if(atomic_load(&down_bps)!=1000000000||atomic_load(&up_bps)!=1000000000)return 1;
 puts("CDC notification tests passed: link, negotiated speed, truncated messages");return 0;
}
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--self-test"))return self_test();
 int service=argc==2&&!strcmp(argv[1],"--service");long duration=0;
 if(!service){if(argc!=3||strcmp(argv[1],"--run"))return 2;char *end;duration=strtol(argv[2],&end,10);if(*end||duration<30||duration>14400)return 2;}
 setvbuf(stdout,NULL,_IOLBF,0);signal(SIGTERM,stop_signal);signal(SIGINT,stop_signal);signal(SIGHUP,SIG_IGN);
 pthread_t rx;int started=0,result=1,note_pending=0;unsigned char note[64];
 struct usbdevfs_urb urb={.type=USBDEVFS_URB_TYPE_INTERRUPT,.endpoint=0x83,.buffer=note,.buffer_length=sizeof note};
 if(prepare())goto finish;
 tap=make_tap();if(tap<0)goto finish;
 if(pthread_create(&rx,NULL,receive_usb,NULL))goto finish;
 started=1;
 if(ioctl(usb,USBDEVFS_SUBMITURB,&urb)){perror("CDC notification submit");goto finish;}note_pending=1;
 puts("READY rtdebug0 (no IPv4 address; old Ethernet unchanged)");long deadline=now()+duration,last=0;
 while(!stopping&&!atomic_load(&failed)&&(service||now()<deadline)){
  struct usbdevfs_urb *done=NULL;
  if(ioctl(usb,USBDEVFS_REAPURBNDELAY,&done)==0){
   note_pending=0;if(done!=&urb||urb.status){atomic_store(&failed,1);break;}
   notification(note,(unsigned)urb.actual_length);urb.actual_length=0;urb.status=0;
   if(ioctl(usb,USBDEVFS_SUBMITURB,&urb)){atomic_store(&failed,1);break;}note_pending=1;
  }else if(errno!=EAGAIN&&errno!=EINTR){atomic_store(&failed,1);break;}
  if(now()!=last){last=now();FILE *f=fopen("/tmp/doveos-ecm-status.next","w");if(f){fprintf(f,"pid=%ld link=%d rx=%lu tx=%lu dropped=%lu down_bps=%u up_bps=%u\n",(long)getpid(),atomic_load(&link_state),atomic_load(&received),atomic_load(&transmitted),atomic_load(&dropped),atomic_load(&down_bps),atomic_load(&up_bps));fclose(f);rename("/tmp/doveos-ecm-status.next","/tmp/doveos-ecm-status");}}
  struct pollfd p={.fd=tap,.events=POLLIN};int n=poll(&p,1,100);if(n<0&&errno!=EINTR){atomic_store(&failed,1);break;}
  if(n>0&&(p.revents&POLLIN)){unsigned char buf[1600];ssize_t len=read(tap,buf,sizeof buf);if(len>0&&send_frame(buf,(unsigned)len)){perror("ECM transmit");atomic_store(&failed,1);break;}}
  if(p.revents&(POLLERR|POLLHUP|POLLNVAL)){atomic_store(&failed,1);break;}
 }
 result=atomic_load(&failed)?1:0;
finish:
 stopping=1;if(note_pending)ioctl(usb,USBDEVFS_DISCARDURB,&urb);if(started)pthread_join(rx,NULL);if(tap>=0)close(tap);
 if(usb>=0){if(claim0)control(0x21,0x43,0,0,NULL,0);if(alternate){struct usbdevfs_setinterface set={.interface=1,.altsetting=0};ioctl(usb,USBDEVFS_SETINTERFACE,&set);}int i=1;if(claim1)ioctl(usb,USBDEVFS_RELEASEINTERFACE,&i);i=0;if(claim0)ioctl(usb,USBDEVFS_RELEASEINTERFACE,&i);close(usb);}
 printf("STOP rx=%lu tx=%lu dropped=%lu interface removed\n",atomic_load(&received),atomic_load(&transmitted),atomic_load(&dropped));return result;
}
