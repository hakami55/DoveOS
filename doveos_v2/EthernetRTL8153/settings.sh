#!/bin/sh
# Settings actions for the temporary USB menu. Fixed actions only; no shell input.
PATH=/usr/bin:/bin:/usr/sbin:/sbin
export PATH
umask 077
ROOT=/media/usb0/DrMoSa
RUNTIME=/tmp/drmosa-usb-menu-runtime
RESULT=/tmp/drmosa-settings-result
say(){ printf '%s\n' "$1" > "$RESULT"; }
case "${1:-}" in
 wifi-scan|wifi-status|wifi-join|wifi-disconnect|boot)
  exec /bin/sh /tmp/drmosa-usb-menu-runtime/wifi.sh "$@"
  ;;
 wifi-reconnect)
  exec /bin/sh /tmp/drmosa-usb-menu-runtime/wifi.sh "$@"
  ;;
 bluetooth-start|bluetooth-scan|bluetooth-pair-le|bluetooth-pair-classic|bluetooth-connect|bluetooth-disconnect|bluetooth-audio-test|bluetooth-forget)
  exec /bin/sh "$ROOT/Bluetooth/bluetooth.sh" "$@"
  ;;
 ethernet-toggle)
  MARK=/media/usb0/DRMOSA_DISABLE_ETHERNET
  SERVICE=/usr/local/etc/drmosa-ethernet/service.sh
  RUN=/tmp/drmosa-ethernet
  if [ -e "$MARK" ];then
   [ -r "$SERVICE" ] || { say 'Ethernet service is unavailable.';exit 0; }
   rm -f "$MARK" || { say 'Could not enable Ethernet on USB.';exit 0; }
   sync
   n=0
   while [ -d "$RUN" ] && [ -f "$RUN/stop" ] && [ "$n" -lt 8 ];do sleep 1;n=$((n+1));done
   if [ ! -d "$RUN" ];then
    nohup /bin/sh "$SERVICE" > /tmp/doveos-ethernet.log 2>&1 < /dev/null &
   fi
   /bin/sh /media/usb0/DoveOS-v2/EthernetRTL8153/start.sh start || :
   n=0
   while [ ! -e /sys/class/net/axdebug0 ] && [ ! -e /sys/class/net/rtdebug0 ] && [ "$n" -lt 12 ];do sleep 1;n=$((n+1));done
   if [ -e /sys/class/net/axdebug0 ] || [ -e /sys/class/net/rtdebug0 ];then say 'Ethernet enabled. Cable link may take a moment.'
   else say 'Ethernet enabled, but the adapter did not start. Check cable and hub.';fi
  else
   : > "$MARK" || { say 'Could not save Ethernet setting on USB.';exit 0; }
   sync
   if [ -d "$RUN" ];then : > "$RUN/stop";fi
   /bin/sh /media/usb0/DoveOS-v2/EthernetRTL8153/start.sh stop || :
   n=0
   while { [ -e /sys/class/net/axdebug0 ] || [ -e /sys/class/net/rtdebug0 ]; } && [ "$n" -lt 8 ];do sleep 1;n=$((n+1));done
   if [ -e /sys/class/net/axdebug0 ] || [ -e /sys/class/net/rtdebug0 ];then say 'Ethernet is stopping; Wi-Fi stays available.'
   else say 'Ethernet off. Wi-Fi stays available.';fi
  fi
  ;;
 sound-set)
  value=${2:-}
  case "$value" in ''|*[!0-9]*) say 'Invalid level.';exit 1;;esac
  [ "$value" -le 100 ] || { say 'Level must be 0 to 100.';exit 1; }
  [ -x "$RUNTIME/doveos-control" ] || { say 'Sound control is unavailable.';exit 1; }
  file="$ROOT/sound-volume.txt";label='Dock volume'
  if "$RUNTIME/doveos-control" volume "$value" >/tmp/doveos-control.log 2>&1;then
   if printf '%s\n' "$value" > "$file.next" && mv "$file.next" "$file";then
    sync;say "$label: $value%"
   else rm -f "$file.next";say "$label changed, but could not save it on USB.";fi
  else say "$label control failed.";exit 1;fi
  ;;
 awake-toggle)
  BASE=/usr/local/etc/drmosa-ethernet
  FLAG="$BASE/keepawake-enabled"
  if [ ! -f "$FLAG" ] || ! (cd "$BASE" && sha256sum -c SHA256SUMS >/dev/null 2>&1);then
   say 'Keep-awake service is unavailable or failed verification.';exit 0
  fi
  if [ "$(cat "$FLAG" 2>/dev/null)" = 1 ]; then
   if printf '0\n' > "$FLAG.next" && mv "$FLAG.next" "$FLAG";then
    sync;say 'Idle sleep allowed again.'
   else rm -f "$FLAG.next";say 'Could not change keep-awake setting.';fi
  else
   if printf '1\n' > "$FLAG.next" && mv "$FLAG.next" "$FLAG";then
    sync;say 'Keep awake enabled with Ethernet. No full power-off block.'
   else rm -f "$FLAG.next";say 'Could not change keep-awake setting.';fi
  fi
  ;;
 volume-next)
  FILE="$ROOT/volume.txt"
  old=$(cat "$FILE" 2>/dev/null)
  case "$old" in
   0) new=25;;25) new=50;;50) new=70;;70) new=80;;80) new=90;;90) new=100;;100) new=0;;*) new=80;;
  esac
  if printf '%s\n' "$new" > "$FILE.next" && mv "$FILE.next" "$FILE";then
   say "Moonlight volume: $new% on next launch."
  else
   rm -f "$FILE.next";say 'Volume setting could not be saved to USB.'
  fi
  ;;
 xbox-toggle)
  XDIR=/tmp/drmosa-xow
  PIDFILE="$XDIR/pid"
  pid=$(cat "$PIDFILE" 2>/dev/null)
  case "$pid" in ''|*[!0-9]*) pid=0;;esac
  if [ "$pid" -gt 1 ] && kill -0 "$pid" 2>/dev/null &&
     tr '\000' ' ' < "/proc/$pid/cmdline" 2>/dev/null | grep -qF "$XDIR/xow";then
   kill -TERM "$pid" 2>/dev/null
   rm -f "$PIDFILE"
   say 'Xbox wireless driver stopped.'
   exit 0
  fi
  [ -r "$ROOT/xow/xow" ] && [ -r "$ROOT/xow/firmware.bin" ] &&
   [ -r "$ROOT/xow/libstdc++.so.6" ] || { say 'Xbox driver files are missing from USB.';exit 0; }
  mkdir -p "$XDIR" || { say 'Xbox runtime directory unavailable.';exit 0; }
  for file in xow firmware.bin libstdc++.so.6;do
   cp "$ROOT/xow/$file" "$XDIR/$file" || { say 'Could not load Xbox driver into RAM.';exit 0; }
  done
  chmod 700 "$XDIR/xow"
  (
   cd "$XDIR" || exit 1
   nohup "$RUNTIME/lib/ld-linux-armhf.so.3" \
    --library-path "$XDIR:$RUNTIME/lib" "$XDIR/xow" \
    > "$XDIR/xow.log" 2>&1 < /dev/null &
   echo $! > "$PIDFILE"
  )
  sleep 2
  pid=$(cat "$PIDFILE" 2>/dev/null)
  if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null;then
   (sleep 5; kill -USR1 "$pid" 2>/dev/null) >/dev/null 2>&1 &
   say 'Xbox driver started. Pair the controller with the dongle.'
  else
   say 'Xbox driver did not start. Check USB adapter and log.'
  fi
  ;;
 *) say 'Unknown Settings action.';;
esac
exit 0
