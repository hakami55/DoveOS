/* Local-only VLC/Realtek decoder protocol. Both ends are ARM little endian. */
#ifndef DOVE_VLC_RTK_PROTOCOL_H
#define DOVE_VLC_RTK_PROTOCOL_H
#include <stdint.h>
#define RTK_MAGIC 0x44564c43u
#define RTK_SOCKET "/tmp/doveplayer/video.sock"
#define RTK_LIMIT (5u * 1024u * 1024u)
enum { RTK_INIT=1, RTK_PACKET=2, RTK_DRAIN=3, RTK_ACK=4, RTK_FRAME=5, RTK_ERROR=6, RTK_DISPLAY=7, RTK_RELEASE=8, RTK_SHARED_FRAME=9 };
typedef struct { uint32_t magic, type, size, width, height, flags; int64_t pts; } rtk_msg;
typedef struct { uint32_t offset, stride, chroma_offset; } rtk_shared_info;
#endif
