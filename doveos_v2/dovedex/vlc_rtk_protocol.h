/* Local-only VLC/Realtek decoder protocol. Both ends are ARM little endian. */
#ifndef DOVE_VLC_RTK_PROTOCOL_H
#define DOVE_VLC_RTK_PROTOCOL_H
#include <stdint.h>
#define RTK_MAGIC 0x44564c43u
#define RTK_SOCKET "/tmp/dovedex-rtk.sock"
#define RTK_LIMIT (5u * 1024u * 1024u)
enum { RTK_INIT=1, RTK_PACKET=2, RTK_DRAIN=3, RTK_ACK=4, RTK_FRAME=5, RTK_ERROR=6 };
typedef struct { uint32_t magic, type, size, width, height, flags; int64_t pts; } rtk_msg;
#endif
