#!/bin/sh
# Explicit GLES2 shared-window applications; existing app launchers are retained.
set -eu
R=/media/usb0/DoveOS-v2/GPU/native-window
M=/tmp/doveos-v2-root
P=/tmp/doveos-gpu-desktop
inside=0
if [ "${1:-}" = --in-desktop ];then inside=1;shift;fi
expected=$(awk 'NR==1{print $1}' "$R/runtime.sha256")
case "$expected" in ''|*[!0-9a-f]*) echo 'Invalid native-window manifest';exit 2;;esac
test "${#expected}" = 64
test "$(sha256sum "$R/runtime.tar.gz" | awk '{print $1}')" = "$expected"
N=/tmp/doveos-native-window-$expected
if [ ! -f "$N/ready" ];then
 mkdir "$N.install-lock" || { echo 'Native runtime installation in progress';exit 2; }
 trap 'rmdir "$N.install-lock" 2>/dev/null || :' EXIT
 trap 'exit 143' INT TERM
 test ! -e "$N" || { echo 'Incomplete native runtime; restart DoveOS';exit 2; }
 mkdir "$N"
 gunzip -c "$R/runtime.tar.gz" | tar -xf - -C "$N"
 (cd "$N";sha256sum -c files.sha256) > /dev/null
 touch "$N/ready"
 rmdir "$N.install-lock";trap - EXIT INT TERM
fi
(cd "$N";sha256sum -c files.sha256) > /dev/null
test -S /tmp/doveos-rgb/native-windows.sock || { echo 'Start the 720p RGB GPU desktop';exit 2; }
test -r "$P/libMali-r4p1-native-trial.so"
if [ "$#" = 0 ];then set -- "$N/dove-gpu-demo";fi
if [ "$1" = --mouse-test ];then shift;set -- "$N/doveos-mouse-test-gpu" "$@";fi
case "$1" in /*) ;; *) echo 'Use an absolute app path inside DoveOS';exit 2;;esac
run_child(){
 if [ "$inside" = 1 ];then exec /usr/bin/env "$@";fi
 exec chroot "$M" /usr/bin/env "$@"
}
run_child DISPLAY=:7 LD_LIBRARY_PATH="$N:$P:$P/usr/lib/arm-linux-gnueabihf:$P/lib/arm-linux-gnueabihf:/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf" LD_PRELOAD="$P/gpu-dma-shim.so" DOVE_NATIVE_MALI="$P/libMali-r4p1-native-trial.so" DOVE_ION_LIBRARY="$P/libVOutAllocator.so" DOVE_COMPOSITOR_SOCKET=/tmp/doveos-rgb/native-windows.sock "$@"
