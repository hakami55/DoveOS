/* SPDX-License-Identifier: GPL-2.0-or-later
 * DoveOS Xbox One USB bridge. Protocol based on Linux v4.19 xpad.c
 * (authors and GPL notice in https://github.com/torvalds/linux/blob/v4.19/drivers/input/joystick/xpad.c).
 * No kernel detach, USB reset or firmware update.
 */
#define _GNU_SOURCE
#include <linux/usbdevice_fs.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

static volatile sig_atomic_t stopping;
static void stop(int sig) { (void)sig; stopping=1; }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9; }
static void bounded_log(void) {
 struct stat st;
 if(fstat(STDOUT_FILENO,&st)==0 && S_ISREG(st.st_mode) && st.st_size>65536) {
  if(ftruncate(STDOUT_FILENO,0)==0 && lseek(STDOUT_FILENO,0,SEEK_SET)>=0)puts("LOG_ROTATED");
 }
}
static unsigned number(const char *base, const char *key, int radix) {
 char p[PATH_MAX], b[64]; snprintf(p,sizeof p,"%s/%s",base,key);
 FILE *f=fopen(p,"r"); if(!f) return UINT_MAX;
 if(!fgets(b,sizeof b,f)) { fclose(f); return UINT_MAX; } fclose(f);
 return (unsigned)strtoul(b,NULL,radix);
}
static int device(void) {
 DIR *dir=opendir("/sys/bus/usb/devices"); if(!dir) return -1;
 struct dirent *e; int fd=-1;
 while((e=readdir(dir))) {
  char b[PATH_MAX], p[PATH_MAX]; snprintf(b,sizeof b,"/sys/bus/usb/devices/%s",e->d_name);
  if(number(b,"idVendor",16)!=0x045e || number(b,"idProduct",16)!=0x02dd) continue;
  snprintf(p,sizeof p,"%s:1.0/driver",b);
  if(access(p,F_OK)==0) { fprintf(stderr,"REFUSE existing interface driver\n"); break; }
  snprintf(p,sizeof p,"%s:1.0/ep_81",b);
  if(number(p,"bmAttributes",16)!=3 || number(p,"wMaxPacketSize",16)!=64) break;
  snprintf(p,sizeof p,"%s:1.0/ep_01",b);
  if(number(p,"bmAttributes",16)!=3 || number(p,"wMaxPacketSize",16)!=64) break;
  unsigned bus=number(b,"busnum",10), dev=number(b,"devnum",10);
  if(bus>999 || dev>999) break;
  snprintf(p,sizeof p,"/dev/bus/usb/%03u/%03u",bus,dev);
  fd=open(p,O_RDWR|O_CLOEXEC); if(fd<0) break;
  unsigned char desc[18];
  if(read(fd,desc,sizeof desc)!=18 || desc[8]!=0x5e || desc[9]!=0x04 || desc[10]!=0xdd || desc[11]!=0x02) {
   close(fd);fd=-1;break;
  }
  printf("USB_MATCH %s 045e:02dd\n",p);break;
 }
 closedir(dir); return fd;
}
/* One outstanding URB. Cancellation is reaped before the stack buffer expires. */
static int transfer(int fd, unsigned ep, void *buf, int len, int ms) {
 struct usbdevfs_urb u; memset(&u,0,sizeof u);
 u.type=USBDEVFS_URB_TYPE_INTERRUPT; u.endpoint=ep;u.buffer=buf;u.buffer_length=len;
 if(ioctl(fd,USBDEVFS_SUBMITURB,&u)<0) return -1;
 double until=now()+ms/1000.0; int canceled=0;
 for(;;) {
  void *done=NULL;
  if(ioctl(fd,USBDEVFS_REAPURBNDELAY,&done)==0) {
   if(done!=&u) { errno=EPROTO;return -1; }
   if(canceled) { errno=ETIMEDOUT;return 0; }
   if(u.status<0) {errno=-u.status;return -1;} return u.actual_length;
  }
  if(errno!=EAGAIN && errno!=EINTR) return -1;
  if((stopping || now()>=until) && !canceled) {
   if(ioctl(fd,USBDEVFS_DISCARDURB,&u)<0 && errno!=EINVAL) return -1;
   canceled=1;
  }
  struct pollfd pollfd={fd,POLLOUT,0};
  if(poll(&pollfd,1,canceled?10:50)<0 && errno!=EINTR) return -1;
 }
}
static const int keys[]={BTN_A,BTN_B,BTN_X,BTN_Y,BTN_START,BTN_SELECT,BTN_TL,BTN_TR,BTN_THUMBL,BTN_THUMBR,BTN_MODE};
static const int axes[]={ABS_X,ABS_Y,ABS_RX,ABS_RY,ABS_Z,ABS_RZ,ABS_HAT0X,ABS_HAT0Y};
static int input_device(void) {
 int fd=open("/dev/uinput",O_RDWR|O_NONBLOCK|O_CLOEXEC); if(fd<0)return -1;
 struct uinput_user_dev u; memset(&u,0,sizeof u);
 snprintf(u.name,sizeof u.name,"Microsoft Xbox One USB (DoveOS)");
 u.ff_effects_max=16; u.id.bustype=BUS_USB;u.id.vendor=0x045e;u.id.product=0x02dd;u.id.version=0x0203;
 if(ioctl(fd,UI_SET_EVBIT,EV_KEY)<0 || ioctl(fd,UI_SET_EVBIT,EV_ABS)<0 || ioctl(fd,UI_SET_EVBIT,EV_FF)<0 || ioctl(fd,UI_SET_FFBIT,FF_RUMBLE)<0 || ioctl(fd,UI_SET_FFBIT,FF_GAIN)<0)goto fail;
 for(unsigned i=0;i<sizeof keys/sizeof keys[0];i++) if(ioctl(fd,UI_SET_KEYBIT,keys[i])<0)goto fail;
 for(unsigned i=0;i<sizeof axes/sizeof axes[0];i++) {
  int a=axes[i]; if(ioctl(fd,UI_SET_ABSBIT,a)<0)goto fail;
  u.absmin[a]=i<4?-32768:i<6?0:-1;
  u.absmax[a]=i<4?32767:i<6?1023:1;
  u.absfuzz[a]=i<4?255:i<6?3:0;u.absflat[a]=i<4?4095:0;
 }
 if(write(fd,&u,sizeof u)!=(ssize_t)sizeof u || ioctl(fd,UI_DEV_CREATE)<0)goto fail;
 return fd;
fail: close(fd);return -1;
}
static int emit(int fd,unsigned type,unsigned code,int value) {
 struct input_event e;memset(&e,0,sizeof e);e.type=type;e.code=code;e.value=value;
 return write(fd,&e,sizeof e)==(ssize_t)sizeof e?0:-1;
}
static uint16_t le(const unsigned char *p) { return (uint16_t)(p[0]|p[1]<<8); }

