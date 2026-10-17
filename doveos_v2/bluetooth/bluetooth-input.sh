#!/bin/sh
# Saved BLE mouse reconnect using the verified ATT layout; no UHID.
set -eu
umask 077
U=/media/usb0/DrMoSa/Bluetooth
B=/tmp/doveos-bluetooth
H=$B/hid
M=/tmp/doveos-v2-root
RESULT=${DOVEOS_HID_RESULT:-$B/result}
. "$U/bluetooth-radio.sh"
trap radio_release 0
trap 'exit 143' INT TERM HUP
address=${2:-}
printf '%s\n' "$address" | grep -Eq '^[[:xdigit:]]{2}(:[[:xdigit:]]{2}){5}$' || exit 2
case "${1:-}" in start|stop) ;; *) exit 2;; esac
mkdir -p "$H"
if [ -r "$H/mouse.pid" ];then
 old=$(cat "$H/mouse.pid")
 case "$old" in ''|*[!0-9]*) ;; *)
  if [ -r "/proc/$old/cmdline" ];then
   cmd=$(tr '\000' ' ' <"/proc/$old/cmdline")
   case "$cmd" in *"$H/mouse-session.sh "*)
    if [ "$1" = stop ];then case "$cmd" in *"$H/mouse-session.sh $address"*) ;; *) exit 0;; esac;fi
    kill -TERM "$old"
    for n in 1 2 3;do kill -0 "$old" 2>/dev/null || break;sleep 1;done
    if kill -0 "$old" 2>/dev/null;then echo 'The previous mouse session is still stopping.' >"$RESULT";exit 1;fi;;
   esac
  fi;;
 esac
fi
[ "$1" != stop ] || exit 0
rm -f "$H/ready" "$H/state"
for f in mouse-input line-buffer.so bounded-run mouse-session.sh mouse-encrypt mouse-gatt;do
 [ -r "$U/$f" ] || { echo 'The saved Bluetooth mouse helper is missing.' >"$RESULT";exit 1; }
 cp "$U/$f" "$H/$f.new";chmod 700 "$H/$f.new";mv "$H/$f.new" "$H/$f"
done
if ! radio_acquire 30;then
 echo 'Bluetooth radio is busy; connection will retry.' >"$RESULT";exit 1
fi
if ! radio_connected "$address";then
 DOVEOS_RADIO_LE_PENDING=1
 DOVEOS_RADIO_LE_ADDRESS=$address
 DOVEOS_RADIO_LOG="$H/native-connect.log"
 "$H/bounded-run" 12 /usr/local/bin/bluetooth/hcitool lecc "$address" >"$H/native-connect.log" 2>&1 || :
fi
radio_cancel_pending
mode=native
device=$(printf '%s' "$address" | tr ':' '_' | tr 'a-f' 'A-F')
for attempt in 1 2 3;do
 if dbus-send --system --print-reply --reply-timeout=1000 --dest=org.bluez "/org/bluez/hci0/dev_$device" org.freedesktop.DBus.Properties.Get string:org.bluez.Device1 string:ServicesResolved 2>/dev/null | grep -q 'boolean true';then mode=dbus;break;fi
 sleep 1
done
if [ "$mode" = native ];then
if ! "$M/lib/ld-linux-armhf.so.3" --library-path "$M/usr/lib/arm-linux-gnueabihf:$M/lib/arm-linux-gnueabihf" "$H/mouse-encrypt" "$address" >"$H/encryption.log" 2>&1;then
 echo 'Could not restore the saved mouse encryption. Wake it and retry Reconnect.' >"$RESULT";exit 1
fi
radio_release
"$H/bounded-run" 8 /usr/bin/gatttool -i hci0 -b "$address" -t public -l medium --char-read -a 0x0020 >"$H/map-reply.log" 2>"$H/map-error.log" || :
map=$(sed -n 's/^Characteristic value\/descriptor: //p' "$H/map-reply.log" | tr -d '[:space:]')
if [ -z "$map" ];then echo 'Mouse did not answer. Wake it on the saved Bluetooth channel and retry.' >"$RESULT";exit 1;fi
if [ "$map" != 05010902a10185010901a100050919012908150025019508750181020501093009311601f826ff07750c9502810609381581257f750895018106050c0a38028106c0c0050c0901a1018503150026ff0319002aff03751095018100c0 ];then echo 'This mouse layout is not supported by the verified input helper.' >"$RESULT";exit 1;fi
reference=$("$H/bounded-run" 8 /usr/bin/gatttool -i hci0 -b "$address" -t public -l medium --char-read -a 0x0024 2>"$H/reference-error.log" | sed -n 's/^Characteristic value\/descriptor: //p' | tr -d '[:space:]')
[ "$reference" = 0101 ] || { echo 'The mouse input reference was not confirmed. Wake it and retry.' >"$RESULT";exit 1; }
fi
radio_release
nohup /bin/sh "$H/mouse-session.sh" "$address" "$mode" >"$H/session.log" 2>&1 </dev/null &
helper=$!;echo "$helper" >"$H/mouse.pid"
for n in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25;do
 if [ -f "$H/ready" ] && kill -0 "$helper" 2>/dev/null;then
  echo 'Bluetooth mouse connected. Desktop input ready.' >"$RESULT";exit 0
 fi
 kill -0 "$helper" 2>/dev/null || break
 sleep 1
done
if [ -r "/proc/$helper/cmdline" ] && tr '\000' ' ' <"/proc/$helper/cmdline" | grep -Fq "$H/mouse-session.sh $address";then kill -TERM "$helper" 2>/dev/null || :;fi
if [ -r "$H/state" ];then cp "$H/state" "$RESULT";else echo 'Mouse input did not start. Wake it and try Reconnect.' >"$RESULT";fi
exit 1
