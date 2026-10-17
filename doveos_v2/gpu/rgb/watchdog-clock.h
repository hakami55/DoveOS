#ifndef DOVE_RGB_WATCHDOG_CLOCK_H
#define DOVE_RGB_WATCHDOG_CLOCK_H
#include <stdint.h>
/* A writer may publish a heartbeat after the reader sampled `now`.
 * Unsigned subtraction then looks almost 49.7 days old. Treat that short
 * future interval as fresh while retaining wrap-safe real stall detection.
 * All watchdog deadlines are much shorter than half the uint32 clock range. */
static inline int rgb_heartbeat_expired(uint32_t now,uint32_t beat,uint32_t timeout){
 uint32_t age=now-beat;
 return age<UINT32_C(0x80000000)&&age>timeout;
}
static inline int rgb_heartbeat_ahead(uint32_t now,uint32_t beat){
 return now-beat>=UINT32_C(0x80000000);
}
#endif
