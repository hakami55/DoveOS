#!/bin/sh
# Small, bounded sounds; no resident daemon and no hardware mixer changes.
set -eu
umask 077
B=/media/usb0/DoveOS-v2
T=/tmp/doveos-theme
F=$B/home/startup-sounds.txt
RESULT=/tmp/drmosa-settings-result
L=/tmp/doveos-system-sound.lock
action=${1:-};arg=${2:-}
valid_clip(){ case "$1" in dove-dawn|dove-wings|dove-nest) return 0;;*)return 1;;esac; }
valid_volume(){ case "$1" in ''|*[!0-9]*)return 1;;esac;test "$1" -le 100; }
load(){ enabled=1;clip=dove-dawn;gain=40;events=1
 if test -r "$F";then
  IFS=: read -r e c g s < "$F" || :
  if valid_clip "${c:-}" && valid_volume "${g:-}";then
   case "${e:-}" in 0|1) enabled=$e;;esac
   case "${s:-}" in 0|1) events=$s;;esac
   clip=$c;gain=$g
  fi
 fi
}
say(){ printf '%s\n' "$1" > "$RESULT"; }
if test "$action" = startup-sound-save;then
 IFS=: read -r enabled clip gain events <<EOF
$arg
EOF
 case "$enabled:$events" in 0:0|0:1|1:0|1:1) ;;*)say 'Invalid sound settings.';exit 2;;esac
 valid_clip "$clip" && valid_volume "$gain" || { say 'Invalid sound choice or volume.';exit 2; }
 test -f "$B/AUTO_START" || { say "DoveOS USB is not mounted.";exit 1; }
 mkdir -p "$B/home"
 printf '%s:%s:%s:%s\n' "$enabled" "$clip" "$gain" "$events" > "$F.next"
 mv "$F.next" "$F";say 'Sound settings saved. Startup music plays once after the next full boot.';exit 0
fi
load
case "$action" in
 startup-sound-boot)
  mkdir /tmp/doveos-startup-sound-attempted 2>/dev/null || exit 0
  test "$enabled" = 1 || exit 0
  ready=0
  for n in $(seq 1 20);do
   if pidof jwm >/dev/null && pidof pcmanfm >/dev/null;then ready=1;break;fi
   sleep 1
  done
  test "$ready" = 1 || exit 0
  sleep 1;;
 startup-sound-preview)
  IFS=: read -r clip gain <<EOF
$arg
EOF
  valid_clip "$clip" && valid_volume "$gain" || { say 'Invalid sound preview.';exit 2; };;
 system-sound-preview|system-sound-event)
  case "$arg" in information|warning|question|error) ;;*)exit 2;;esac
  test "$action" != system-sound-event || test "$events" = 1 || exit 0
  clip=dove-$arg;gain=65;;
 *)exit 2;;
esac
# Respect global mute, audio tests, active playback and direct-display streams.
if test "$(cat /media/usb0/DrMoSa/sound-volume.txt 2>/dev/null || echo 70)" = 0;then
 case "$action" in *preview)say 'Sound is muted. Unmute it in Volume to preview.';;esac;exit 0
fi
if test -d /tmp/doveos-settings-sound-test.lock || test -e /tmp/doveos-direct-display || test -n "$(pidof aplay arecord vlc vlc-private || :)";then
 case "$action" in *preview)say 'Audio is busy. Stop playback or recording before previewing.';;esac;exit 0
fi
for card in /proc/asound/card[0-9]*;do
 test "$(cat "$card/id" 2>/dev/null)" = RTKAlsaCard || continue
 for status in "$card"/pcm0p/sub*/status;do
  if grep -q '^state: RUNNING' "$status" 2>/dev/null;then exit 0;fi
 done
done
mkdir "$L" 2>/dev/null || exit 0
app=
cleanup(){ trap - EXIT INT TERM;test -z "$app" || { kill -TERM "$app" 2>/dev/null || :;sleep 1;kill -KILL "$app" 2>/dev/null || :;wait "$app" 2>/dev/null || :; };rm -f "$L/clip.wav";rmdir "$L" 2>/dev/null || :; }
trap cleanup EXIT
trap 'exit 0' INT TERM
file=$B/StartupSounds/clips/$clip.wav
test -r "$file" && test -x "$T/scale-wave" && test -x /usr/bin/aplay || { case "$action" in *preview)say 'Sound files or playback helper are unavailable.';;esac;exit 1; }
"$T/scale-wave" "$file" "$gain" > "$L/clip.wav"
/usr/bin/aplay -N -B 100000 -F 25000 -D plughw:RTKAlsaCard,0 "$L/clip.wav" > /tmp/doveos-system-sound.log 2>&1 < /dev/null &
app=$!
finished=0
for n in 1 2 3 4 5 6 7 8;do
 kill -0 "$app" 2>/dev/null || break
 # This driver can retain the final 192 silent frames forever in DRAINING.
 # Only terminate our own player after EOF and <=256 frames remain (<6 ms).
 for card in /proc/asound/card[0-9]*;do
  test "$(cat "$card/id" 2>/dev/null)" = RTKAlsaCard || continue
  state=$card/pcm0p/sub0/status
  if grep -q '^state: DRAINING' "$state" 2>/dev/null && test "$(awk '$1=="owner_pid"{print $3}' "$state")" = "$app";then
   delay=$(awk '$1=="delay"{print $3}' "$state")
   case "$delay" in ''|*[!0-9]*) ;;*)if test "$delay" -le 256;then finished=1;kill -TERM "$app" 2>/dev/null || :;fi;;esac
  fi
 done
 test "$finished" = 0 || break
 sleep 1
done
code=0
if kill -0 "$app" 2>/dev/null;then kill -TERM "$app" 2>/dev/null || :;sleep 1;kill -KILL "$app" 2>/dev/null || :;code=1;fi
wait "$app" || code=$?;test "$finished" = 0 || code=0;app=
case "$action" in *preview)if test "$code" = 0;then say 'Sound preview finished.';else say 'Could not play sound. See the sound log.';fi;;esac
exit "$code"