typedef struct {
 struct ff_effect effects[16];
 int active, dirty;
 unsigned gain, strong, weak;
 double begins, ends;
} Rumble;
static void rumble_start(Rumble *r,unsigned strong,unsigned weak,unsigned delay,unsigned duration) {
 r->strong=strong;r->weak=weak;r->begins=now()+(delay>5000?5000:delay)/1000.0;
 r->ends=r->begins+(duration>5000?5000:duration?duration:5000)/1000.0;
 r->dirty=1;
}
static int ff_events(int fd,Rumble *r) {
 static double log_at;
 struct input_event e;
 while(read(fd,&e,sizeof e)==(ssize_t)sizeof e) {
  if(e.type==EV_UINPUT && e.code==UI_FF_UPLOAD) {
   struct uinput_ff_upload q;memset(&q,0,sizeof q);q.request_id=e.value;
   if(ioctl(fd,UI_BEGIN_FF_UPLOAD,&q)<0)return -1;
   q.retval=-EINVAL;
   if(q.effect.type==FF_RUMBLE && q.effect.id>=0 && q.effect.id<16) {
    r->effects[q.effect.id]=q.effect;q.retval=0;
    if(now()>=log_at){printf("FF_UPLOAD id=%d strong=%u weak=%u length=%u\n",q.effect.id,q.effect.u.rumble.strong_magnitude,q.effect.u.rumble.weak_magnitude,q.effect.replay.length);log_at=now()+1;}
   }
   if(ioctl(fd,UI_END_FF_UPLOAD,&q)<0)return -1;
  } else if(e.type==EV_UINPUT && e.code==UI_FF_ERASE) {
   struct uinput_ff_erase q;memset(&q,0,sizeof q);q.request_id=e.value;
   if(ioctl(fd,UI_BEGIN_FF_ERASE,&q)<0)return -1;
   q.retval=q.effect_id<16?0:-EINVAL;
   if(q.retval==0) {
    memset(&r->effects[q.effect_id],0,sizeof r->effects[0]);
    if(r->active==(int)q.effect_id) {r->strong=r->weak=0;r->dirty=1;r->active=-1;}
   }
   if(ioctl(fd,UI_END_FF_ERASE,&q)<0)return -1;
  } else if(e.type==EV_FF && e.code==FF_GAIN) {
   r->gain=e.value<0?0:e.value>65535?65535:(unsigned)e.value;r->dirty=1;
  } else if(e.type==EV_FF && e.code<16) {
   struct ff_effect *effect=&r->effects[e.code];
   if(e.value>0 && effect->type==FF_RUMBLE) {
    r->active=e.code;rumble_start(r,effect->u.rumble.strong_magnitude,effect->u.rumble.weak_magnitude,effect->replay.delay,effect->replay.length);
   } else if(r->active==e.code) {r->strong=r->weak=0;r->dirty=1;r->active=-1;}
  }
 }
 return errno==EAGAIN || errno==EINTR?0:-1;
}
static const char *rumble_path="/tmp/doveos-xbox-usb-rumble.sock";
static int rumble_socket(void) {
 struct stat st;
 if(lstat(rumble_path,&st)==0) {
  if(!S_ISSOCK(st.st_mode) || st.st_uid!=getuid()) {errno=EEXIST;return -1;}
  if(unlink(rumble_path)<0)return -1;
 }
 int fd=socket(AF_UNIX,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)return -1;
 struct sockaddr_un a;memset(&a,0,sizeof a);a.sun_family=AF_UNIX;snprintf(a.sun_path,sizeof a.sun_path,"%s",rumble_path);
 if(bind(fd,(struct sockaddr*)&a,sizeof a)<0 || chmod(rumble_path,0600)<0){close(fd);return -1;}return fd;
}
static void socket_events(int fd,Rumble *r) {
 unsigned char b[10];ssize_t n;
 while((n=recv(fd,b,sizeof b,MSG_TRUNC))>=0) {
  if(n!=10 || b[0]!='X' || b[1]!='R' || b[2]!=1 || b[3]!=0 || b[4]!=0 || b[5]!=0)continue;
  r->active=-1;rumble_start(r,le(b+6),le(b+8),0,5000);
 }
}
static void urb_setup(struct usbdevfs_urb *u,unsigned ep,void *data,int bytes) {
 memset(u,0,sizeof *u);u->type=USBDEVFS_URB_TYPE_INTERRUPT;u->endpoint=ep;u->buffer=data;u->buffer_length=bytes;
}
static int decode_input(int u,const unsigned char *b,int n,unsigned *seen) {
 if(n<18 || b[0]!=0x20 || b[3]<14 || le(b+6)>1023 || le(b+8)>1023)return 0;
 int k[]={!!(b[4]&16),!!(b[4]&32),!!(b[4]&64),!!(b[4]&128),!!(b[4]&4),!!(b[4]&8),!!(b[5]&16),!!(b[5]&32),!!(b[5]&64),!!(b[5]&128)};
 int a[]={(int16_t)le(b+10),~(int16_t)le(b+12),(int16_t)le(b+14),~(int16_t)le(b+16),le(b+6),le(b+8),!!(b[5]&8)-!!(b[5]&4),!!(b[5]&2)-!!(b[5]&1)};
 for(int i=0;i<10;i++){if(k[i])*seen|=1u<<i;if(emit(u,EV_KEY,keys[i],k[i])<0)return -1;}
 for(int i=0;i<8;i++)if(emit(u,EV_ABS,axes[i],a[i])<0)return -1;
 if(emit(u,EV_SYN,SYN_REPORT,0)<0)return -1;
 return 1;
}
static int session(int usb,int seconds) {
 unsigned interface=0;int rc=-1,u=-1,sock=-1,out_live=0,ack_pending=0,ack_seq=0;
 unsigned char in_data[64],out_data[13],seq=1;struct usbdevfs_urb in,out;
 unsigned long reports=0,rejected=0,errors=0;unsigned seen=0,consecutive=0;
 Rumble r;memset(&r,0,sizeof r);r.gain=65535;r.active=-1;
 if(ioctl(usb,USBDEVFS_CLAIMINTERFACE,&interface)<0){perror("claim");close(usb);return -1;}
 unsigned char init[]={5,0x20,0,1,0};
 if(transfer(usb,1,init,sizeof init,2000)!=5){perror("initialize");goto cleanup;}
 u=input_device();if(u<0){perror("uinput");goto cleanup;}
 sock=rumble_socket();if(sock<0){perror("rumble socket");goto cleanup;}
 urb_setup(&in,0x81,in_data,sizeof in_data);
 if(ioctl(usb,USBDEVFS_SUBMITURB,&in)<0)goto cleanup;
 printf("XBOX_USB_READY persistent=%d rumble=1\n",seconds==0);
 double end=seconds?now()+seconds:0,report=now()+30,out_until=0,refresh_rumble=0,next_send=0,rumble_log_at=0,shutdown_check=0;
 while(!stopping && (!end || now()<end)) {
  if(now()>=shutdown_check){if(access("/tmp/doveos-shutting-down",F_OK)==0){stopping=1;break;}shutdown_check=now()+1;}
  struct pollfd pf[]={{usb,POLLOUT,0},{u,POLLIN,0},{sock,POLLIN,0}};
  int result=poll(pf,3,50);if(result<0 && errno!=EINTR)goto cleanup;
  if(pf[1].revents&POLLIN)if(ff_events(u,&r)<0)goto cleanup;
  if(pf[2].revents&POLLIN)socket_events(sock,&r);
  if(pf[0].revents&(POLLHUP|POLLERR|POLLNVAL)){puts("USB_DISCONNECTED");goto cleanup;}
  void *done;
  while(ioctl(usb,USBDEVFS_REAPURBNDELAY,&done)==0) {
   struct usbdevfs_urb *v=done;
   if(v==&out) {
    out_live=0;next_send=now()+0.01;
    if(out.status<0){errno=-out.status;perror("USB output");goto cleanup;}
   } else if(v==&in) {
    if(in.status<0) {
     int error=-in.status;
     if((error!=EPROTO && error!=EILSEQ && error!=EOVERFLOW) || ++consecutive>5) {errno=error;perror("USB input");goto cleanup;}
     errors++;printf("USB_RETRY error=%d count=%lu\n",error,errors);usleep(20000);
    } else {
     consecutive=0;int n=in.actual_length;
     if(n>=5 && in_data[0]==7) {
      if(in_data[1]==0x30){ack_pending=1;ack_seq=in_data[2];}
      if(in_data[4]&1)seen|=1u<<10;
      if(emit(u,EV_KEY,BTN_MODE,in_data[4]&1)<0 || emit(u,EV_SYN,SYN_REPORT,0)<0)goto cleanup;
     } else if(n && in_data[0]==0x20) {
      int decoded=decode_input(u,in_data,n,&seen);if(decoded<0)goto cleanup;
      if(decoded)reports++;else rejected++;
     }
    }
    if(ioctl(usb,USBDEVFS_SUBMITURB,&in)<0){perror("resubmit");goto cleanup;}
   } else {errno=EPROTO;perror("unexpected completion");goto cleanup;}
  }
  if(errno!=EAGAIN && errno!=EINTR){perror("reap");goto cleanup;}
  if(out_live && now()>out_until){errno=ETIMEDOUT;perror("output deadline");goto cleanup;}
  if(r.ends && now()>=r.ends){r.strong=r.weak=0;r.ends=0;r.dirty=1;}
  if(r.ends && now()>=refresh_rumble && now()>=r.begins)r.dirty=1;
  if(!out_live && now()>=next_send && (ack_pending || (r.dirty && now()>=r.begins))) {
   if(ack_pending) {
    unsigned char ack[]={1,0x20,0,9,0,7,0x20,2,0,0,0,0,0};memcpy(out_data,ack,13);out_data[2]=ack_seq;ack_pending=0;
   } else {
    unsigned strong=(uint64_t)r.strong*r.gain/65535/512,weak=(uint64_t)r.weak*r.gain/65535/512;
    unsigned char data[]={9,0,0,9,0,15,0,0,0,0,0,0,0};memcpy(out_data,data,13);
    out_data[2]=seq++;out_data[8]=strong>100?100:strong;out_data[9]=weak>100?100:weak;r.dirty=0;
    double left=r.ends-now();unsigned ticks=left>0?(unsigned)(left*100):0;
    if((strong || weak) && !ticks)ticks=1;
    out_data[10]=ticks>255?255:ticks;refresh_rumble=now()+2;
    if(now()>=rumble_log_at || (!strong && !weak)){printf("RUMBLE strong=%u weak=%u\n",out_data[8],out_data[9]);rumble_log_at=now()+1;}
   }
   urb_setup(&out,1,out_data,13);
   if(ioctl(usb,USBDEVFS_SUBMITURB,&out)<0){perror("submit output");goto cleanup;}
   out_live=1;out_until=now()+1;
  }
  if(now()>=report){bounded_log();printf("INPUT_REPORTS total=%lu buttons=%03x rejected=%lu errors=%lu\n",reports,seen,rejected,errors);report=now()+30;}
 }
 rc=0;
cleanup:
 /* Closing usbfs synchronously cancels pending URBs before stack lifetime ends.
  * Every rumble command has a finite <=2.55 second period, no infinite repeats.
  */
 close(usb);
 if(r.strong || r.weak) {
  int reset=device();
  if(reset>=0) {
   if(ioctl(reset,USBDEVFS_CLAIMINTERFACE,&interface)==0) {
    unsigned char off[]={9,0,0,9,0,15,0,0,0,0,0,0,0};off[2]=seq++;
    sig_atomic_t saved=stopping;stopping=0;
    (void)transfer(reset,1,off,sizeof off,250);stopping=saved || stopping;
   }
   close(reset);
  }
 }
 if(u>=0){ioctl(u,UI_DEV_DESTROY);close(u);}
 if(sock>=0){close(sock);unlink(rumble_path);}
 printf("SESSION_END reports=%lu buttons=%03x rejected=%lu errors=%lu\n",reports,seen,rejected,errors);
 bounded_log();
 return rc;
}
int main(int argc,char **argv) {
 int daemon=argc==2 && !strcmp(argv[1],"--daemon");
 int seconds=daemon?0:argc==2?atoi(argv[1]):900;
 if(!daemon && (seconds<1 || seconds>1800))return 2;
 setvbuf(stdout,NULL,_IOLBF,0);signal(SIGINT,stop);signal(SIGTERM,stop);umask(077);
 int lock=open("/tmp/doveos-xbox-usb-trial.lock",O_CREAT|O_RDWR|O_CLOEXEC,0600);
 if(lock<0 || flock(lock,LOCK_EX|LOCK_NB)<0){perror("bridge already running");return 1;}
 int result=0;
 do {
  if(access("/tmp/doveos-shutting-down",F_OK)==0)break;
  int usb=device();
  if(usb>=0)result=session(usb,seconds);
  else if(!daemon){perror("matching controller");result=-1;}
  if(daemon && !stopping)sleep(2);
 } while(daemon && !stopping);
 close(lock);return result<0?1:0;
}
