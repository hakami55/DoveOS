/* The dock's OMX export corrupts chroma for cropped, non-aligned dimensions.
   Decode the complete coded rectangle; crop only while composing the display.
   Encoded picture data, colour metadata and timing are left unchanged.
   Optional bounded low-delay trial preserves reference pictures and rejects
   B slices. It is not an assumption about every AirPlay sender. */
#include "h264_stream.h"
static uint8_t airplay_old_sps[512],airplay_new_sps[1024];
static size_t airplay_old_sps_size,airplay_new_sps_size;
static unsigned airplay_slice_counts[5];
static int airplay_low_delay_active;
static int airplay_slice_kind(const uint8_t *nal,size_t size){
    /* Only the two initial Exp-Golomb fields; never parse compressed pixels. */
    uint8_t rbsp[32];size_t n=0;unsigned zeros=0;
    for(size_t i=1;i<size&&n<sizeof rbsp;i++){
        if(zeros==2&&nal[i]==3){zeros=0;continue;}
        rbsp[n++]=nal[i];zeros=nal[i]==0?zeros+1:0;
    }
    size_t bit=0;unsigned value=0;
    for(int field=0;field<2;field++){
        unsigned leading=0;
        while(bit<n*8 && !(rbsp[bit/8]&(1u<<(7-bit%8)))){bit++;if(++leading>24)return -1;}
        if(bit>=n*8 || bit+1+leading>n*8)return -1;
        bit++;value=1;
        while(leading--){value=(value<<1)|((rbsp[bit/8]>>(7-bit%8))&1);bit++;}
        value--;
    }
    return value<=9?(int)(value%5):-1;
}
static int airplay_start_code(const uint8_t *p,size_t n,size_t from,size_t *prefix){
    for(size_t i=from;i+3<=n;i++){
        if(p[i] || p[i+1])continue;
        if(p[i+2]==1){*prefix=3;return (int)i;}
        if(i+4<=n && !p[i+2] && p[i+3]==1){*prefix=4;return (int)i;}
    }
    return -1;
}
static int airplay_normalize_sps(const uint8_t *data,size_t size,uint8_t **result,size_t *result_size){
    if(!size || size>2*1024*1024)return -1;
    uint8_t *copy=malloc(size+4096);if(!copy)return -1;
    size_t written=0,position=0,prefix=0;
    int start=airplay_start_code(data,size,0,&prefix);
    if(start<0){free(copy);return -1;}
    memcpy(copy,data,(size_t)start);written=(size_t)start;position=(size_t)start;
    while(position<size){
        size_t next_prefix=0;
        int next=airplay_start_code(data,size,position+prefix,&next_prefix);
        size_t stop=next<0?size:(size_t)next;
        const uint8_t *nal=data+position+prefix;
        size_t length=stop-position-prefix;
        const uint8_t *payload=nal;size_t payload_size=length;
        if(length && ((nal[0]&31)==1 || (nal[0]&31)==5)){
            int kind=airplay_slice_kind(nal,length);
            if(kind>=0)airplay_slice_counts[kind]++;
            if(airplay_low_delay_active && kind!=0 && kind!=2){
                printf("AIRPLAY_LOW_DELAY_UNSUPPORTED_SLICE=%d\n",kind);
                free(copy);return -1;
            }
        }
        if(length && (nal[0]&31)==7){
            if(length>sizeof airplay_old_sps){free(copy);return -1;}
            if(length!=airplay_old_sps_size || memcmp(nal,airplay_old_sps,length)){
                h264_stream_t *h=h264_new();if(!h){free(copy);return -1;}
                int ok=read_nal_unit(h,(uint8_t*)nal,(int)length)>0;
                sps_t *s=h->sps;
                int coded_width=(s->pic_width_in_mbs_minus1+1)*16;
                int coded_height=(s->pic_height_in_map_units_minus1+1)*16;
                if(!ok || !s->frame_mbs_only_flag ||
                   coded_width!=((airplay_visible_width+15)&~15) ||
                   coded_height!=((airplay_visible_height+15)&~15) ||
                   s->frame_crop_left_offset || s->frame_crop_top_offset){
                    h264_free(h);free(copy);return -1;
                }
                printf("AIRPLAY_SPS_ORIGINAL profile=%d level=%d refs=%d poc=%d restriction=%d reorder=%d buffering=%d\n",
                       s->profile_idc,s->level_idc,s->num_ref_frames,s->pic_order_cnt_type,
                       s->vui.bitstream_restriction_flag,s->vui.num_reorder_frames,s->vui.max_dec_frame_buffering);
                if((airplay_visible_width&15)||(airplay_visible_height&15))s->frame_cropping_flag=0;
                airplay_low_delay_active=0;
                if(getenv("DOVE_AIRPLAY_LOW_DELAY_TRIAL") &&
                   (!s->vui_parameters_present_flag || !s->vui.bitstream_restriction_flag) &&
                   s->num_ref_frames>0 && s->num_ref_frames<=16){
                    s->vui_parameters_present_flag=1;
                    s->vui.bitstream_restriction_flag=1;
                    s->vui.motion_vectors_over_pic_boundaries_flag=1;
                    s->vui.max_bytes_per_pic_denom=0;
                    s->vui.max_bits_per_mb_denom=0;
                    s->vui.log2_max_mv_length_horizontal=16;
                    s->vui.log2_max_mv_length_vertical=16;
                    s->vui.num_reorder_frames=0;
                    s->vui.max_dec_frame_buffering=s->num_ref_frames;
                    airplay_low_delay_active=1;
                    printf("AIRPLAY_LOW_DELAY_TRIAL refs=%d reorder=0 buffering=%d\n",s->num_ref_frames,s->num_ref_frames);
                }
                int bytes=write_nal_unit(h,airplay_new_sps,sizeof airplay_new_sps);
                h264_free(h);
                if(bytes<=0){free(copy);return -1;}
                memcpy(airplay_old_sps,nal,length);airplay_old_sps_size=length;
                airplay_new_sps_size=(size_t)bytes;
                printf("AIRPLAY_SPS_CODED_RECT visible=%dx%d coded=%dx%d\n",
                       airplay_visible_width,airplay_visible_height,coded_width,coded_height);
            }
            payload=airplay_new_sps;payload_size=airplay_new_sps_size;
        }
        if(written+prefix+payload_size>size+4096){free(copy);return -1;}
        memcpy(copy+written,data+position,prefix);written+=prefix;
        memcpy(copy+written,payload,payload_size);written+=payload_size;
        if(next<0)break;
        position=(size_t)next;prefix=next_prefix;
    }
    *result=copy;*result_size=written;return 0;
}
