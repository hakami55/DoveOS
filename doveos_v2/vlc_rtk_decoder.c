/* SPDX-License-Identifier: GPL-2.0-or-later
 * VLC 3 decoder module. Vendor code stays in a separate host process.
 */
#define _GNU_SOURCE
#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_codec.h>
#include <vlc_picture.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include "vlc_rtk_protocol.h"
struct decoder_sys_t { int fd; pthread_t receiver; bool started,prepend; int failed; unsigned frames,nal_length; uint8_t *prefix; size_t prefix_size; };
static int full_io(int fd, void *p, size_t n, bool sending) {
    while(n) { ssize_t k=sending?send(fd,p,n,MSG_NOSIGNAL):recv(fd,p,n,0);
        if(k<0 && errno==EINTR)continue;
        if(k<=0)return -1;
        p=(char*)p+k;n-=(size_t)k;
    } return 0;
}
static void *receive_frames(void *arg) {
    decoder_t *dec=arg;decoder_sys_t *s=dec->p_sys;
    size_t bytes=dec->fmt_out.video.i_width*dec->fmt_out.video.i_height*3/2;
    uint8_t *frame=malloc(bytes);if(!frame){s->failed=1;return NULL;}
    pthread_cleanup_push(free,frame);
    for(;;) {
        rtk_msg m;
        if(full_io(s->fd,&m,sizeof m,false))break;
        if(m.magic!=RTK_MAGIC || m.type!=RTK_FRAME || m.width!=dec->fmt_out.video.i_width ||
           m.height!=dec->fmt_out.video.i_height || m.size!=m.width*m.height*3/2 || m.size>RTK_LIMIT)break;
        if(full_io(s->fd,frame,m.size,false))break;
        picture_t *pic=decoder_NewPicture(dec);
        if(!pic)break;
        for(unsigned y=0;y<m.height;y++)
            memcpy(pic->p[0].p_pixels+y*pic->p[0].i_pitch,frame+y*m.width,m.width);
        for(unsigned y=0;y<m.height/2;y++)
            memcpy(pic->p[1].p_pixels+y*pic->p[1].i_pitch,frame+m.width*m.height+y*m.width,m.width);
        pic->date=m.pts;pic->b_progressive=true;
        decoder_QueueVideo(dec,pic);s->frames++;
        if(s->frames==1 || s->frames%120==0)msg_Info(dec,"REALTEK_HARDWARE_FRAME count=%u pts=%lld",s->frames,(long long)m.pts);
    }
    pthread_cleanup_pop(1);
    __atomic_store_n(&s->failed,1,__ATOMIC_RELEASE);
    return NULL;
}
static void disconnect_decoder(decoder_t *dec) {
    decoder_sys_t *s=dec->p_sys;
    if(s->fd>=0)shutdown(s->fd,SHUT_RDWR);
    if(s->started){pthread_cancel(s->receiver);pthread_join(s->receiver,NULL);s->started=false;}
    if(s->fd>=0)close(s->fd);
    s->fd=-1;
}
static int connect_decoder(decoder_t *dec) {
    decoder_sys_t *s=dec->p_sys;s->failed=0;s->prepend=true;
    s->fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(s->fd<0)return -1;
    struct sockaddr_un addr={.sun_family=AF_UNIX};strcpy(addr.sun_path,RTK_SOCKET);
    struct timeval timeout={5,0};setsockopt(s->fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
    setsockopt(s->fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
    rtk_msg m={.magic=RTK_MAGIC,.type=RTK_INIT,.width=dec->fmt_in.video.i_width,.height=dec->fmt_in.video.i_height,.flags=(dec->fmt_out.video.i_width<<16)|dec->fmt_out.video.i_height};
    if(connect(s->fd,(struct sockaddr*)&addr,sizeof addr) || full_io(s->fd,&m,sizeof m,true) ||
       full_io(s->fd,&m,sizeof m,false) || m.magic!=RTK_MAGIC || m.type!=RTK_ACK){msg_Err(dec,"Realtek broker handshake failed: %s",strerror(errno));disconnect_decoder(dec);return -1;}
    timeout.tv_sec=0;setsockopt(s->fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
    if(pthread_create(&s->receiver,NULL,receive_frames,dec)){disconnect_decoder(dec);return -1;}
    s->started=true;return 0;
}
static int decode(decoder_t *dec,block_t *block) {
    decoder_sys_t *s=dec->p_sys;
    if(__atomic_load_n(&s->failed,__ATOMIC_ACQUIRE)){if(block)block_Release(block);return VLCDEC_ECRITICAL;}
    rtk_msg m={.magic=RTK_MAGIC,.type=block?RTK_PACKET:RTK_DRAIN};
    uint8_t *annex=NULL;const uint8_t *data=block?block->p_buffer:NULL;
    if(block){
        if(block->i_buffer>RTK_LIMIT || block->i_flags&BLOCK_FLAG_CORRUPTED){block_Release(block);return VLCDEC_SUCCESS;}
        m.size=block->i_buffer;m.pts=block->i_pts!=VLC_TS_INVALID?block->i_pts:block->i_dts;
        if(s->nal_length || s->prepend){
            size_t capacity=RTK_LIMIT,used=s->prepend?s->prefix_size:0;
            annex=malloc(capacity);
            if(!annex){block_Release(block);return VLCDEC_ECRITICAL;}
            if(used)memcpy(annex,s->prefix,used);
            if(s->nal_length){
                size_t pos=0;
                while(pos<block->i_buffer){
                    if(block->i_buffer-pos<s->nal_length)goto malformed;
                    uint32_t n=0;for(unsigned k=0;k<s->nal_length;k++)n=(n<<8)|block->p_buffer[pos++];
                    if(n>block->i_buffer-pos || n+4>capacity-used)goto malformed;
                    memcpy(annex+used,"\0\0\0\1",4);used+=4;
                    memcpy(annex+used,block->p_buffer+pos,n);used+=n;pos+=n;
                }
            }else{
                if(block->i_buffer>capacity-used)goto malformed;
                memcpy(annex+used,block->p_buffer,block->i_buffer);used+=block->i_buffer;
            }
            data=annex;m.size=used;s->prepend=false;
        }
    }
    int error=full_io(s->fd,&m,sizeof m,true);
    if(!error && block)error=full_io(s->fd,(void*)data,m.size,true);
    free(annex);
    if(block)block_Release(block);
    else if(!error)usleep(200000);
    return error?VLCDEC_ECRITICAL:VLCDEC_SUCCESS;
malformed:
    msg_Err(dec,"Malformed H264 NAL packet");free(annex);block_Release(block);return VLCDEC_ECRITICAL;
}
static void flush(decoder_t *dec){disconnect_decoder(dec);if(connect_decoder(dec))msg_Err(dec,"Realtek decoder restart failed");}
static int open_decoder(vlc_object_t *obj) {
    decoder_t *dec=(decoder_t*)obj;
    if(dec->fmt_in.i_codec!=VLC_CODEC_H264)return VLC_EGENERIC;
    unsigned w=dec->fmt_in.video.i_width,h=dec->fmt_in.video.i_height;
    msg_Info(dec,"Realtek candidate width=%u height=%u visible=%ux%u packetized=%d extra=%d",w,h,dec->fmt_in.video.i_visible_width,dec->fmt_in.video.i_visible_height,dec->fmt_in.b_packetized,dec->fmt_in.i_extra);
    if(!w||!h||w>1920||h>1088||(w&15)||(h&1))return VLC_EGENERIC;
    if(dec->fmt_in.i_extra>=2){
        const uint8_t *e=dec->fmt_in.p_extra;
        if(e[0]==1 && e[1]!=66 && e[1]!=77 && e[1]!=88 && e[1]!=100)return VLC_EGENERIC;
    }
    decoder_sys_t *s=calloc(1,sizeof *s);if(!s)return VLC_ENOMEM;
    s->fd=-1;dec->p_sys=s;
    if(dec->fmt_in.i_extra>0){
        const uint8_t *extra=dec->fmt_in.p_extra;size_t length=dec->fmt_in.i_extra;
        s->prefix=malloc(length*2+32);if(!s->prefix){free(s);return VLC_ENOMEM;}
        if(length>=7 && extra[0]==1){
            s->nal_length=(extra[4]&3)+1;size_t pos=6;
            for(unsigned group=0;group<2;group++){
                if(pos>=length)goto bad_extra;
                unsigned count=group?extra[pos++]:extra[5]&31;
                for(unsigned i=0;i<count;i++){
                    if(length-pos<2)goto bad_extra;
                    unsigned n=((unsigned)extra[pos]<<8)|extra[pos+1];pos+=2;
                    if(n>length-pos)goto bad_extra;
                    memcpy(s->prefix+s->prefix_size,"\0\0\0\1",4);s->prefix_size+=4;
                    memcpy(s->prefix+s->prefix_size,extra+pos,n);s->prefix_size+=n;pos+=n;
                }
            }
        }else{memcpy(s->prefix,extra,length);s->prefix_size=length;}
    }
    dec->fmt_out.i_cat=VIDEO_ES;dec->fmt_out.i_codec=VLC_CODEC_NV12;
    dec->fmt_out.video=dec->fmt_in.video;dec->fmt_out.video.i_chroma=VLC_CODEC_NV12;
    if(w>1280 || h>720){
        unsigned target_w=1280,target_h=(uint64_t)h*target_w/w;
        if(target_h>720){target_h=720;target_w=((uint64_t)w*target_h/h)&~15u;}
        target_h&=~1u;
        dec->fmt_out.video.i_width=dec->fmt_out.video.i_visible_width=target_w;
        dec->fmt_out.video.i_height=dec->fmt_out.video.i_visible_height=target_h;
        dec->fmt_out.video.i_x_offset=dec->fmt_out.video.i_y_offset=0;
    }
    /* This exact VLC 3.0.9.2 ABI reserves 18 H264 DPB pictures in VLC core.
     * The Realtek process already owns the codec DPB; VLC only needs display
     * copies. 18 - 14 + 1 = five pictures, saving ~42 MiB at 1080p. */
    dec->i_extra_picture_buffers=-14;
    if(!dec->fmt_out.video.i_visible_width)dec->fmt_out.video.i_visible_width=w;
    if(!dec->fmt_out.video.i_visible_height)dec->fmt_out.video.i_visible_height=h;
    if(decoder_UpdateVideoFormat(dec) || connect_decoder(dec))goto bad_extra;
    dec->pf_decode=decode;dec->pf_flush=flush;
    msg_Info(dec,"REALTEK_HARDWARE_DECODER_ACTIVE H264 %ux%u display=%ux%u max=30fps",w,h,dec->fmt_out.video.i_width,dec->fmt_out.video.i_height);
    return VLC_SUCCESS;
bad_extra:
    free(s->prefix);free(s);dec->p_sys=NULL;return VLC_EGENERIC;
}
static void close_decoder(vlc_object_t *obj) {
    decoder_t *dec=(decoder_t*)obj;decoder_sys_t *s=dec->p_sys;
    disconnect_decoder(dec);msg_Info(dec,"Realtek decoded %u frames",s->frames);free(s->prefix);free(s);
}
vlc_module_begin()
    set_shortname("Realtek hardware")
    set_description("DoveOS isolated Realtek H.264 hardware decoder")
    set_capability("video decoder",110)
    add_shortcut("dove_rtk")
    set_callbacks(open_decoder,close_decoder)
vlc_module_end()
