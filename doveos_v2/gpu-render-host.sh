#!/bin/sh
# Private optional conversion runtime. CPU fallback leaves app libraries alone.
set -eu
B=/media/usb0/DoveOS-v2
M=/tmp/doveos-v2-root
P=/tmp/doveos-gpu-render
CPU_LIBS="$M/usr/lib/arm-linux-gnueabihf:$M/lib/arm-linux-gnueabihf:/tmp/drmosa-usb-menu-runtime/lib:/usr/lib:/lib"
mode=$(cat "$B/home/render-mode.txt" 2>/dev/null || echo auto)
case "$mode" in auto|gpu) ;; *) mode=cpu;; esac
if [ "$mode" != cpu ];then
 if [ ! -f "$B/GPU/render-runtime.tar.gz" ] || ! (cd "$B/GPU" && tr -d '\r' < render-runtime.sha256 | sha256sum -c - >/dev/null 2>&1);then
  echo GPU_RUNTIME_CHECK_FAILED_CPU_FALLBACK;mode=cpu
 else
  mkdir -p "$P"
  if ! gunzip -c "$B/GPU/render-runtime.tar.gz" | tar -xf - -C "$P";then mode=cpu;fi
 fi
fi
if [ "$mode" != cpu ];then
 export DOVE_GPU_MODE="$mode" DOVE_GPU_LIBRARY="$P/libdoveos-gpu.so"
 export LD_PRELOAD="$P/gpu-dma-shim.so"
 exec "$M/lib/ld-linux-armhf.so.3" --library-path "$P:$CPU_LIBS" /tmp/doveos-v2-bridge "$@"
fi
unset LD_PRELOAD
export DOVE_GPU_MODE=cpu
exec "$M/lib/ld-linux-armhf.so.3" --library-path "$CPU_LIBS" /tmp/doveos-v2-bridge "$@"
