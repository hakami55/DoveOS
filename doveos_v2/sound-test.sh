#!/bin/sh
# Bounded speaker/headset tests. Leaves hardware volume and other players alone.
set -u
umask 077
RESULT=/tmp/drmosa-settings-result
L=/tmp/doveos-settings-sound-test.lock
T=/tmp/doveos-theme
D=/tmp/doveos-settings-audio
. "$T/audio-devices.sh"
action=${1:-}
say(){ printf '%s\n' "$1" > "$RESULT"; }
owned(){
 case "$1" in ''|*[!0-9]*) return 1;;esac
 [ "$1" -gt 1 ] && kill -0 "$1" 2>/dev/null &&
 tr '\000' ' ' < "/proc/$1/cmdline" 2>/dev/null | grep -qF '/tmp/doveos-theme/sound-test.sh'
}
if [ "$action" = sound-test-stop ];then
 p=$(cat "$L/pid" 2>/dev/null || :)
 if owned "$p";then kill -TERM "$p";else say 'No sound test is running.';fi
 exit 0
fi
case "$action" in sound-test|sound-test-left|sound-test-right|sound-headphones|sound-record|sound-play-recording) ;; *) exit 2;;esac
test -z "$(pidof vlc vlc-private aplay arecord || :)" || { say 'Stop other audio playback or recording before starting this test.';exit 1; }
test ! -e /tmp/doveos-direct-display || { say 'End the full-screen stream before testing sound.';exit 1; }
direction=p
case "$action" in
 sound-record) route=Dock,0;direction=c;;
 sound-headphones) route=${2:-Dock,0};;
 *) route=RTKAlsaCard,0;;
esac
audio_available "$route" "$direction" || { say 'Dock audio is unavailable. For microphone recording, connect a 3.5 mm headset with a microphone.';exit 1; }
device=${route%,*};device_number=${route##*,}
for card in /proc/asound/card[0-9]*;do
 [ "$(cat "$card/id" 2>/dev/null)" = "$device" ] || continue
 for status in "$card"/pcm"$device_number$direction"/sub*/status;do
  if grep -q '^state: RUNNING' "$status" 2>/dev/null;then
   say 'This audio device is busy. End its current stream before testing.';exit 1
  fi
 done
done
if ! mkdir "$L" 2>/dev/null;then
 p=$(cat "$L/pid" 2>/dev/null || :)
 if owned "$p";then say 'An audio test is already running.';exit 1;fi
 rm -f "$L/pid";rmdir "$L" 2>/dev/null || exit 1
 mkdir "$L" || exit 1
fi
app= stopped=0 timedout=0
cleanup(){
 trap - EXIT INT TERM
 if [ -n "$app" ];then kill -TERM "$app" 2>/dev/null || :;sleep 1;kill -KILL "$app" 2>/dev/null || :;wait "$app" 2>/dev/null || :;fi
 rm -f "$D/recording.next.wav" "$L/pid";rmdir "$L" 2>/dev/null || :
}
interrupt(){ stopped=1;test -z "$app" || kill -TERM "$app" 2>/dev/null || :; }
trap cleanup EXIT
trap interrupt INT TERM
echo $$ > "$L/pid"
case "$action" in
 sound-record)
  test -x /usr/bin/arecord || { say 'Headset microphone recording is unavailable.';exit 1; }
  mkdir -p "$D" || { say 'Could not prepare temporary recording storage.';exit 1; }
  chmod 700 "$D"
  # HP 03f0:0c56 can retain a bad capture clock state between recordings.
  # The verified 44.1 -> 32 -> 48 kHz sequence clears periodic 1 ms gaps.
  # Discard preparation audio; retain the previous sample on stop/failure.
  say 'Preparing microphone. Wait for the Recording message before speaking.'
  for prepare_rate in 44100 32000;do
   /usr/bin/arecord -D "hw:$device,$device_number" -t raw -f S16_LE -r "$prepare_rate" -c 2 -d 1 /dev/null > /tmp/doveos-mic-prepare.log 2>&1 < /dev/null &
   app=$!
   for prepare_wait in 1 2 3 4;do
    kill -0 "$app" 2>/dev/null || break
    [ "$stopped" = 0 ] || break
    sleep 1
   done
   if [ "$stopped" = 1 ];then say 'Microphone preparation stopped. Previous recording kept.';exit 1;fi
   if kill -0 "$app" 2>/dev/null;then say 'Microphone preparation timed out. Previous recording kept.';exit 1;fi
   prepare_code=0;wait "$app" || prepare_code=$?;app=
   if [ "$prepare_code" != 0 ];then say 'Could not prepare microphone. Previous recording kept.';exit 1;fi
  done
  say 'Recording for five seconds. Speak into the headset microphone.'
  /usr/bin/arecord -D "hw:$device,$device_number" -t wav -f S16_LE -r 48000 -c 2 -d 5 "$D/recording.next.wav" > /tmp/doveos-sound-test.log 2>&1 < /dev/null &
  ;;
 *)
  test -x /usr/bin/aplay || { say 'System audio playback is unavailable.';exit 1; }
  clip=$T/sound-test.wav
  case "$action" in
   sound-test-left) clip=$T/sound-test-left.wav;say "Playing three seconds of LEFT-channel tones...";;
   sound-test-right) clip=$T/sound-test-right.wav;say "Playing three seconds of RIGHT-channel tones...";;
   sound-play-recording) clip=$D/recording.wav;say 'Playing your recording...';;
   sound-headphones) say 'Playing six seconds of tones through the headphones...';;
   *) say 'Playing six seconds of STEREO tones...';;
  esac
  [ -r "$clip" ] || { say 'No recording or test clip is available. Record a sample first.';exit 1; }
  /usr/bin/aplay -N -B 100000 -F 25000 -D "plughw:$device,$device_number" "$clip" > /tmp/doveos-sound-test.log 2>&1 < /dev/null &
  ;;
esac
app=$!
for n in $(seq 1 20);do
 kill -0 "$app" 2>/dev/null || break
 [ "$stopped" = 0 ] || break
 sleep 1
 [ "$n" -lt 20 ] || timedout=1
done
if [ "$stopped" = 1 ] || [ "$timedout" = 1 ];then
 kill -TERM "$app" 2>/dev/null || :;sleep 1;kill -KILL "$app" 2>/dev/null || :
fi
code=0;wait "$app" || code=$?;app=
if [ "$stopped" = 1 ];then say 'Sound test stopped. Any previous recording was kept.'
elif [ "$timedout" = 1 ];then say 'Audio test timed out; try again after ending other audio streams.';exit 1
elif [ "$code" = 0 ];then
 case "$action" in
  sound-record)
   [ -s "$D/recording.next.wav" ] && mv "$D/recording.next.wav" "$D/recording.wav" || { say 'Could not save the microphone sample.';exit 1; }
   chmod 600 "$D/recording.wav"
   say 'Recording finished. Press Play Recording to hear the sample.'
   ;;
  sound-play-recording) say 'Recording playback finished. Did you hear your voice?';;
  sound-test-left) say 'Left-channel test finished. Did you hear tones only on the left?';;
  sound-test-right) say 'Right-channel test finished. Did you hear tones only on the right?';;
  sound-headphones) say 'Headphone test finished. Did you hear the alternating tones?';;
  *) say 'Sound test finished. Did you hear the alternating tones?';;
 esac
else
 if [ "$action" = sound-record ];then say 'Could not record this input. Check its connection, permission and recording support.'
 else say 'Could not play audio. Check the headset plug or end other audio streams.';fi
 exit 1
fi
