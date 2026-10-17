#!/bin/sh
# Process-scoped legacy OpenGL/GLES2 compatibility, with X11 pbuffer presentation.
set -eu
B=/media/usb0/DoveOS-v2/GPU
M=/tmp/doveos-v2-root
test -c /dev/mali || { echo 'Mali GPU is unavailable';exit 2; }
test -f "$M/usr/bin/env" || { echo 'Start the DoveOS desktop first';exit 2; }
test "$#" -gt 0 || { echo 'Usage: gpu-app-run.sh /absolute/app/path [arguments]';exit 2; }
mode=copy
case "$1" in
 --direct) shift;exec sh "$B/direct-apps/gpu-direct-app-run.sh" "$@";;
 --window) mode=window;shift;;
 --gles-window) mode=gles;shift;;
esac
test "$#" -gt 0 || { echo 'Provide an application path';exit 2; }
case "$1" in /*) ;; *) echo 'Use an absolute application path inside DoveOS';exit 2;; esac
expected=$(awk 'NR==1{print $1}' "$B/app-runtime.sha256")
case "$expected" in ''|*[!0-9a-f]*) echo 'Invalid GPU runtime manifest';exit 3;;esac
test "${#expected}" = 64 || { echo 'Invalid GPU runtime manifest';exit 3; }
actual=$(sha256sum "$B/app-runtime.tar.gz" | awk '{print $1}')
test "$expected" = "$actual" || { echo 'GPU app runtime checksum failed';exit 3; }
# A content-addressed directory keeps updates from replacing libraries mapped
# by an already-running app. The legacy runtime remains available to old users.
P=/tmp/doveos-v2-gpu-apps-$expected
installed=$(cat "$P/runtime.sha256" 2>/dev/null || :)
if [ "$installed" != "$expected" ];then
 lock=$P.install-lock
 mkdir "$lock" 2>/dev/null || { echo 'GPU runtime installation in progress; retry shortly';exit 3; }
 trap 'rmdir "$lock" 2>/dev/null || :' EXIT
 trap 'exit 143' INT TERM
 test ! -e "$P" || { echo 'Incomplete GPU runtime; restart DoveOS to clear it';exit 3; }
 mkdir "$P"
 gunzip -c "$B/app-runtime.tar.gz" | tar -xf - -C "$P"
 (cd "$P" && sha256sum -c runtime-files.sha256) > "$lock/verify.log" 2>&1 || {
  cat "$lock/verify.log";rm -f "$lock/verify.log";echo 'GPU runtime member verification failed';exit 3;
 }
 printf '%s\n' "$expected" > "$P/runtime.sha256"
 rm -f "$lock/verify.log";rmdir "$lock";trap - EXIT INT TERM
fi
(cd "$P" && sha256sum -c runtime-files.sha256) > /dev/null 2>&1 || { echo 'Installed GPU runtime checksum failed';exit 3; }
libs="$P:/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf"
egl="$P/libEGL.so.1";fb=3
if [ "$mode" = window ] || [ "$mode" = gles ];then
 test -r "$P/window/libEGL.so.1" || { echo 'GPU window adapter unavailable';exit 3; }
 libs="$P/window:$libs";egl="$P/window/libEGL.so.1";fb=0
fi
if [ "$mode" = gles ];then
 # Native GLES clients must resolve GL symbols before the legacy GL4ES shim.
 # Set the preload only AFTER entering the desktop root. The host chroot
 # utility does not have the desktop X11 dependencies in its library path.
 exec chroot "$M" /usr/bin/env DISPLAY=:7 \
  LD_LIBRARY_PATH="$libs" DOVE_EGL_MALI_LIBRARY="$P/libMali-app.so" \
  DOVE_EGL_X11_PLATFORM_COMPAT=1 LD_PRELOAD="$egl:$P/libGLESv2.so.2" \
  LIBGL_EGL="$egl" LIBGL_GLES="$P/libGLESv2.so.2" \
  LIBGL_ES=2 LIBGL_FB=0 LIBGL_NOPSA=1 LIBGL_BLITFULLSCREEN=0 "$@"
fi
exec chroot "$M" /usr/bin/env DISPLAY=:7 \
 LD_LIBRARY_PATH="$libs" DOVE_EGL_MALI_LIBRARY="$P/libMali-app.so" \
 LIBGL_EGL="$egl" LIBGL_GLES="$P/libGLESv2.so.2" \
 LIBGL_ES=2 LIBGL_FB="$fb" LIBGL_NOPSA=1 LIBGL_BLITFULLSCREEN=0 "$@"
