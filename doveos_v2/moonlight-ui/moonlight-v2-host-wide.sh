#!/bin/sh
set -u
ROOT=/media/usb0/DoveOS-v2
RUNTIME=/tmp/drmosa-usb-menu-runtime
LOG="$ROOT/moonlight.log"
test -x "$RUNTIME/dock-stream-hw" || { echo RUNTIME_MISSING > "$LOG"; exit 1; }
test -r "$RUNTIME/keys" || { echo PAIRING_KEYS_MISSING > "$LOG"; exit 1; }
host=169.254.244.136;width=1920;height=1080;fps=60;bitrate=8000;volume=70;pointer=60;app=881448767;codec=h264;info=0
if [ -r /tmp/drmosa-moonlight-session ];then
 read host width height fps bitrate volume pointer app codec info < /tmp/drmosa-moonlight-session
 rm -f /tmp/drmosa-moonlight-session
fi
case "$host" in ''|*[!0-9.]*) host=169.254.244.136;;esac
case "$width:$height:$fps" in 1920:1080:60|1920:1080:30|1280:720:60) ;; *) width=1920;height=1080;fps=60;;esac
case "$bitrate" in 3000|5000|8000|10000) ;; *) bitrate=8000;;esac
case "$volume" in 0|25|50|70|80|90|100) ;; *) volume=70;;esac
case "$pointer" in 40|60|80|100) ;; *) pointer=60;;esac
case "$app" in ''|*[!0-9]*) app=881448767;;esac
case "$info" in 1) ;; *) info=0;;esac
requested_codec=$codec
automatic=0
case "$codec" in auto-hevc) automatic=1;codec=hevc;;auto-h264) automatic=1;codec=h264;;hevc|h264) ;; *) requested_codec=h264;codec=h264;;esac
echo "STREAM host=$host ${width}x${height}@$fps bitrate=$bitrate requested_codec=$requested_codec codec=$codec" > "$LOG"
rm -f /tmp/drmosa-moonlight.stop
cp "$ROOT/moonlight-quit-combo" /tmp/doveos-v2-moonlight-quit-combo
chmod 700 /tmp/doveos-v2-moonlight-quit-combo
"$RUNTIME/lib/ld-linux-armhf.so.3" --library-path "$RUNTIME/lib" \
 /tmp/doveos-v2-moonlight-quit-combo >> "$LOG" 2>&1 &
monitor=$!
wide_reader=
power_guard=
POWER_LOG=/tmp/doveos-moonlight-wifi-power.log
stop_power_guard(){
 if [ -n "$power_guard" ];then
  kill -TERM "$power_guard" 2>/dev/null || :
  n=0;while kill -0 "$power_guard" 2>/dev/null && [ "$n" -lt 10 ];do n=$((n+1));sleep 1;done
  if kill -0 "$power_guard" 2>/dev/null;then echo WIFI_POWER_RESTORE_PENDING >> "$LOG";else wait "$power_guard" 2>/dev/null || :;fi
  cat "$POWER_LOG" >> "$LOG";power_guard=
 fi
}
stop_wide_reader(){
 if [ -n "$wide_reader" ];then kill "$wide_reader" 2>/dev/null || :;n=0;while kill -0 "$wide_reader" 2>/dev/null && [ "$n" -lt 3 ];do n=$((n+1));sleep 1;done;kill -9 "$wide_reader" 2>/dev/null || :;wait "$wide_reader" 2>/dev/null || :;wide_reader=;fi
}
trap 'stop_wide_reader;stop_power_guard' EXIT
if /bin/busybox ip route get "$host" 2>/dev/null | grep -q 'dev wlan0' &&
 [ -r "$ROOT/Moonlight/wifi-stream-power" ] &&
 (cd "$ROOT/Moonlight";sha256sum -c wifi-power.sha256) >> "$LOG" 2>&1;then
 cp "$ROOT/Moonlight/wifi-stream-power" /tmp/doveos-moonlight-wifi-power
 chmod 700 /tmp/doveos-moonlight-wifi-power
 "$RUNTIME/lib/ld-linux-armhf.so.3" --library-path "$RUNTIME/lib" /tmp/doveos-moonlight-wifi-power --guard "$$" > "$POWER_LOG" 2>&1 &
 power_guard=$!
 n=0;while kill -0 "$power_guard" 2>/dev/null && ! grep -q '^PS_READY=1$' "$POWER_LOG" && [ "$n" -lt 3 ];do n=$((n+1));sleep 1;done
 echo WIFI_STREAM_POWER_GUARD >> "$LOG"
 cat "$POWER_LOG" >> "$LOG"
fi
stream=./dock-stream-hw
if [ -r "$ROOT/Moonlight/dock-stream-wide" ] && [ -r "$ROOT/clickpad-reader" ] && (cd "$ROOT/Moonlight";sha256sum -c wide.sha256) >> "$LOG" 2>&1;then
 cp "$ROOT/Moonlight/dock-stream-wide" /tmp/doveos-moonlight-ui/dock-stream-wide
 cp "$ROOT/clickpad-reader" /tmp/doveos-moonlight-clickpad-reader
 chmod 700 /tmp/doveos-moonlight-ui/dock-stream-wide /tmp/doveos-moonlight-clickpad-reader
 M=/tmp/doveos-v2-root
 "$RUNTIME/lib/ld-linux-armhf.so.3" --library-path "$RUNTIME/lib" /tmp/doveos-moonlight-clickpad-reader service > /tmp/doveos-moonlight-clickpad.log 2>&1 &
 wide_reader=$!
 stream=/tmp/doveos-moonlight-ui/dock-stream-wide
fi
attempt=0;played=0
while :;do
 echo "CODEC_ATTEMPT=$codec" >> "$LOG"
 (cd "$RUNTIME" || exit 1
  DRMOSA_STREAM_INFO="$info" DRMOSA_VIDEO_CODEC="$codec" DRMOSA_POINTER_PERCENT="$pointer" "$RUNTIME/lib/ld-linux-armhf.so.3" \
    --library-path "$RUNTIME/lib" "$stream" "$host" ./keys 1800 \
    "$width" "$height" "$fps" "$bitrate" 0 "$volume" "$app") >> "$LOG" 2>&1
 code=$?
 # Inspect each attempt and remember if HEVC ever displayed a picture.
 # Do not switch after a user exit or a later playback/network failure.
 displayed=$(awk '/^CODEC_ATTEMPT=/{seen=0} /^HEVC_DISPLAYED=/{split($0,a,"=");if(a[2]+0>0)seen=1} /^(RESULT |HEVC_SUMMARY)/{for(i=1;i<=NF;i++)if($i ~ /^displayed=/){split($i,a,"=");if(a[2]+0>0)seen=1}} END{print seen+0}' "$LOG")
 [ "$displayed" -eq 0 ] || played=1
 if [ "$codec" = hevc ] && [ "$code" -ne 0 ] && { [ "$code" -ne 25 ] || [ "$attempt" -eq 1 ]; } && [ "$played" -eq 0 ] && [ ! -e /tmp/drmosa-moonlight.stop ] && { [ "$automatic" -eq 1 ] || { grep -q '^HEVC_CORE_BUSY=1$' "$LOG" && grep -q '^VIDEO_OPEN_FAILED$' "$LOG"; }; };then
  echo 'HEVC_STARTUP_UNAVAILABLE: retrying once with H.264' >> "$LOG"
  codec=h264;attempt=0;sleep 1;continue
 fi
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
stop_power_guard
echo "STREAM_EXIT=$code" >> "$LOG"
exit "$code"
