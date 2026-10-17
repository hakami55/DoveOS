#!/bin/sh
# DOVEOS_BOOT_TIMING_V1: uptime survives calendar-clock corrections.
boot_trace(){
 read dove_boot_up dove_boot_idle < /proc/uptime
 printf '%s %s\n' "$dove_boot_up" "$*" >> /media/usb0/DoveOS-v2/boot-timing.log 2>/dev/null || :
}
set -eu
ROOT=/media/usb0/DoveOS-v2
M=/tmp/doveos-v2-root
PID=/tmp/doveos-v2.pid
boot_trace "desktop_entry ${1:-start}"
case "${1:-start}" in
 stop) touch /tmp/doveos-v2-stop;exit 0;;
 status) cat "$ROOT/desktop.log" 2>/dev/null;exit 0;;
 start)
  test ! -e /tmp/gpu-rgb-retained || { echo RGB_RECOVERY_UNCONFIRMED_BUFFERS_RETAINED;exit 7; }
  if [ -f "$PID" ] && kill -0 "$(cat "$PID")" 2>/dev/null;then echo DOVEOS_V2_ALREADY_RUNNING;exit 0;fi
  test -f "$ROOT/doveos-v2.ext2"
  test -f "$ROOT/x11-bridge"
  cp "$ROOT/start-desktop.sh" /tmp/doveos-v2-start.sh
  nohup /bin/sh /tmp/doveos-v2-start.sh supervise > "$ROOT/desktop.log" 2>&1 </dev/null &
  echo $! > "$PID"
  echo DOVEOS_V2_STARTING
  exit 0;;
 supervise) ;;
 *) exit 2;;
esac

boot_trace old_menu_wait_begin
touch /tmp/drmosa-usb-menu.stop /tmp/drmosa-moonlight.stop
n=0
while [ -f /tmp/drmosa-usb-menu.pid ] && [ "$n" -lt 20 ];do
 old=$(cat /tmp/drmosa-usb-menu.pid)
 kill -0 "$old" 2>/dev/null || break
 n=$((n+1));sleep 1
done
if [ -f /tmp/drmosa-usb-menu.pid ];then
 old=$(cat /tmp/drmosa-usb-menu.pid)
 if kill -0 "$old" 2>/dev/null;then echo OLD_MENU_STILL_RUNNING;rm -f "$PID";exit 3;fi
fi
boot_trace old_menu_wait_end
rm -f /tmp/doveos-v2-stop /tmp/doveos-v2-action /tmp/doveos-vlc-opening
mkdir -p "$M" "$ROOT/home" /tmp/doveos-v2-x
rm -f /tmp/doveos-v2-x/Xvfb_screen0
rm -f /tmp/.X11-unix/X7
boot_trace root_mount_begin
# Verify backing device/inode, not only the pathname left by USB re-enumeration.
/bin/sh "$ROOT/root-mount-guard.sh"
boot_trace root_mount_end
if [ -r "$ROOT/Memory/swap-runtime.sh" ];then
 /bin/sh "$ROOT/Memory/swap-runtime.sh" start >> "$ROOT/Memory/swap.log" 2>&1 || echo DOVEOS_SWAP_UNAVAILABLE
