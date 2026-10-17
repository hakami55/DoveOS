#ifndef DOVE_AUDIO_IPC_H
#define DOVE_AUDIO_IPC_H
#include <stdint.h>
#define DA_MAGIC 0x44415031u
#define DA_SOCKET "/tmp/doveplayer/audio.sock"
#define DA_LIMIT 32768u
enum {DA_OPEN=1,DA_PLAY,DA_DELAY,DA_PAUSE,DA_FLUSH,DA_STOP};
typedef struct {uint32_t magic,op,size;int32_t arg;int64_t delay;} da_message;
#endif
