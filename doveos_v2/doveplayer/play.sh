#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2/DovePlayer
Q=/tmp/doveplayer
M=/tmp/doveos-v2-root
R=/tmp/doveplayer-runtime
mkdir -p "$Q"
mkdir "$Q/playing" 2>/dev/null || { echo 'DovePlayer is already playing.';exit 2; }
broker= audio= app=
cleanup(){
 trap - EXIT INT TERM
 [ -z "$app" ] || { kill -TERM "$app" 2>/dev/null || :;wait "$app" 2>/dev/null || :; }
 # The broker child owns the display buffers. Let it finish retirement first.
 for n in $(seq 1 15);do
  test ! -e "$Q/broker-child.pid" && break
  sleep 1
 done
 if [ -e "$Q/broker-child.pid" ];then
  echo 'DOVEPLAYER_BROKER_STILL_OWNS_BUFFERS - kept alive for safety'
  : > "$Q/retained"
 else
  [ -z "$broker" ] || { kill -TERM "$broker" 2>/dev/null || :;wait "$broker" 2>/dev/null || :; }
  rm -f "$Q/video.sock"
 fi
 [ -z "$audio" ] || { kill -TERM "$audio" 2>/dev/null || :;wait "$audio" 2>/dev/null || :; }
 rm -f "$Q/audio.sock" "$Q/core.pid"
 rmdir "$Q/playing" 2>/dev/null || :
}
trap cleanup EXIT
trap 'exit 143' INT TERM
test -f "$M/usr/bin/env"
test ! -e /tmp/doveos-direct-display
test ! -e "$Q/retained"
test -z "$(pidof vlc vlc-private mpv doveplayer-core || :)"
headroom=$(awk '/^MemFree:/{f=$2}/^Cached:/{c=$2}/^Shmem:/{s=$2}/^SReclaimable:/{r=$2}END{print f+c-s+r}' /proc/meminfo)
[ "$headroom" -ge 65536 ] || { echo 'Close other apps to free memory.';exit 2; }
sh "$B/setup.sh"
(cd "$B";sha256sum -c binaries.sha256) >/dev/null
cp "$B/doveplayer-core" "$B/doveplayer-broker" "$B/doveplayer-audio" "$Q/"
chmod 755 "$Q/doveplayer-core" "$Q/doveplayer-broker" "$Q/doveplayer-audio"
rm -f "$Q/video.sock" "$Q/audio.sock"
"$M/lib/ld-linux-armhf.so.3" --library-path "$M/usr/lib/arm-linux-gnueabihf:$M/lib/arm-linux-gnueabihf:/usr/lib:/lib" "$Q/doveplayer-broker" > "$Q/broker.log" 2>&1 &
broker=$!
/bin/sh /media/usb0/DoveOS-v2/audio-route.sh host "$Q/doveplayer-audio" > "$Q/audio.log" 2>&1 &
audio=$!
for n in 1 2 3 4 5;do test -S "$Q/video.sock" && test -S "$Q/audio.sock" && break;sleep 1;done
test -S "$Q/video.sock";test -S "$Q/audio.sock"
"$M/lib/ld-linux-armhf.so.3" --library-path "$R/lib:$M/usr/lib/arm-linux-gnueabihf:$M/lib/arm-linux-gnueabihf" "$Q/doveplayer-core" "$@" > "$Q/player.log" 2>&1 &
app=$!
result=0;wait "$app" || result=$?;app=
echo "DOVEPLAYER_EXIT=$result"
exit "$result"