fi
bridge= session= xserver= clickpad= power_manager= gpuserver= gcursor=
render=cpu
clear_transition(){
 C="$ROOT/Transitions/clear-transition"
 R=/tmp/drmosa-usb-menu-runtime
 if [ -r "$C" ] && (cd "$ROOT/Transitions";sha256sum -c files.sha256) >/dev/null 2>&1;then
  cp "$C" /tmp/doveos-clear-transition
  "$R/lib/ld-linux-armhf.so.3" --library-path "$R/lib" /tmp/doveos-clear-transition --clear || :
 fi
}
cleanup(){
 cleanup_status=$?
 trap - EXIT INT TERM
 action=$(cat /tmp/doveos-v2-action 2>/dev/null || :)
 read exit_uptime exit_idle < /proc/uptime
 {
  echo "DESKTOP_EXIT uptime=$exit_uptime renderer=$render action=$action shell_status=$cleanup_status"
  for owned in "$bridge" "$session" "$gpuserver" "$gcursor";do
   [ -z "$owned" ] || { echo "PROCESS pid=$owned";cat "/proc/$owned/stat" 2>/dev/null || :; }
  done
 } >> "$ROOT/session-exits.log"
 intentional_stop=0
 [ ! -f /tmp/doveos-v2-stop ] || intentional_stop=1
 if [ "$render" = gpu ];then
  cp "$ROOT/GPU/desktop/server.log" "$ROOT/GPU/desktop/last-session.log" 2>/dev/null || :
  if [ -z "$action" ] && [ "$intentional_stop" -eq 0 ];then
   touch /tmp/doveos-gpu-disabled;printf 'restart\n' > /tmp/doveos-v2-action
   echo GPU_UNEXPECTED_EXIT_CPU_FALLBACK
  fi
 fi
 if [ "$action" = shutdown ];then
  # Normal power-off failed its wake test on the stock Realtek firmware.
  # Recover the desktop if an old client still requests that action.
  echo SHUTDOWN_DISABLED_RESTARTING_DESKTOP
  printf 'restart\n' > /tmp/doveos-v2-action
 fi
 if [ -f "$ROOT/power-button-host.sh" ];then
  /bin/sh "$ROOT/power-button-host.sh" stop || :
  sleep 1
 fi
 if [ -n "$power_manager" ];then
  kill "$power_manager" 2>/dev/null || :
  ticks=0
  while kill -0 "$power_manager" 2>/dev/null && [ "$ticks" -lt 3 ];do sleep 1;ticks=$((ticks+1));done
  kill -9 "$power_manager" 2>/dev/null || :
  wait "$power_manager" 2>/dev/null || :
 fi
 rm -f /tmp/doveos-screen-blank /tmp/doveos-screen-dim /tmp/doveos-power-test
 if [ -f /tmp/doveos-clickpad-reader.pid ];then
  reader_pid=$(cat /tmp/doveos-clickpad-reader.pid)
  if [ -r "/proc/$reader_pid/cmdline" ] && tr '\000' ' ' < "/proc/$reader_pid/cmdline" | grep -q '/tmp/doveos-clickpad-reader service';then
   kill "$reader_pid" 2>/dev/null || :
  fi
 fi
 for log in desktop session bridge;do
  [ ! -f "$ROOT/$log.log" ] || cp "$ROOT/$log.log" "$ROOT/$log.previous.log"
 done
 for process in "$clickpad" "$bridge" "$session";do
  [ -z "$process" ] || kill "$process" 2>/dev/null || :
 done
 if [ -n "$gcursor" ];then
  kill "$gcursor" 2>/dev/null || :
  ticks=0
  while kill -0 "$gcursor" 2>/dev/null && [ "$ticks" -lt 3 ];do sleep 1;ticks=$((ticks+1));done
  kill -9 "$gcursor" 2>/dev/null || :;wait "$gcursor" 2>/dev/null || :
 fi
 if [ "$render" = gpu ];then clear_transition;fi
 if [ -n "$gpuserver" ];then
  if [ -f /tmp/doveos-rgb-active ];then
   if ! /bin/sh "$ROOT/GPU/rgb/rgb-host.sh" stop "$gpuserver";then
    echo RGB_RECOVERY_UNCONFIRMED_DESKTOP_STOPPED
    touch /tmp/doveos-gpu-disabled
    return
   fi
   wait "$gpuserver" 2>/dev/null || :
   rm -f /tmp/doveos-rgb-active
  else
  kill "$gpuserver" 2>/dev/null || :
  ticks=0
  while kill -0 "$gpuserver" 2>/dev/null && [ "$ticks" -lt 5 ];do sleep 1;ticks=$((ticks+1));done
  kill -9 "$gpuserver" 2>/dev/null || :
  wait "$gpuserver" 2>/dev/null || :
  fi
  rm -f /tmp/doveos-gpu-desktop.pid
 fi
 if [ -n "$xserver" ];then kill "$xserver" 2>/dev/null || :;fi
 umount "$M/tmp/doveos-gpu-assets" 2>/dev/null || :
 umount "$M/tmp/doveos-uvc-runtime" 2>/dev/null || :
 umount "$M/tmp/doveos-uvc-audio-runtime" 2>/dev/null || :
 if [ -n "$clickpad" ];then wait "$clickpad" 2>/dev/null || :;rm -f /tmp/doveos-clickpad-reader.pid;fi
 for directory in /proc/[0-9]*;do
  if [ "$(readlink "$directory/root" 2>/dev/null)" = "$M" ];then kill "${directory##*/}" 2>/dev/null || :;fi
 done
 sleep 1
 for directory in /proc/[0-9]*;do
  if [ "$(readlink "$directory/root" 2>/dev/null)" = "$M" ];then kill -9 "${directory##*/}" 2>/dev/null || :;fi
 done
 sleep 1
 umount "$M/tmp/doveos-static-runtime" 2>/dev/null || :
 for path in lib/arm-linux-gnueabihf/libz.so.1.2.11 usr/bin/jwm usr/bin/doveos-status usr/bin/doveos-settings-ui usr/bin/doveos-settings-proxy usr/bin/doveos-session etc/doveos root/Desktop root media/usb0 media tmp sys proc dev/pts dev;do
  # A previously interrupted cleanup may leave duplicate binds. Retire every
  # inactive layer at these exact owned targets; never force a busy unmount.
  layers=0
  while [ "$layers" -lt 8 ] && awk -v p="$M/$path" '$2==p{found=1}END{exit !found}' /proc/mounts;do
   umount "$M/$path" 2>/dev/null || break
   layers=$((layers+1))
  done
 done
 umount "$M" 2>/dev/null || :
 umount /tmp/doveos-static-runtime 2>/dev/null || :
 rm -f /tmp/doveos-v2-stop
 echo DOVEOS_V2_SESSION_ENDED
 action=$(cat /tmp/doveos-v2-action 2>/dev/null || :)
 rm -f /tmp/doveos-v2-action
 case "$action" in
 moonlight)
  /bin/sh "$ROOT/moonlight-v2-host.sh" || :
  rm -f "$PID"
  /bin/sh "$ROOT/start-desktop.sh" start;;
 dex)
  /bin/sh "$ROOT/dex-v2-host.sh" || :
  rm -f "$PID"
  /bin/sh "$ROOT/start-desktop.sh" start;;
 hdmi|media|games)
  /bin/sh "$ROOT/app-handoff-host.sh" "$action" || :
  rm -f "$PID"
  /bin/sh "$ROOT/start-desktop.sh" start;;
 restart)
  rm -f "$PID" /tmp/doveos-v2-recovery-count
  /bin/sh "$ROOT/start-desktop.sh" start;;
 shutdown)
  echo SHUTDOWN_DISABLED_RESTARTING_DESKTOP
  rm -f "$PID" /tmp/doveos-v2-recovery-count
  /bin/sh "$ROOT/start-desktop.sh" start;;
 legacy)
  rm -f "$PID"
  /bin/sh /media/usb0/autorun/01-drmosa-menu.sh || :;;
 *)
  rm -f "$PID"
  if [ "$intentional_stop" -eq 0 ];then
   attempts=$(cat /tmp/doveos-v2-recovery-count 2>/dev/null || echo 0)
   case "$attempts" in ''|*[!0-9]*) attempts=0;; esac
   if [ "$attempts" -lt 2 ];then
    attempts=$((attempts+1));echo "$attempts" > /tmp/doveos-v2-recovery-count
    echo "Unexpected desktop exit; recovering v2 ($attempts/2)" >> "$ROOT/recovery.log"
    sleep 2
    /bin/sh "$ROOT/start-desktop.sh" start
   else
    echo 'Repeated desktop failure; v2 stopped. See *.previous.log.' >> "$ROOT/recovery.log"
   fi
  fi;;
 esac
}
trap cleanup EXIT INT TERM
mount --bind /dev "$M/dev"
mount --bind /dev/pts "$M/dev/pts"
mount --bind /proc "$M/proc"
mount --bind /sys "$M/sys"
mount --bind /tmp "$M/tmp"
mount --bind /media "$M/media"
mount --bind /media/usb0 "$M/media/usb0"
mount --bind "$ROOT/home" "$M/root"
if [ -r "$ROOT/RuntimeFixes/apply.sh" ];then
 /bin/sh "$ROOT/RuntimeFixes/apply.sh" || echo DOVEOS_RUNTIME_LIBRARY_FIX_UNAVAILABLE
