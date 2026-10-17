#ifndef DOVEOS_GPU_BACKEND_H
#define DOVEOS_GPU_BACKEND_H
#include <stdint.h>
/* Coordinates describe dirty BGRA pixels; output is tightly packed NV12.
 * All calls stay on the creating thread. A failed conversion requires CPU
 * fallback with a full-frame refresh before presenting another frame. */
typedef void *(*DoveGpuCreate)(int width,int height);
typedef int (*DoveGpuConvert)(void *state,const uint8_t *bgra,uint8_t *nv12,
                             int left,int top,int right,int bottom);
typedef void (*DoveGpuDestroy)(void *state);
#endif
