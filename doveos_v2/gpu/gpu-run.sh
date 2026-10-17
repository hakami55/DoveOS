#!/bin/sh
# Private GPU runtime. USB files persist; executable files are cached in RAM.
set -eu
B=/media/usb0/DoveOS-v2/GPU
P=/tmp/doveos-gpu-runtime
M=/tmp/doveos-v2-root
[ -x "$M/lib/ld-linux-armhf.so.3" ] || M=
test -c /dev/mali
test -x "$M/lib/ld-linux-armhf.so.3"
test -f "$B/runtime.tar.gz"
test "$#" -gt 0
mkdir -p "$P"
expected=$(tr -d '\r\n' < "$B/runtime.sha256")
cached=$(cat "$P/.archive-sha256" 2>/dev/null || :)
if [ "$cached" != "$expected" ];then
 actual=$(sha256sum "$B/runtime.tar.gz");actual=${actual%% *}
 [ "$actual" = "$expected" ] || { echo 'GPU runtime checksum mismatch';exit 2; }
 gunzip -c "$B/runtime.tar.gz" | tar -xf - -C "$P"
 chmod 755 "$P/gpu-probe" "$P/gpu-stress"
 printf '%s\n' "$expected" > "$P/.archive-sha256"
fi
for name in libMali.so libMali-r4p1-r1p1-trial.so libEGL.so libEGL.so.1 libGLESv2.so libGLESv2.so.2 libGLESv1_CM.so libGLESv1_CM.so.1;do
 ln -sf libMali-r4p1-r1p1.so "$P/$name"
done
program=$1;shift
case "$program" in probe) program="$P/gpu-probe";set -- "$P/libMali-r4p1-r1p1.so" "$@";;
 stress) program="$P/gpu-stress";;
 /*) test -f "$program";test -x "$program";;
 *) echo 'Use probe, stress, or an absolute RAM executable path';exit 2;;esac
# Keep desktop/GTK libraries unchanged. Only this process uses Mali EGL/GLES.
unset LD_PRELOAD
exec "$M/lib/ld-linux-armhf.so.3" --library-path "$P:$M/usr/lib/arm-linux-gnueabihf:$M/lib/arm-linux-gnueabihf:/usr/lib:/lib" "$program" "$@"