fi
boot_trace optional_apps_begin
if [ -f "$ROOT/optional-mount.sh" ];then
 /bin/sh "$ROOT/optional-mount.sh" || echo OPTIONAL_APPS_UNAVAILABLE
fi
boot_trace optional_apps_end
boot_trace theme_begin
if [ -f "$ROOT/apply-theme.sh" ];then
 /bin/sh "$ROOT/apply-theme.sh" || echo DOVEOS_THEME_UNAVAILABLE
fi
boot_trace theme_end
mkdir -p /tmp/doveos-v2-desktop "$M/root/Desktop"
mount --bind /tmp/doveos-v2-desktop "$M/root/Desktop"
host=7
if [ "$(cat "$ROOT/home/desktop-renderer.txt" 2>/dev/null || :)" = gpu ] && [ ! -f /tmp/doveos-gpu-disabled ];then
 if /bin/sh "$ROOT/GPU/desktop/gpu-desktop-host.sh" prepare > "$ROOT/GPU/desktop/prepare.log" 2>&1;then render=gpu;host=6;else echo GPU_PREPARE_FAILED_CPU_FALLBACK;fi
fi
rm -f /tmp/.X6-lock /tmp/.X7-lock /tmp/.X11-unix/X6 /tmp/.X11-unix/X7
screen=1280x720
if [ "$render" = gpu ];then
 screen=1280x720
