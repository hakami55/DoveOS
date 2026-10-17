#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2/GPU/desktop
P=/tmp/doveos-gpu-desktop
M=/tmp/doveos-v2-root
screen=1280x720
if [ "$(cat /tmp/doveos-desktop-resolution-trial 2>/dev/null || cat /media/usb0/DoveOS-v2/home/desktop-resolution.txt 2>/dev/null || :)" = 960x540 ];then screen=960x540;fi
Q=/tmp/doveos-gpu-540p
case "${1:-}" in
 prepare)
  test -c /dev/mali || { echo GPU_MALI_DEVICE_UNAVAILABLE;exit 1; }
  # Local boot must not depend on the later debug Ethernet interface. This
  # adapter targets the verified stock Lap Dock firmware build. Remote command
  # launchers independently retain token authentication and dock MAC checks.
  test "$(sha256sum /usr/local/bin/IMS_Modules/wfd_dfb | cut -d' ' -f1)" = cb3b26154ce5f07113c614a2d7f03c59ac113333340f96966c31f9e7cdb295c2 || { echo GPU_FIRMWARE_BUILD_UNSUPPORTED;exit 1; }
  (cd "$B";sha256sum -c runtime.sha256)
  expected=$(awk '{print $1}' "$B/runtime.sha256")
  if [ -r "$B/gpu-assets.sh" ] && /bin/sh "$B/gpu-assets.sh" "$expected" > /tmp/doveos-gpu-assets.log 2>&1;then
   echo GPU_STATIC_FILES_USB_BACKED
  else
   # Detach only our known alias; a foreign symlink must never be unpacked into.
   if [ -L "$P" ];then
    test "$(readlink "$P")" = /tmp/doveos-gpu-assets || exit 4
    rm "$P"
   fi
   if [ ! -d "$P" ] && [ -d "$P.ram-rollback" ];then mv "$P.ram-rollback" "$P";fi
   mkdir -p "$P"
   echo GPU_STATIC_FILES_RAM_FALLBACK
  fi
  if [ "$(cat "$P/installed.sha256" 2>/dev/null || :)" != "$expected" ];then
   gunzip -c "$B/runtime.tar.gz" | tar -xf - -C "$P"
   (cd "$P";sha256sum -c files.sha256)
   chmod 700 "$P/Xephyr-dove" "$P/gpu-input"
   echo "$expected" > "$P/installed.sha256"
  fi
  # The shared theme owns window decorations on CPU and GPU desktops.
  # Keep the verified GPU runtime JWM only as the theme-less fallback.
  # Rebind after choosing the source: replacing a file can change its inode.
  wm_source="$P/jwm"
  if [ -x /tmp/doveos-theme/jwm ];then wm_source=/tmp/doveos-theme/jwm;fi
  umount "$M/usr/bin/jwm" 2>/dev/null || :
  mount --bind "$wm_source" "$M/usr/bin/jwm"
  cmp "$wm_source" "$M/usr/bin/jwm"
  echo "GPU_WINDOW_MANAGER_SOURCE=$wm_source"
  # Outline resizing grabs the X server and uses XOR readback on GLES2.
  # Use live resizing on the GPU desktop; the CPU theme retains its setting.
  sed 's/<ResizeMode>outline<\/ResizeMode>/<ResizeMode>opaque<\/ResizeMode>/' "$M/etc/doveos/jwmrc" > "$M/etc/doveos/jwmrc.gpu-new"
  mv "$M/etc/doveos/jwmrc.gpu-new" "$M/etc/doveos/jwmrc"
  rm -f /tmp/doveos-rgb-active
  if [ "$screen" = 1280x720 ] && { [ -f /tmp/doveos-rgb-trial ] || [ "$(cat /media/usb0/DoveOS-v2/home/desktop-presenter.txt 2>/dev/null || :)" = rgb ]; };then
   /bin/sh /media/usb0/DoveOS-v2/GPU/rgb/rgb-host.sh prepare
   touch /tmp/doveos-rgb-active
  fi
  ;;
 server)
  if [ -f /tmp/doveos-rgb-active ];then exec /bin/sh /media/usb0/DoveOS-v2/GPU/rgb/rgb-host.sh server;fi
  test ! -e /tmp/doveos-direct-display
  E=$P/libEGL-direct.so;V=$P/libgpu-present.so
  if [ "$screen" = 960x540 ];then
   (cd "$B/540p";sha256sum -c runtime.sha256)
   mkdir -p "$Q"
   gunzip -c "$B/540p/runtime.tar.gz" | tar -xf - -C "$Q"
   (cd "$Q";sha256sum -c files.sha256)
   chmod 700 "$Q/gpu-input" "$Q/gpu-cursor"
   E=$Q/libEGL-direct.so;V=$Q/libgpu-present.so
  fi
  exec chroot "$M" /usr/bin/env DISPLAY=:6 HOME=/root \
   LD_LIBRARY_PATH="$P:$P/usr/lib/arm-linux-gnueabihf:$P/lib/arm-linux-gnueabihf:/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf" \
   LD_PRELOAD="$P/gpu-dma-shim.so" DOVE_VOUT_LIBRARY="$P/libVOutWrapper.so" DOVE_ION_LIBRARY="$P/libVOutAllocator.so" \
   DOVE_EGL_MALI_LIBRARY="$P/libMali-r4p1-native-trial.so" LIBGL_GLES="$P/libMali-r4p1-native-trial.so" \
   LIBGL_EGL="$E" DOVE_DIRECT_PRESENTER="$V" \
   DOVE_SMALL_PBUFFER=1 DOVE_GPU_DESKTOP=1 DOVE_DIRECT_TEXTURE=1 LIBGL_DEEPBIND=0 LIBGL_FB=0 LIBGL_ES=2 LIBGL_NOPSA=1 LIBGL_BLITFULLSCREEN=0 DOVE_EGL_DAMAGE=1 DOVE_BIND_PBUFFER=1 \
   "$P/Xephyr-dove" :7 -screen "$screen" -dpi 100 -fullscreen -sw-cursor -glamor -nolisten tcp -ac -noreset
  ;;
 input)
  I=$P/gpu-input;[ "$screen" != 960x540 ] || I=$Q/gpu-input
  exec "$M/lib/ld-linux-armhf.so.3" --library-path "$M/usr/lib/arm-linux-gnueabihf:$M/lib/arm-linux-gnueabihf:/tmp/drmosa-usb-menu-runtime/lib:/usr/lib:/lib" "$I"
  ;;
 cursor)
  if [ -f /tmp/doveos-rgb-active ];then exec /bin/sh /media/usb0/DoveOS-v2/GPU/rgb/rgb-host.sh cursor;fi
  C=$P/gpu-cursor;[ "$screen" != 960x540 ] || C=$Q/gpu-cursor
  if [ "$screen" = 1280x720 ] && [ -x /tmp/doveos-cursors/gpu-cursor ];then C=/tmp/doveos-cursors/gpu-cursor;fi
  exec chroot "$M" /usr/bin/env DISPLAY=:7 HOME=/root "$C" "$(cat /tmp/doveos-gpu-desktop.pid)"
  ;;
 *) exit 2;;
esac
