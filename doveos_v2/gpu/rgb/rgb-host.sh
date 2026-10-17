#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2/GPU/rgb
R=/tmp/doveos-rgb
P=/tmp/doveos-gpu-desktop
M=/tmp/doveos-v2-root
case "${1:-}" in
 prepare)
  test ! -e /tmp/gpu-rgb-retained
  test "$(sha256sum /usr/sbin/ALSADaemon | cut -d ' ' -f1)" = ad13b4c06b121fb4af6be37a4fe0325c8b64687842bb831f5e0d8b784e6f848e
  if [ -r /media/usb0/DoveOS-v2/Memory/static-runtime.sh ] && [ -d "$R" ];then
   /bin/sh /media/usb0/DoveOS-v2/Memory/static-runtime.sh unseal rgb
  fi
  (cd "$B";sha256sum -c runtime.sha256)
  expected=$(cut -d ' ' -f1 "$B/runtime.sha256")
  if [ "$(cat "$R/installed.sha256" 2>/dev/null || :)" != "$expected" ];then
   # Running binaries cannot be replaced underneath an existing guardian.
   test ! -f /tmp/doveos-rgb-guardian.pid
   mkdir -p "$R"
   gunzip -c "$B/runtime.tar.gz" | tar -xf - -C "$R"
   (cd "$R";sha256sum -c files.sha256)
   chmod 700 "$R/rgb-x-guardian" "$R/rgb-720-cursor" "$R/Xephyr-rgb-720"
   echo "$expected" > "$R/installed.sha256"
  fi
  (cd "$R";sha256sum -c files.sha256)
  if [ -r /media/usb0/DoveOS-v2/Memory/static-runtime.sh ];then
   /bin/sh /media/usb0/DoveOS-v2/Memory/static-runtime.sh stage rgb || echo RGB_STATIC_RAM_FALLBACK
  fi
  ;;
 server)
  test ! -e /tmp/doveos-direct-display
  test ! -e /tmp/gpu-rgb-retained
  set -- $(pidof ALSADaemon);test "$#" -eq 1
  # Guardian lives outside the desktop chroot. A failed recovery retains its
  # allocations independently of desktop cleanup and reports an unsafe state.
  export DISPLAY=:6 HOME=/root DOVE_RPC_LIBRARY=/usr/lib/libVOutWrapper.so
  export LIBGL_GLES="$P/libMali-r4p1-native-trial.so" DOVE_EGL_MALI_LIBRARY="$P/libMali-r4p1-native-trial.so"
  export LIBGL_EGL="$P/libEGL-direct.so" LIBGL_TEXCOPY=0 LIBGL_DEEPBIND=0 LIBGL_FB=0 LIBGL_ES=2 LIBGL_NOPSA=1 LIBGL_BLITFULLSCREEN=0 DOVE_BIND_PBUFFER=1
  export LD_LIBRARY_PATH="$P:$P/usr/lib/arm-linux-gnueabihf:$P/lib/arm-linux-gnueabihf:$M/usr/lib/arm-linux-gnueabihf:$M/lib/arm-linux-gnueabihf:/usr/lib:/lib"
  exec "$M/lib/ld-linux-armhf.so.3" "$R/rgb-x-guardian" "$1"
  ;;
 cursor)
  guard=$(cat /tmp/doveos-gpu-desktop.pid)
  n=0
  # The guardian owns and monitors the cursor child. Its fresh readiness log
  # is the startup contract; the child's PID file is only diagnostic.
  while [ "$n" -lt 12 ] && kill -0 "$guard" 2>/dev/null;do
   if grep -q 'GPU_CURSOR_OVERLAY_READY renderer=rgb720' /media/usb0/DoveOS-v2/GPU/desktop/server.log;then break;fi
   n=$((n+1));sleep 1
  done
  kill -0 "$guard"
  grep -q 'GPU_CURSOR_OVERLAY_READY renderer=rgb720' /media/usb0/DoveOS-v2/GPU/desktop/server.log
  echo GPU_CURSOR_OVERLAY_READY_RGB_SUPERVISED
  while kill -0 "$guard" 2>/dev/null;do sleep 1;done
  exit 1;;
 stop)
  p=${2:-};case "$p" in ''|*[!0-9]*) exit 2;;esac
  printf '%s\n' "$p" > /tmp/doveos-rgb-stop
  kill -TERM "$p" 2>/dev/null || :
  n=0
  while kill -0 "$p" 2>/dev/null && [ "$n" -lt 35 ];do
   state=$(awk '{print $3}' "/proc/$p/stat" 2>/dev/null || :)
   [ "$state" != Z ] || break
   test ! -e /tmp/gpu-rgb-retained || exit 1
   sleep 1;n=$((n+1))
  done
  test ! -e /tmp/gpu-rgb-retained
  if kill -0 "$p" 2>/dev/null && [ "${state:-}" != Z ];then echo RGB_GUARDIAN_RETAINED_NOT_KILLED;exit 1;fi
  ;;
 *) exit 2;;
esac
