/* Fixed-scope ITE service: reads plus audited PWM and native wake control.
 * No reset, boot, flash, or direct GPIO-write commands. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>

static int usbfd = -1;
static unsigned char recipient = 1;
static volatile sig_atomic_t stopping = 0;
static void stop_recording(int signo) { (void)signo; stopping = 1; }
static double now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1000000000.0;
}
static void pause_ms(unsigned n) {
    struct timespec t = {n / 1000, (long)(n % 1000) * 1000000};
    while (nanosleep(&t, &t) < 0 && errno == EINTR) {}
}
static int exchange(const unsigned char *req, unsigned size, unsigned char *reply) {
    unsigned char tx[64] = {0}; memcpy(tx, req, size);
    struct usbdevfs_ctrltransfer q = {.bRequestType=(unsigned char)(0x20|recipient), .bRequest=9,
        .wValue=0x35a, .wIndex=0, .wLength=size, .timeout=1000, .data=tx};
    int n = ioctl(usbfd, USBDEVFS_CONTROL, &q);
    if (n != (int)size) { fprintf(stderr,"SET_SELECTOR failed %d errno=%d\n",n,errno); return -1; }
    pause_ms(25);
    memset(reply,0,64); reply[0]=0x5a;
    q.bRequestType=(unsigned char)(0xa0|recipient); q.bRequest=1; q.wLength=size; q.data=reply;
    n=ioctl(usbfd,USBDEVFS_CONTROL,&q);
    if(n<16 || reply[0]!=0x5a) {
        fprintf(stderr,"GET_REPLY invalid n=%d errno=%d first=%02x\n",n,errno,reply[0]);
        return -1;
    }
    return n;
}
static int permitted(unsigned addr, unsigned n) {
    if (!n || n>10) return 0;
    const unsigned bounds[][2] = {
        {0x3100,0x3110},       /* Immutable version label. */
        {0x8e9a,0x8ea4},       /* Immutable fingerprint from traced function. */
        {0xbf50,0xbf5a},       /* Stock mode33->30 native wake transition. */
        {0x800e0,0x800e2},     /* Active downstream address and probe selector. */
        {0x81000,0x8100c},     /* Cached descriptor length and identity. Main GP=0x8101c. */
        {0x8152c,0x81581},     /* Five 17-byte touch contact slots. */
        {0x81850,0x81860},     /* Incoming 16-byte I2C touch report buffer. */
        {0x806b2,0x806b9},     /* Touch/button mode/status only. */
        {0x80514,0x80524},     /* Audited T0053 cached Smart Battery words. */
        {0x80601,0x80603},     /* Charger flag and exact controller power mode. */
        {0xe3ba,0xe3fc},       /* Audited backlight function, immutable fingerprint. */
        {0x80628,0x8062b},     /* Backlight cached levels/profile; reads only. */
        {0xf01802,0xf01803},   /* Verified PWM DCR0 brightness control. */
        {0xf01664,0xf01665}    /* GPIO D physical input mirror: lid bit1, T0053. */
    };
    for (unsigned i=0;i<sizeof bounds/sizeof bounds[0];i++)
        if(addr>=bounds[i][0] && addr+n<=bounds[i][1]) return 1;
    return 0;
}
static int read_fixed(unsigned addr, unsigned n, unsigned char *out) {
    if (!permitted(addr,n)) return -1;
    unsigned char req[64]={0x5a,5},reply[64];
    req[2]=(unsigned char)addr; req[3]=(unsigned char)(addr>>8);
    req[4]=(unsigned char)(addr>>16); req[5]=(unsigned char)n;
    if(exchange(req,17,reply)<0 || memcmp(req+2,reply+2,4)) {
        fprintf(stderr,"READ framing mismatch addr=%06x\n",addr); return -1;
    }
    memcpy(out,reply+6,n); return 0;
}
static int dump(const char *name, unsigned addr, unsigned size) {
    unsigned char b[85];
    if(size>sizeof b) return -1;
    for(unsigned k=0;k<size;k+=10) {
        unsigned n=size-k; if(n>10)n=10;
        if(read_fixed(addr+k,n,b+k))return -1;
    }
    printf("%s addr=%06x data=",name,addr);
    for(unsigned k=0;k<size;k++)printf("%02x",b[k]);
    puts(""); return 0;
}
static void battery_sample(void) {
    unsigned char b[16],power;
    if(read_fixed(0x80514,10,b)||read_fixed(0x8051e,6,b+10)||read_fixed(0x80601,1,&power))return;
    unsigned pct=b[4]|((unsigned)b[5]<<8),status=b[14]|((unsigned)b[15]<<8);
    if(pct>100 || !(status&0x80))return;
    unsigned voltage=b[0]|((unsigned)b[1]<<8);
    int current=(int16_t)(b[2]|((unsigned)b[3]<<8));
    int fd=open("/tmp/doveos-battery-state.new",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)return;
    FILE *f=fdopen(fd,"w");if(!f){close(fd);return;}
    int ok=fprintf(f,"DOVE_BATTERY1 %.3f %u %u %d %u %u\n",now(),pct,voltage,current,status,(power&0x10)!=0)>0;
    if(fclose(f))ok=0;
    if(ok)rename("/tmp/doveos-battery-state.new","/tmp/doveos-battery-state");
}
#include "backlight_control.h"
static void lid_wake_request(void) {
    FILE *f=fopen("/tmp/doveos-lid-wake-request","r");if(!f)return;
    double requested=0;int n=fscanf(f,"%lf",&requested);fclose(f);
    unlink("/tmp/doveos-lid-wake-request");
    double age=now()-requested;
    if(n!=1||age<0||age>2)return;
    unsigned char code[10],mode,raw;
    const unsigned char expected[10]={0x5a,0x18,0x33,0x1a,0x44,0x10,0x00,0x30,0xae,0x40};
    if(read_fixed(0xbf50,10,code)||memcmp(code,expected,10)||read_fixed(0x80602,1,&mode)||read_fixed(0xf01664,1,&raw)||!(raw&2)){
        puts("LID_NATIVE_WAKE_REJECTED identity_or_sensor");return;
    }
    if(mode==0x10){puts("LID_NATIVE_ALREADY_AWAKE");return;}
    if(mode!=0x33){printf("LID_NATIVE_WAKE_REJECTED mode=%02x\n",mode);return;}
    /* Only the stock controller's audited 33->30 wake transition, bf54.
       The stock state machine sequences the display rails. No GPIO write,
       reset, flash, boot command or forced final state is issued here. */
    unsigned char req[17]={0x5a,6,2,6,8,1,0x30};
    struct usbdevfs_ctrltransfer q={.bRequestType=0x20,.bRequest=9,.wValue=0x35a,.wLength=17,.timeout=1000,.data=req};
    int sent=ioctl(usbfd,USBDEVFS_CONTROL,&q);
    printf("LID_NATIVE_WAKE %.6f mode33_to30 transferred=%d\n",now(),sent);
}
static void lid_sample(void) {
    static double next=0,candidate_since=0;
    static int candidate=-1,stable=-1;
    if(now()<next)return;
    next=now()+0.25;
    unsigned char raw;
    if(read_fixed(0xf01664,1,&raw))return;
    int closed=(raw&2)==0;
    if(closed!=candidate){candidate=closed;candidate_since=now();}
    if(now()-candidate_since>=0.2)stable=candidate;
    if(stable<0)return;
    FILE *f=fopen("/tmp/doveos-lid-state.new","w");
    if(!f)return;
    int good=fprintf(f,"DOVE_LID1 %.6f %d %u\n",now(),stable,(unsigned)raw)>0;
    if(fclose(f))good=0;
    if(good)rename("/tmp/doveos-lid-state.new","/tmp/doveos-lid-state");
}
int main(int argc, char **argv) {
    struct sigaction action = {0};
    action.sa_handler = stop_recording;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    sigset_t stop_signals;
    sigemptyset(&stop_signals);
    sigaddset(&stop_signals,SIGTERM);
    sigaddset(&stop_signals,SIGINT);
    sigprocmask(SIG_UNBLOCK,&stop_signals,NULL);
    int record=argc==2 && !strcmp(argv[1],"record");
    int device_probe=argc==2 && !strcmp(argv[1],"probe-device");
    int trial=argc==2 && !strcmp(argv[1],"trial");
    int service=argc==2 && !strcmp(argv[1],"service");
    if(argc>2 || (argc==2 && !record && !device_probe && !trial && !service))return 2;
    if(device_probe || trial || service)recipient=0;
    setvbuf(stdout,NULL,_IOLBF,0);
    unsigned bus=0,dev=0; char path[100];
    FILE *f=fopen("/sys/bus/usb/devices/1-1.3/busnum","r");
    if(!f || fscanf(f,"%u",&bus)!=1)return 3;
    fclose(f); f=fopen("/sys/bus/usb/devices/1-1.3/devnum","r");
    if(!f || fscanf(f,"%u",&dev)!=1)return 3;
    fclose(f); snprintf(path,sizeof path,"/dev/bus/usb/%03u/%03u",bus,dev);
    usbfd=open(path,O_RDWR|O_CLOEXEC);
    if(usbfd<0){perror("usb open");return 3;}
    unsigned char req[64]={0x5a},reply[64],check[16];
    memcpy(req+1,"ITE Tech. Inc.",15);
    int enabled=0;
    for(unsigned attempt=0;attempt<2;attempt++) {
        if(exchange(req,64,reply)<0)break;
        if(!memcmp(req+1,reply+1,15)){enabled=1;break;}
    }
    if(!enabled){puts("ENABLE_NOT_VERIFIED_STOP");return 4;}
    if(read_fixed(0x3100,10,check) || read_fixed(0x310a,6,check+10)
       || memcmp(check,"Fih_Dove: T0053\0",16)) {
        puts("VERSION_NOT_VERIFIED_STOP");return 5;
    }
    const unsigned char fingerprint[10]={0x00,0x11,0x80,0x08,0xa6,0x9f,0x40,0x10,0xa0,0x08};
    if(read_fixed(0x8e9a,10,check) || memcmp(check,fingerprint,10)) {
        puts("CODE_NOT_VERIFIED_STOP");return 5;
    }
    puts("T0053_VERSION_AND_CLICK_CODE_VERIFIED");
    if(dump("DOWNSTREAM",0x800e0,2) || dump("CACHED_HID",0x81000,12)
       || dump("INPUT_BUFFER",0x81850,16) || dump("CONTACT_SLOTS",0x8152c,85)
       || dump("MODE_STATUS",0x806b2,7))return 6;
    if(trial || service) {
        if(service)backlight_init();
        int sock=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);
        if(sock<0)return 7;
        struct sockaddr_in target={.sin_family=AF_INET,.sin_port=htons(18829),
                                   .sin_addr={.s_addr=htonl(INADDR_LOOPBACK)}};
        unsigned seq=0,old_scan=0; double fresh=now(),end=service?0:now()+600.0;
        int previous=-1,failed=0;double next_battery=0;
        puts(service?"CLICKPAD_RAM_SERVICE_READY desktop_lifetime=1":"CLICKPAD_RAM_TRIAL_READY maximum_seconds=600");
        while(!stopping && (!end || now()<end)) {
            unsigned char b[10],packet[12]={0x43,0x50,1,0};
            if(read_fixed(0x81853,10,b)){failed=1;break;}
            unsigned scan=b[5]|((unsigned)b[6]<<8);
            if(scan!=old_scan){fresh=now();old_scan=scan;}
            unsigned x=b[1]|((unsigned)b[2]<<8),y=b[3]|((unsigned)b[4]<<8);
            int pressed=(b[8]&1) && (b[0]&2) && b[7] && now()-fresh<0.5;
            packet[3]=(unsigned char)pressed;
            for(unsigned i=0;i<4;i++)packet[4+i]=(unsigned char)(seq>>(i*8));
            packet[8]=(unsigned char)x;packet[9]=(unsigned char)(x>>8);
            packet[10]=(unsigned char)y;packet[11]=(unsigned char)(y>>8);
            if(sendto(sock,packet,sizeof packet,0,(struct sockaddr*)&target,sizeof target)!=(ssize_t)sizeof packet){failed=1;break;}
            if(previous!=pressed){if(!service)printf("RAW_SWITCH state=%d x=%u y=%u seq=%u\n",pressed,x,y,seq);previous=pressed;}
            seq++;pause_ms(5);
            if(service){int refresh=now()>=next_battery;if(refresh){battery_sample();next_battery=now()+5.0;}backlight_tick(refresh);lid_sample();lid_wake_request();}
        }
        if(service){backlight_restore();unlink("/tmp/doveos-battery-state");unlink("/tmp/doveos-lid-state");}
        close(sock);close(usbfd);
        printf("CLICKPAD_RAM_TRIAL_FINISHED samples=%u failed=%d\n",seq,failed);
        return failed?7:0;
    }
    if(record) {
        double end=now()+120.0;
        puts("RAW_SWITCH_RECORDER_READY maximum_seconds=120");
        while(!stopping && now()<end) {
            unsigned char incoming[10],slot[10];
            if(read_fixed(0x81853,10,incoming) || read_fixed(0x81530,10,slot))return 6;
            printf("SAMPLE t=%.6f incoming=",now());
            for(unsigned k=0;k<10;k++)printf("%02x",incoming[k]);
            printf(" slot=");for(unsigned k=0;k<10;k++)printf("%02x",slot[k]);
            puts("");pause_ms(10);
        }
        puts("RAW_SWITCH_RECORDER_FINISHED");
    }
    close(usbfd);return 0;
}
