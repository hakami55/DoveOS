#!/bin/sh
# Saved BLE keyboard reconnect using the verified ATT layout; no UHID.
set -eu
umask 077
U=/media/usb0/DrMoSa/Bluetooth
B=/tmp/doveos-bluetooth
H=$B/keyboard-hid
M=/tmp/doveos-v2-root
RESULT=${DOVEOS_HID_RESULT:-$B/result}
. "$U/bluetooth-radio.sh"
trap radio_release 0
trap 'exit 143' INT TERM HUP
address=${2:-}
printf '%s\n' "$address" | grep -Eq '^[[:xdigit:]]{2}(:[[:xdigit:]]{2}){5}$' || exit 2
case "${1:-}" in start|stop) ;; *) exit 2;; esac
mkdir -p "$H"
if [ -r "$H/keyboard.pid" ];then
 old=$(cat "$H/keyboard.pid")
 case "$old" in ''|*[!0-9]*) ;; *)
  if [ -r "/proc/$old/cmdline" ];then
   cmd=$(tr '\000' ' ' <"/proc/$old/cmdline")
   case "$cmd" in *"$H/keyboard-session.sh "*)
    if [ "$1" = stop ];then case "$cmd" in *"$H/keyboard-session.sh $address"*) ;; *) exit 0;; esac;fi
    if [ "$1" = start ] && [ -f "$H/ready" ];then
     case "$cmd" in *"$H/keyboard-session.sh $address "*)
      if /usr/local/bin/bluetooth/hcitool con | grep -F "$address" | grep -Eq 'state 1([[:space:]]|$)';then
       echo 'Bluetooth keyboard connected. Desktop input ready.' >"$RESULT";exit 0
      fi;; esac
    fi
    kill -TERM "$old"
    for n in 1 2 3;do kill -0 "$old" 2>/dev/null || break;sleep 1;done
    if kill -0 "$old" 2>/dev/null;then echo 'The previous keyboard session is still stopping.' >"$RESULT";exit 1;fi;;
   esac
  fi;;
 esac
fi
[ "$1" != stop ] || exit 0
rm -f "$H/ready" "$H/state"
for f in keyboard-input line-buffer.so bounded-run keyboard-session.sh mouse-encrypt keyboard-gatt;do
 [ -r "$U/$f" ] || { echo 'The saved Bluetooth keyboard helper is missing.' >"$RESULT";exit 1; }
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
 if dbus-send --system --print-reply --reply-timeout=1000 --dest=org.bluez / org.freedesktop.DBus.ObjectManager.GetManagedObjects 2>/dev/null | grep -Fq "object path \"/org/bluez/hci0/dev_$device/service";then mode=dbus;break;fi
 sleep 1
done
if [ "$mode" = native ];then
if ! "$M/lib/ld-linux-armhf.so.3" --library-path "$M/usr/lib/arm-linux-gnueabihf:$M/lib/arm-linux-gnueabihf" "$H/mouse-encrypt" "$address" >"$H/encryption.log" 2>&1;then
 echo 'Could not restore the saved keyboard encryption. Wake it and retry Reconnect.' >"$RESULT";exit 1
fi
radio_release
"$H/bounded-run" 8 /usr/bin/gatttool -i hci0 -b "$address" -t public -l medium --char-read -a 0x001d >"$H/map-reply.log" 2>"$H/map-error.log" || :
map=$(sed -n 's/^Characteristic value\/descriptor: //p' "$H/map-reply.log" | tr -d '[:space:]')
if [ -z "$map" ];then echo 'Keyboard did not answer. Wake it on the saved Bluetooth channel and retry.' >"$RESULT";exit 1;fi
if [ "$map" != 05010906a1018502050719e029e71500250175019508810295017508810195057501050819012905910295017503910195067508150026ff000507190029ff8100c0050c0901a1018503150026ff0319002aff03751095018100c005010980a101850415002501750195030981098209838102950175058103c005010906a101850505071500250119002977957875018102c0050c0901a10185061500250175019503810175019501050c09b8810206ff0009038102750195038101c005010902a10185090901a100050919012908150025019508750181020501093009311601f826ff07750c9502810609381581257f750895018106050c0a38028106c0c0 ];then echo 'This keyboard layout is not supported by the verified input helper.' >"$RESULT";exit 1;fi
reference=$("$H/bounded-run" 8 /usr/bin/gatttool -i hci0 -b "$address" -t public -l medium --char-read -a 0x0021 2>"$H/reference-error.log" | sed -n 's/^Characteristic value\/descriptor: //p' | tr -d '[:space:]')
[ "$reference" = 0201 ] || { echo 'The keyboard input reference was not confirmed. Wake it and retry.' >"$RESULT";exit 1; }
fi
radio_release
nohup /bin/sh "$H/keyboard-session.sh" "$address" "$mode" >"$H/session.log" 2>&1 </dev/null &
helper=$!;echo "$helper" >"$H/keyboard.pid"
for n in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25;do
 if [ -f "$H/ready" ] && kill -0 "$helper" 2>/dev/null;then
  if ! grep -Fxq "$address" "$U/keyboard-devices.txt" 2>/dev/null;then
   if { [ ! -r "$U/keyboard-devices.txt" ] || cat "$U/keyboard-devices.txt";printf '%s\n' "$address"; } >"$U/keyboard-devices.txt.new";then
    mv "$U/keyboard-devices.txt.new" "$U/keyboard-devices.txt"
   fi
  fi
  echo 'Bluetooth keyboard connected. Desktop input ready.' >"$RESULT";exit 0
 fi
 kill -0 "$helper" 2>/dev/null || break
 sleep 1
done
if [ -r "/proc/$helper/cmdline" ] && tr '\000' ' ' <"/proc/$helper/cmdline" | grep -Fq "$H/keyboard-session.sh $address";then kill -TERM "$helper" 2>/dev/null || :;fi
if [ -r "$H/state" ];then cp "$H/state" "$RESULT";else echo 'Keyboard input did not start. Wake it and try Reconnect.' >"$RESULT";fi
exit 1
