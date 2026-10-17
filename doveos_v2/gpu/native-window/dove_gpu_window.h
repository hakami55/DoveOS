#ifndef DOVE_GPU_WINDOW_H
#define DOVE_GPU_WINDOW_H
#include <X11/Xlib.h>
#include <GLES2/gl2.h>
typedef struct DoveGpuWindow DoveGpuWindow;
/* Explicit native GLES2 API. Use one rendering thread per process; call on the creating thread/context.
 * The caller owns its X window and EGL context. No framebuffer-zero interception.
 * Opaque colour targets only. begin follows window resize and binds its target.
 * begin returns 1 on success, 0 while hidden, -1 on failure. Caller draws only
 * after success, then calls present. The compositor leases the last complete
 * image until replacement or disconnect. Unsupported apps keep the old path. */
DoveGpuWindow *dove_gpu_window_attach(Window window);
/* Reserve smaller fixed bounds for a canvas. Resize beyond them ends this
 * session. Default attach reserves 1280x720; capacity must be 32..1280x32..720. */
DoveGpuWindow *dove_gpu_window_attach_capacity(Window window, int max_width, int max_height);
int dove_gpu_window_begin(DoveGpuWindow *window, int *width, int *height);
int dove_gpu_window_present(DoveGpuWindow *window);
int dove_gpu_window_flush(DoveGpuWindow *window);
int dove_gpu_window_detach(DoveGpuWindow *window);
/* Optional compatibility copy from an owned source FBO (0 is current pbuffer).
 * Preserves bindings/viewport/scissor. Returns 1 presented, 0 hidden, -1 failed.
 * One GPU image copy is required; no CPU pixel readback. */
int dove_gpu_window_copy_framebuffer(DoveGpuWindow *window, unsigned source, int width, int height);
#endif
