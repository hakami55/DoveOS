#!/bin/sh
# Explicit opt-in for qualified single-render-thread opaque GLES2 X11 apps.
set -eu
R=/media/usb0/DoveOS-v2/GPU/direct-apps
M=/tmp/doveos-v2-root
P=/tmp/doveos-gpu-desktop
test "$#" -gt 0 || { echo 'Provide an absolute GLES2 app path';exit 2; }
case "$1" in /*) ;; *) echo 'Use an absolute app path inside DoveOS';exit 2;;esac
test -S /tmp/doveos-rgb/native-windows.sock || { echo 'Start the 720p RGB GPU desktop';exit 2; }
expected=$(awk 'NR==1{print $1}' "$R/runtime.sha256")
case "$expected" in ''|*[!0-9a-f]*) exit 3;;esac
test "${#expected}" = 64
test "$(sha256sum "$R/runtime.tar.gz" | awk '{print $1}')" = "$expected"
N=/tmp/doveos-direct-apps-$expected
if [ ! -f "$N/ready" ];then
 mkdir "$N.install-lock" || { echo 'Runtime installation in progress';exit 3; }
 trap 'rmdir "$N.install-lock" 2>/dev/null || :' EXIT
 trap 'exit 143' INT TERM
 test ! -e "$N" || { echo 'Incomplete runtime; restart DoveOS';exit 3; }
 mkdir "$N"
 gunzip -c "$R/runtime.tar.gz" | tar -xf - -C "$N"
 (cd "$N";sha256sum -c files.sha256) > /dev/null
 ln -s libEGL.so.1 "$N/libEGL.so"
 ln -s "$P/libMali-r4p1-native-trial.so" "$N/libGLESv2.so.2"
 ln -s libGLESv2.so.2 "$N/libGLESv2.so"
 touch "$N/ready";rmdir "$N.install-lock";trap - EXIT INT TERM
fi
(cd "$N";sha256sum -c files.sha256) > /dev/null
test "$(readlink "$N/libEGL.so")" = libEGL.so.1
test "$(readlink "$N/libGLESv2.so.2")" = "$P/libMali-r4p1-native-trial.so"
test "$(readlink "$N/libGLESv2.so")" = libGLESv2.so.2
exec chroot "$M" /usr/bin/env DISPLAY=:7 \
 LD_LIBRARY_PATH="$N:$P:$P/usr/lib/arm-linux-gnueabihf:$P/lib/arm-linux-gnueabihf:/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf" \
 LD_PRELOAD="$N/libEGL.so.1:$P/libMali-r4p1-native-trial.so:$P/gpu-dma-shim.so" \
 LIBGL_EGL="$N/libEGL.so.1" LIBGL_GLES="$P/libMali-r4p1-native-trial.so" LIBGL_ES=2 LIBGL_FB=0 LIBGL_NOPSA=1 LIBGL_BLITFULLSCREEN=0 \
 DOVE_EGL_MALI_LIBRARY="$P/libMali-r4p1-native-trial.so" DOVE_EGL_X11_PLATFORM_COMPAT=1 \
 DOVE_NATIVE_MALI="$P/libMali-r4p1-native-trial.so" DOVE_ION_LIBRARY="$P/libVOutAllocator.so" \
 DOVE_COMPOSITOR_SOCKET=/tmp/doveos-rgb/native-windows.sock DOVE_WINDOW_DIRECT_LIBRARY="$N/libdove-gpu-window.so" "$@"
