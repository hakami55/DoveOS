#!/bin/sh
set -eu
P=/media/usb0/DoveOS-v2/DovePlayer/play.sh
Q=/tmp/doveplayer
R=/media/usb0/DoveOS-v2/Reports/doveplayer-20261008
F=/media/usb0/DoveOS-v2/Experimental/mpv/test-1280x720.mp4
test ! -d "$Q/playing"
sh "$P" "$F" 0 0 > "$Q/control-launch.log" 2>&1 &
app=$!
for n in $(seq 1 30);do test -f "$Q/core.pid" && grep -q '^Playing' "$Q/state" && break;sleep 1;done
test -f "$Q/core.pid"
sleep 4
kill -USR1 "$(cat "$Q/core.pid")"
code=0;wait "$app" || code=$?
echo PAUSE_EXIT=$code
test "$code" = 21
test ! -e /tmp/doveos-direct-display
grep -q '^Paused' "$Q/state"
for f in player audio broker;do cp "$Q/$f.log" "$R/pause-$f.log";done
pos=$(sed -n '2p' "$Q/state")
echo RESUME_AT=$pos
sh "$P" "$F" "$pos" 0 > "$Q/control-launch.log" 2>&1
grep -q '^Finished' "$Q/state"
grep -q 'DOVEPLAYER_SUMMARY' "$Q/broker.log"
for f in player audio broker;do cp "$Q/$f.log" "$R/resume-$f.log";done
sh "$P" "$F" 7 0 > "$Q/control-launch.log" 2>&1
for f in player audio broker;do cp "$Q/$f.log" "$R/seek-$f.log";done
test ! -e /tmp/doveos-direct-display
sh "$P" /media/usb0/DrMoSa/Media/Big-Buck-Bunny-audio-test.m4a 0 6 > "$Q/control-launch.log" 2>&1
for f in player audio broker;do cp "$Q/$f.log" "$R/audio-only-$f.log";done
test ! -e /tmp/doveos-direct-display
code=0
sh "$P" /media/usb0/DoveOS-v2/DovePlayer/README.txt 0 0 > "$Q/control-launch.log" 2>&1 || code=$?
echo INVALID_FILE_EXIT=$code
test "$code" != 0
cp "$Q/player.log" "$R/invalid-file.log"
test ! -e /tmp/doveos-direct-display
echo DOVEPLAYER_CONTROL_CHECK_PASS
