#ifndef DOVEOS_FILE_TYPES_H
#define DOVEOS_FILE_TYPES_H
#include <string.h>
#include <strings.h>

enum dove_file_type { DOVE_UNSUPPORTED, DOVE_TEXT, DOVE_PICTURE, DOVE_AUDIO, DOVE_VIDEO };

static enum dove_file_type dove_file_type(const char *name) {
    const char *dot=strrchr(name,'.');
    if(!dot)return DOVE_UNSUPPORTED;
    if(!strcasecmp(dot,".txt"))return DOVE_TEXT;
    if(!strcasecmp(dot,".png")||!strcasecmp(dot,".jpg")||
       !strcasecmp(dot,".jpeg")||!strcasecmp(dot,".bmp"))return DOVE_PICTURE;
    if(!strcasecmp(dot,".wav")||!strcasecmp(dot,".mp3")||
       !strcasecmp(dot,".flac")||!strcasecmp(dot,".ogg"))return DOVE_AUDIO;
    if(!strcasecmp(dot,".mp4")||!strcasecmp(dot,".mkv")||
       !strcasecmp(dot,".mov")||!strcasecmp(dot,".avi"))return DOVE_VIDEO;
    return DOVE_UNSUPPORTED;
}
#endif
