/* SPDX-License-Identifier: GPL-2.0-or-later
 * Root-side, local-only broker for the stock Realtek OMX decoder.
 * Each connection gets a separate process; no firmware is written.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <signal.h>
#include "vlc_rtk_protocol.h"
static int ended,hw_width,hw_height;
static uint64_t LiGetMicroseconds(void);
#include "vlc_rtk_omx.h"
static int client_fd=-1;
static unsigned display_width,display_height;
static uint64_t LiGetMicroseconds(void){return (uint64_t)(seconds()*1000000.0);}
static int full_io(int fd,void *p,size_t n,int sending){
    while(n){ssize_t k=sending?send(fd,p,n,MSG_NOSIGNAL):recv(fd,p,n,0);
        if(k<0 && errno==EINTR)continue;
        if(k<=0)return -1;
        p=(char*)p+k;n-=(size_t)k;
    }return 0;
}
static void *output_loop(void *unused){
    (void)unused;unsigned frames=0;
    size_t input_bytes=(size_t)hw_width*hw_height*3/2;
    size_t bytes=(size_t)display_width*display_height*3/2;
    uint8_t *pixels=malloc(bytes);if(!pixels){hw_failed=1;return NULL;}
    unsigned xmap[1920],uvmap[960];
    for(unsigned x=0;x<display_width;x++)xmap[x]=(uint64_t)x*hw_width/display_width;
    for(unsigned x=0;x<display_width/2;x++)uvmap[x]=((uint64_t)x*hw_width/display_width)*2;
    int64_t last_pts=-1;unsigned skipped=0;
    while(!output_stop && !hw_failed){
        if(port_change && !configured){
            __atomic_store_n(&ready_read,__atomic_load_n(&ready_write,__ATOMIC_ACQUIRE),__ATOMIC_RELEASE);
            if(resize_output()){hw_failed=1;break;}
        }
        OMX_BUFFERHEADERTYPE *b=pop_output();
        if(!b){delay_ms(1);continue;}
        if(!configured){b->nFilledLen=0;component->FillThisBuffer(component,b);continue;}
        if(last_pts>=0 && b->nTimeStamp>last_pts && b->nTimeStamp-last_pts<32000){
            b->nFilledLen=0;b->nOffset=0;b->nFlags=0;
            if(component->FillThisBuffer(component,b)!=OMX_ErrorNone){hw_failed=1;break;}
            skipped++;continue;
        }
        if(b->nOffset+output_stride*output_slice*3/2>b->nAllocLen){hw_failed=1;break;}
        const uint8_t *data=b->pBuffer+b->nOffset;
        /* OMX reports allocation padding in nSliceHeight, but returns cropped,
         * tightly packed NV12 in nFilledLen. Use the actual returned layout. */
        int chroma_start=b->nFilledLen==input_bytes?hw_height:output_slice;
        if(!frames)printf("RTK_VLC_BUFFER filled=%u allocated=%u stride=%d slice=%d chroma_start=%d\n",(unsigned)b->nFilledLen,(unsigned)b->nAllocLen,output_stride,output_slice,chroma_start);
        if(display_width==(unsigned)hw_width && display_height==(unsigned)hw_height){
            for(int y=0;y<hw_height;y++)memcpy(pixels+y*hw_width,data+y*output_stride,hw_width);
            for(int y=0;y<hw_height/2;y++)memcpy(pixels+hw_width*hw_height+y*hw_width,data+output_stride*chroma_start+y*output_stride,hw_width);
        }else{
            for(unsigned y=0;y<display_height;y++){
                const uint8_t *src=data+((uint64_t)y*hw_height/display_height)*output_stride;
                uint8_t *dst=pixels+y*display_width;
                for(unsigned x=0;x<display_width;x++)dst[x]=src[xmap[x]];
            }
            for(unsigned y=0;y<display_height/2;y++){
                const uint8_t *src=data+output_stride*chroma_start+((uint64_t)y*hw_height/display_height)*output_stride;
                uint8_t *dst=pixels+display_width*display_height+y*display_width;
                for(unsigned x=0;x<display_width/2;x++){dst[2*x]=src[uvmap[x]];dst[2*x+1]=src[uvmap[x]+1];}
            }
        }
        rtk_msg m={.magic=RTK_MAGIC,.type=RTK_FRAME,.size=bytes,.width=display_width,.height=display_height,.pts=b->nTimeStamp};
        last_pts=b->nTimeStamp;
        b->nFilledLen=0;b->nOffset=0;b->nFlags=0;
        if(component->FillThisBuffer(component,b)!=OMX_ErrorNone){hw_failed=1;break;}
        if(full_io(client_fd,&m,sizeof m,1) || full_io(client_fd,pixels,bytes,1)){hw_failed=1;break;}
        if(++frames==1 || frames%120==0)printf("RTK_VLC_HARDWARE_OUTPUT count=%u pts=%lld\n",frames,(long long)m.pts);
    }
    printf("RTK_VLC_DISPLAY_END frames=%u skipped_for_30fps=%u\n",frames,skipped);
    free(pixels);shutdown(client_fd,SHUT_RDWR);return NULL;
}
static int submit_packet(const uint8_t *data,size_t size,int64_t pts,int eos){
    if(size>in[0]->nAllocLen)return -1;
    if(size)memcpy(in[0]->pBuffer,data,size);
    in[0]->nOffset=0;in[0]->nFilledLen=size;in[0]->nTimeStamp=pts;
    in[0]->nFlags=OMX_BUFFERFLAG_ENDOFFRAME|(eos?OMX_BUFFERFLAG_EOS:0);
    if(component->EmptyThisBuffer(component,in[0])!=OMX_ErrorNone)return -1;
    double limit=seconds()+3;
    while(seconds()<limit && !hw_failed){
        struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_nsec+=2000000;
        if(until.tv_nsec>=1000000000){until.tv_sec++;until.tv_nsec-=1000000000;}
        if(!sem_timedwait(&input_done,&until))return 0;
        if(errno!=ETIMEDOUT && errno!=EINTR)return -1;
    }return -1;
}
static void close_omx(void){
    if(!component)return;
    component->SendCommand(component,OMX_CommandStateSet,OMX_StateIdle,NULL);
    if(wait_state(OMX_StateIdle))return;
    component->SendCommand(component,OMX_CommandStateSet,OMX_StateLoaded,NULL);
    for(unsigned p=0;p<2;p++)for(unsigned i=0;i<ports[p].nBufferCountActual;i++){
        OMX_BUFFERHEADERTYPE *b=p?out[i]:in[i];if(b)component->FreeBuffer(component,p,b);
    }
    if(!wait_state(OMX_StateLoaded))component->ComponentDeInit(component);
    /* The vendor OMX_Deinit crashes. Exiting this child releases its mappings. */
}
static void session(int fd){
    alarm(15);client_fd=fd;
    rtk_msg m;
    if(full_io(fd,&m,sizeof m,0) || m.magic!=RTK_MAGIC || m.type!=RTK_INIT ||
       !m.width || !m.height || m.width>1920 || m.height>1088 || (m.width&1) || (m.height&1))return;
    hw_width=m.width;hw_height=m.height;
    display_width=m.flags>>16;display_height=m.flags&65535;
    if(!display_width || !display_height || display_width>1280 || display_height>720 || display_width>(unsigned)hw_width || display_height>(unsigned)hw_height || (display_width&1) || (display_height&1))return;
    if(omx_backend_init())return;
    m.type=RTK_ACK;
    if(full_io(fd,&m,sizeof m,1))return;
    alarm(0);
    if(pthread_create(&output_thread,NULL,output_loop,NULL))return;
    uint8_t *packet=malloc(RTK_LIMIT);if(!packet){hw_failed=1;shutdown(fd,SHUT_RDWR);pthread_join(output_thread,NULL);return;}
    while(!hw_failed && !full_io(fd,&m,sizeof m,0)){
        if(m.magic!=RTK_MAGIC || m.size>RTK_LIMIT || (m.type!=RTK_PACKET && m.type!=RTK_DRAIN))break;
        if(m.size && full_io(fd,packet,m.size,0))break;
        if(submit_packet(packet,m.size,m.pts,m.type==RTK_DRAIN))break;
    }
    output_stop=1;shutdown(fd,SHUT_RDWR);pthread_join(output_thread,NULL);free(packet);
    alarm(8);close_omx();alarm(0);puts("RTK_VLC_SESSION_CLOSED");
}
int main(void){
    setvbuf(stdout,NULL,_IOLBF,0);
    sigset_t mask;sigemptyset(&mask);sigprocmask(SIG_SETMASK,&mask,NULL);
    signal(SIGCHLD,SIG_IGN);signal(SIGPIPE,SIG_IGN);
    int server=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(server<0)return 2;
    struct sockaddr_un addr={.sun_family=AF_UNIX};strcpy(addr.sun_path,RTK_SOCKET);
    unlink(RTK_SOCKET);
    if(bind(server,(struct sockaddr*)&addr,sizeof addr) || chown(RTK_SOCKET,65534,29) || chmod(RTK_SOCKET,0600) || listen(server,2))return 3;
    puts("RTK_VLC_BROKER_READY");
    for(;;){
        int fd=accept4(server,NULL,NULL,SOCK_CLOEXEC);if(fd<0){if(errno==EINTR)continue;return 4;}
        struct ucred peer;socklen_t len=sizeof peer;
        if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&len) || (peer.uid!=65534 && peer.uid!=0)){close(fd);continue;}
        pid_t parent=getpid(),child=fork();
        if(child==0){prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(1);close(server);session(fd);close(fd);_exit(0);}
        close(fd);
    }
}