if [ "$(cat /tmp/doveos-desktop-resolution-trial 2>/dev/null || cat /media/usb0/DoveOS-v2/home/desktop-resolution.txt 2>/dev/null || :)" = 960x540 ];then screen=960x540;fi
fi
chroot "$M" /usr/bin/env LD_LIBRARY_PATH=/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf /usr/bin/Xvfb :"$host" -screen 0 "${screen}x24" -fbdir /tmp/doveos-v2-x -nolisten tcp -ac -noreset -extension GLX > "$ROOT/xserver.log" 2>&1 &
xserver=$!
n=0
while [ ! -S /tmp/.X11-unix/X"$host" ] && [ "$n" -lt 15 ];do
 kill -0 "$xserver" 2>/dev/null || { echo XSERVER_FAILED;exit 4; }
 n=$((n+1));sleep 1
done
test -S /tmp/.X11-unix/X"$host"
# DOVEOS_ANIMATED_SPLASH_HANDOFF: stop drawing before the desktop owns fb0.
touch /tmp/doveos-splash-handoff
if [ -x /usr/local/etc/doveos-foreground-trial-20261003/splash-dots ];then
 /usr/local/etc/doveos-foreground-trial-20261003/splash-dots --stop || :
fi
boot_trace splash_handoff_complete
clear_transition
if [ "$render" = gpu ];then
 /bin/sh "$ROOT/GPU/desktop/gpu-desktop-host.sh" server > "$ROOT/GPU/desktop/server.log" 2>&1 &
 gpuserver=$!;echo "$gpuserver" > /tmp/doveos-gpu-desktop.pid
 n=0
 while [ "$n" -lt 15 ] && kill -0 "$gpuserver" 2>/dev/null;do
  if [ -S /tmp/.X11-unix/X7 ] && grep -q GPU_PRESENTER_READY "$ROOT/GPU/desktop/server.log";then break;fi
  n=$((n+1));sleep 1
 done
 if ! kill -0 "$gpuserver" 2>/dev/null || ! grep -q GPU_PRESENTER_READY "$ROOT/GPU/desktop/server.log";then
  touch /tmp/doveos-gpu-disabled;echo GPU_START_FAILED_CPU_FALLBACK;printf 'restart\n' > /tmp/doveos-v2-action;exit 1
 fi
 echo 'GPU accelerated X11 desktop (Mali GLES2)' > /tmp/doveos-render-status
 if [ -f /tmp/doveos-rgb-active ];then echo 'GPU accelerated RGB desktop (Mali GLES2, 720p)' > /tmp/doveos-render-status;fi
 rm -f /tmp/doveos-gpu-display-paused /tmp/doveos-gpu-cursor-paused
 # Clear readiness before the child can truncate or write its new session log.
 : > "$ROOT/GPU/desktop/cursor.log"
 /bin/sh "$ROOT/GPU/desktop/gpu-desktop-host.sh" cursor >> "$ROOT/GPU/desktop/cursor.log" 2>&1 &
 gcursor=$!
 n=0
 # The RGB cursor supervisor may wait eight seconds for its child to report
 # readiness. Give it time before treating startup as a GPU failure.
 while [ "$n" -lt 15 ] && kill -0 "$gcursor" 2>/dev/null;do
  grep -q GPU_CURSOR_OVERLAY_READY "$ROOT/GPU/desktop/cursor.log" && break
  sleep 1;n=$((n+1))
 done
 if ! kill -0 "$gcursor" 2>/dev/null || ! grep -q GPU_CURSOR_OVERLAY_READY "$ROOT/GPU/desktop/cursor.log";then
  touch /tmp/doveos-gpu-disabled;echo GPU_CURSOR_FAILED_CPU_FALLBACK;printf 'restart\n' > /tmp/doveos-v2-action;exit 1
 fi
