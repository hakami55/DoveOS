#define _GNU_SOURCE
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int set_volume(int percent) {
    snd_ctl_t *ctl=NULL;
    snd_ctl_elem_id_t *id;
    snd_ctl_elem_info_t *info;
    snd_ctl_elem_value_t *value;
    snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_info_alloca(&info);
    snd_ctl_elem_value_alloca(&value);
    if(snd_ctl_open(&ctl,"hw:1",0)<0)return 2;
    snd_ctl_elem_id_set_interface(id,SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_id_set_name(id,"Headset Playback Volume");
    snd_ctl_elem_info_set_id(info,id);
    if(snd_ctl_elem_info(ctl,info)<0){snd_ctl_close(ctl);return 3;}
    long low=snd_ctl_elem_info_get_min(info),high=snd_ctl_elem_info_get_max(info);
    long raw=low+(high-low)*percent/100;
    snd_ctl_elem_value_set_id(value,id);
    unsigned count=snd_ctl_elem_info_get_count(info);
    for(unsigned i=0;i<count;i++)snd_ctl_elem_value_set_integer(value,i,raw);
    int rc=snd_ctl_elem_write(ctl,value);
    snd_ctl_close(ctl);
    if(rc<0)return 4;
    printf("VOLUME=%d\n",percent);
    return 0;
}

int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--check")){puts("DOVEOS_CONTROL_CHECK_OK");return 0;}
    if(argc!=3)return 1;
    char *end=NULL;long value=strtol(argv[2],&end,10);
    if(!end||*end||value<0||value>100)return 1;
    if(!strcmp(argv[1],"volume"))return set_volume((int)value);
    return 1;
}
