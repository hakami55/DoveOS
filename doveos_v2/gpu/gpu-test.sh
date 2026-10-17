#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2/GPU
LOCK=/tmp/doveos-gpu-test.lock
mkdir "$LOCK" 2>/dev/null || { echo 'GPU test already running';exit 2; }
trap 'rmdir "$LOCK" 2>/dev/null || :' EXIT INT TERM
LOG="$B/last-test.log"
run(){
 echo 'DoveOS experimental GPU test'
 echo 'This is an offscreen GLES test, not desktop-wide acceleration.'
 [ ! -r /sys/kernel/debug/mali/version ] || cat /sys/kernel/debug/mali/version
 echo MEMORY_BEFORE
 if [ -r /sys/kernel/debug/mali/memory_usage ];then cat /sys/kernel/debug/mali/memory_usage;else echo 'Debug counters unavailable in this view';fi
 /bin/sh "$B/gpu-run.sh" probe || return $?
 /bin/sh "$B/gpu-run.sh" stress 1280 720 10 || return $?
 echo MEMORY_AFTER
 if [ -r /sys/kernel/debug/mali/memory_usage ];then cat /sys/kernel/debug/mali/memory_usage;else echo 'Debug counters unavailable in this view';fi
 echo GPU_TEST_COMPLETE
}
result=0
run > "$LOG" 2>&1 || result=$?
cat "$LOG"
exit "$result"