fi
boot_trace xserver_ready
# DoveOS power-manager owns dim/off/wake. X's separate default ten-minute
# saver otherwise covers the desktop and makes app readback return black.
chroot "$M" /usr/bin/env DISPLAY=:7 /usr/bin/xset s off || echo X_SAVER_DISABLE_FAILED
chroot "$M" /usr/bin/env DISPLAY=:7 /usr/bin/xset s reset || :
chroot "$M" /bin/sh /usr/bin/doveos-session > "$ROOT/session.log" 2>&1 &
session=$!
boot_trace session_started
cp "$ROOT/x11-bridge" /tmp/doveos-v2-bridge
chmod 700 /tmp/doveos-v2-bridge
if [ "$render" = gpu ];then
 /bin/sh "$ROOT/GPU/desktop/gpu-desktop-host.sh" input > "$ROOT/bridge.log" 2>&1 &
 bridge=$!
else
 /bin/sh "$ROOT/gpu-render-host.sh" > "$ROOT/bridge.log" 2>&1 &
 bridge=$!
fi
# Read the pre-filter physical switch while this desktop owns the input path.
# If the reader stops, the bridge releases held buttons and uses native input.
if [ -f "$ROOT/clickpad-reader" ];then
 cp "$ROOT/clickpad-reader" /tmp/doveos-clickpad-reader
 chmod 700 /tmp/doveos-clickpad-reader
 "$M/lib/ld-linux-armhf.so.3" --library-path "$M/usr/lib/arm-linux-gnueabihf:$M/lib/arm-linux-gnueabihf" /tmp/doveos-clickpad-reader service > "$ROOT/clickpad.log" 2>&1 &
 clickpad=$!
 echo "$clickpad" > /tmp/doveos-clickpad-reader.pid
