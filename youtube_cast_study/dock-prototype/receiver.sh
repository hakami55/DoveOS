#!/bin/sh
# Manual standalone receiver preview. No boot hooks or PC media relay.
set -eu
exec 2>&1
B=/media/usb0/DoveOS-v2/YouTubeTrial
Q=/tmp/doveos-youtube-live
R=/tmp/doveos-youtube-probe
M=/tmp/doveos-v2-root
mkdir -p "$Q"
exec > "$Q/bootstrap.log" 2>&1
case "${1:-start}" in
 stop) touch "$Q/stop";exit 0;;
 start) ;;
 *) exit 2;;
esac
mkdir "$Q/receiver.lock" 2>/dev/null || { echo 'YouTube receiver is already running.';exit 2; }
echo $$ > "$Q/receiver.lock/pid"
app=
cleanup(){
 trap - EXIT INT TERM
 touch "$Q/stop"
 [ -z "$app" ] || { kill -TERM "$app" 2>/dev/null || :;wait "$app" 2>/dev/null || :; }
 rm -f "$Q/receiver.lock/pid";rmdir "$Q/receiver.lock" 2>/dev/null || :
}
trap cleanup EXIT
trap 'exit 143' INT TERM
(cd "$B";sha256sum -c receiver.sha256) >/dev/null
sh /media/usb0/DoveOS-v2/root-mount-guard.sh /media/usb0/DoveOS-v2 "$R" "$B/runtime-probe.ext2"
if [ ! -e /tmp/doveos-youtube-runtime-verified ];then
 test "$(sha256sum "$B/runtime-probe.ext2" | cut -d ' ' -f1)" = 4f407cec9874ae7cd1d97a2a82ed64d4dfaaf4809cc4d57efe59a3404630358a
 touch /tmp/doveos-youtube-runtime-verified
fi
for file in native-receiver native-player media-source vision-source safe-diagnostics;do cp "$B/$file.mjs" "$Q/$file.mjs";done
[ "$(readlink "$Q/node_modules" 2>/dev/null || :)" = "$R/node_modules" ] || ln -s "$R/node_modules" "$Q/node_modules"
gunzip -c "$B/yt-lite.tar.gz" | tar -xf - -C "$Q"
rm -f "$Q/stop" "$Q/pair"
"$M/lib/ld-linux-armhf.so.3" --library-path "$R/lib:$M/usr/lib/arm-linux-gnueabihf:$M/lib/arm-linux-gnueabihf" \
 "$R/node" --expose-gc --max-old-space-size=32 --max-semi-space-size=1 "$Q/native-receiver.mjs" > "$Q/receiver.log" 2>&1 &
app=$!
wait "$app"
