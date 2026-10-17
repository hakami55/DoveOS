#!/bin/sh
set -eu
A=/tmp/doveos-bluealsa
B=/tmp/doveos-bluetooth
M=/tmp/doveos-v2-root
U=/media/usb0/DrMoSa/Bluetooth
mkdir -p "$A"
L=$A/lib:$M/usr/lib/arm-linux-gnueabihf:$M/lib/arm-linux-gnueabihf:$B/lib
cli(){ "$A/unblock-exec" "$B/lib/ld-linux-armhf.so.3" --library-path "$L" "$A/bluealsa-cli-debug" "$@"; }
owned_service(){
 [ -r "$A/service.pid" ] || return 1
 pid=$(cat "$A/service.pid")
 case "$pid" in ''|*[!0-9]*) return 1;; esac
 [ -r "/proc/$pid/cmdline" ] && tr '\000' ' ' <"/proc/$pid/cmdline" | grep -Fq "$A/bluealsa "
}
audio_owner(){
 dbus-send --system --print-reply --reply-timeout=1000 --dest=org.freedesktop.DBus / org.freedesktop.DBus.GetNameOwner string:org.bluealsa >/dev/null 2>&1
}
start(){
 if owned_service && audio_owner;then return 0;fi
 if owned_service;then
  kill "$pid" 2>/dev/null || :
  sleep 1
 fi
 rm -f "$A/service.pid"
 if [ ! -x "$A/bluealsa" ];then
  gunzip -c "$U/bluealsa-runtime.tar.gz" | tar -xf - -C "$A"
 fi
 for n in bluealsa-cli-debug unblock-exec bounded-run;do
  cp "$U/$n" "$A/$n.new";chmod 700 "$A/$n.new";mv "$A/$n.new" "$A/$n"
 done
 chmod 700 "$A/bluealsa"
 if ! mount | grep -q ' on /etc/dbus-1/system.d ';then
  mkdir -p "$A/system.d"
  cp -a /etc/dbus-1/system.d/. "$A/system.d/"
  chmod 755 "$A/system.d"
  mount --bind "$A/system.d" /etc/dbus-1/system.d
 fi
 # Input may have mounted another policy directory first. Install into the
 # active directory so startup does not depend on which profile starts first.
 P=/etc/dbus-1/system.d
 cat >"$P/doveos-bluealsa.conf.new" <<'XML'
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN" "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig><policy user="root"><allow own="org.bluealsa"/><allow send_destination="org.bluealsa"/><allow send_interface="org.bluez.MediaEndpoint1"/></policy></busconfig>
XML
 chmod 644 "$P/doveos-bluealsa.conf.new"
 mv "$P/doveos-bluealsa.conf.new" "$P/doveos-bluealsa.conf"
 dbus-send --system --print-reply --reply-timeout=3000 --dest=org.freedesktop.DBus / org.freedesktop.DBus.ReloadConfig >/dev/null
 nohup "$A/unblock-exec" "$B/lib/ld-linux-armhf.so.3" --library-path "$L" "$A/bluealsa" -p a2dp-source >"$A/service.log" 2>&1 </dev/null &
 echo $! >"$A/service.pid"
 for i in 1 2 3 4 5;do
  if owned_service && audio_owner;then return 0;fi
  sleep 1
 done
 echo 'Bluetooth audio service did not start.' >&2;return 1
}
case "${1:-start}" in
 start) start;;
 info|test)
  address=${2:-};printf '%s\n' "$address" | grep -Eq '^[[:xdigit:]]{2}(:[[:xdigit:]]{2}){5}$' || exit 2
  start
  mac=$(printf '%s' "$address" | tr ':' '_' | tr 'a-f' 'A-F')
  P=/org/bluealsa/hci0/dev_${mac}/a2dpsrc/sink
  if [ "$1" = info ];then cli info "$P";else
   "$A/bounded-run" 10 "$A/unblock-exec" "$B/lib/ld-linux-armhf.so.3" --library-path "$L" "$A/bluealsa-cli-debug" open "$P" <"$U/headphone-test.raw"
  fi;;
 stop)
  if owned_service;then kill "$pid" 2>/dev/null || :;fi
  rm -f "$A/service.pid";;
 *) exit 2;;
esac