fi
boot_trace desktop_processes_started
echo DOVEOS_V2_DESKTOP_STARTED
if [ -f "$ROOT/power-manager" ];then
 cp "$ROOT/power-manager" /tmp/doveos-power-manager
 chmod 700 /tmp/doveos-power-manager
 "$M/lib/ld-linux-armhf.so.3" --library-path "$M/usr/lib/arm-linux-gnueabihf:$M/lib/arm-linux-gnueabihf" /tmp/doveos-power-manager > "$ROOT/power.log" 2>&1 &
 power_manager=$!
fi
if [ -f "$ROOT/power-button-host.sh" ];then
 /bin/sh "$ROOT/power-button-host.sh" start || echo POWER_BUTTON_SLEEP_UNAVAILABLE
fi
boot_trace desktop_supervision_started
healthy_ticks=0
while kill -0 "$bridge" 2>/dev/null && kill -0 "$session" 2>/dev/null && [ ! -f /tmp/doveos-v2-stop ];do
 if [ "$render" = gpu ];then
  free=$(awk '/MemFree:/{f=$2}/Buffers:/{b=$2}/^Cached:/{c=$2}/Shmem:/{s=$2}END{print f+b+(c>s?(c-s)/2:0)}' /proc/meminfo)
  cma=$(awk '$1=="nr_free_cma"{print $2*4}' /proc/vmstat)
  gpu=$(cat /sys/kernel/debug/mali/memory_usage 2>/dev/null || echo 0)
  gpu_failure=
  if ! kill -0 "$gpuserver" 2>/dev/null;then gpu_failure=server_exited
  elif ! kill -0 "$gcursor" 2>/dev/null;then gpu_failure=cursor_exited
  elif [ "$((free-cma))" -lt 16000 ];then gpu_failure=ordinary_memory_low
  elif [ "$gpu" -gt 48000000 ];then gpu_failure=mali_memory_limit
  fi
  if [ -n "$gpu_failure" ];then
   echo "GPU_SESSION_FAILED_CPU_FALLBACK reason=$gpu_failure ordinary_estimate_kb=$((free-cma)) mali_bytes=$gpu"
   touch /tmp/doveos-gpu-disabled;printf 'restart\n' > /tmp/doveos-v2-action;break
  fi
 fi
 # A newly enumerated USB may have the same path but a different device.
 # Restart only after its new mount/image is readable; normal cleanup removes
 # old chroot binds before the identity guard mounts the current image.
 if [ "$((healthy_ticks%5))" -eq 0 ];then
  current_usb=$(awk '$2=="/media/usb0"{device=$1}END{print device}' /proc/mounts)
  desktop_usb=$(awk -v point="$M/root" '$2==point{device=$1}END{print device}' /proc/mounts)
  if [ -n "$current_usb" ] && [ -n "$desktop_usb" ] && [ "$current_usb" != "$desktop_usb" ] && [ -r "$ROOT/doveos-v2.ext2" ];then
   echo "USB_REENUMERATED_DESKTOP_RESTART old=$desktop_usb current=$current_usb"
   printf 'restart\n' > /tmp/doveos-v2-action;break
  fi
 fi
 sleep 1;healthy_ticks=$((healthy_ticks+1))
 if [ "$healthy_ticks" -eq 15 ];then
  rm -f /tmp/doveos-v2-recovery-count
  if [ -r "$ROOT/boot-splash/rearm-after-desktop.sh" ];then
   /bin/sh "$ROOT/boot-splash/rearm-after-desktop.sh" --rearm > "$ROOT/boot-splash/rearm.log" 2>&1 || echo SPLASH_REARM_DEFERRED
  fi
 fi
done
