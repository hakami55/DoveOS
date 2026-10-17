#ifndef DOVE_RGB_SESSION_H
#define DOVE_RGB_SESSION_H
#include <stdint.h>
typedef struct {volatile uint32_t agent,safe,created,phase,owner,touched,restored,gpu_safe;} State;
typedef struct {uint32_t phys,bytes;int fd;} WireBuffer;
#define RGB_SESSION_BYTES 69632
#define RGB_CURSOR_SIDE 128
typedef struct {
 int32_t x,y; uint32_t w,h,shape,visible;
 uint32_t pixels[RGB_CURSOR_SIDE*RGB_CURSOR_SIDE];
} RgbCursor;
typedef struct {
 uint32_t magic,width,height,parent;State state;WireBuffer buffers[5];
 volatile uint32_t frames,reused,bad,wraps,ready,closed,errors,copies;
 volatile uint32_t display_pid,display_reaped,cursor_pid,cursor_reaped,cursor_seq,cursor_updates,cursor_frames;
 volatile uint32_t heartbeat,cursor_heartbeat,owner_heartbeat,pause_request,paused,epoch;
 RgbCursor cursor;
} RgbSession;
_Static_assert(sizeof(RgbSession)<=RGB_SESSION_BYTES,"shared session allocation");
#define RGB_SESSION_MAGIC 0x44524743u
#endif
