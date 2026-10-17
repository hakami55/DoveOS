#!/bin/sh
set -u
ROOT=/media/usb0/DoveOS-v2
RUNTIME=/tmp/drmosa-usb-menu-runtime
LOG="$ROOT/moonlight.log"
test -x "$RUNTIME/dock-stream-hw" || { echo RUNTIME_MISSING > "$LOG"; exit 1; }
test -r "$RUNTIME/keys" || { echo PAIRING_KEYS_MISSING > "$LOG"; exit 1; }
host=169.254.244.136;width=1920;height=1080;fps=60;bitrate=8000;volume=70;pointer=60;app=881448767;codec=h264
if [ -r /tmp/drmosa-moonlight-session ];then
 read host width height fps bitrate volume pointer app codec < /tmp/drmosa-moonlight-session
 rm -f /tmp/drmosa-moonlight-session
fi
case "$host" in ''|*[!0-9.]*) host=169.254.244.136;;esac
case "$width:$height:$fps" in 1920:1080:60|1920:1080:30|1280:720:60) ;; *) width=1920;height=1080;fps=60;;esac
case "$bitrate" in 3000|5000|8000|10000) ;; *) bitrate=8000;;esac
case "$volume" in 0|25|50|70|80|90|100) ;; *) volume=70;;esac
case "$pointer" in 40|60|80|100) ;; *) pointer=60;;esac
case "$app" in ''|*[!0-9]*) app=881448767;;esac
case "$codec" in hevc|h264) ;; *) codec=h264;;esac
echo "STREAM host=$host ${width}x${height}@$fps bitrate=$bitrate codec=$codec" > "$LOG"
rm -f /tmp/drmosa-moonlight.stop
cp "$ROOT/moonlight-quit-combo" /tmp/doveos-v2-moonlight-quit-combo
chmod 700 /tmp/doveos-v2-moonlight-quit-combo
"$RUNTIME/lib/ld-linux-armhf.so.3" --library-path "$RUNTIME/lib" \
 /tmp/doveos-v2-moonlight-quit-combo >> "$LOG" 2>&1 &
monitor=$!
attempt=0
while :;do
 (cd "$RUNTIME" || exit 1
  DRMOSA_VIDEO_CODEC="$codec" DRMOSA_POINTER_PERCENT="$pointer" "$RUNTIME/lib/ld-linux-armhf.so.3" \
    --library-path "$RUNTIME/lib" ./dock-stream-hw "$host" ./keys 1800 \
    "$width" "$height" "$fps" "$bitrate" 0 "$volume" "$app") >> "$LOG" 2>&1
 code=$?
 if [ "$code" -eq 25 ] && [ "$attempt" -eq 0 ] && [ ! -e /tmp/drmosa-moonlight.stop ];then
  attempt=1;sleep 1;continue
 fi
 break
done
kill "$monitor" 2>/dev/null || :
ticks=0
while kill -0 "$monitor" 2>/dev/null && [ "$ticks" -lt 3 ];do ticks=$((ticks+1));sleep 1;done
kill -9 "$monitor" 2>/dev/null || :
wait "$monitor" 2>/dev/null || :
rm -f /tmp/doveos-v2-moonlight-quit-combo
rm -f /tmp/drmosa-moonlight.stop
echo "STREAM_EXIT=$code" >> "$LOG"
exit "$code"
