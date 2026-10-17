#!/bin/sh
# Read-only diagnostic: no clock setting, suspend, watchdog or power writes.
echo DOVEOS_POWER_INSPECT_1
date -u
uname -a
for p in /proc/sys/kernel/random/boot_id /proc/uptime \
 /sys/power/state /sys/kernel/suspend/mode /sys/kernel/suspend/context \
 /sys/kernel/suspend/wakeup /sys/kernel/suspend/timer \
 /sys/kernel/suspend/resume_state /sys/kernel/suspend/watchdog_en \
 /sys/kernel/suspend/watchdog_timer_sec /sys/kernel/suspend/gpio_wakeup_en \
 /sys/kernel/suspend/gpio_wakeup_act /proc/driver/rtc \
 /tmp/doveos-power-status /tmp/doveos-time-status \
 /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor \
 /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq; do
    echo "--- $p"
    if [ -r "$p" ]; then cat "$p"; else echo UNAVAILABLE; fi
done
first=$(cat /sys/class/rtc/rtc0/since_epoch 2>/dev/null)
read begin ignored < /proc/uptime
sleep 3
last=$(cat /sys/class/rtc/rtc0/since_epoch 2>/dev/null)
read end ignored < /proc/uptime
echo "RTC_SAMPLE first=$first last=$last monotonic_begin=$begin monotonic_end=$end"
case "$first:$last" in
    *[!0-9:]*|:*|*:) echo RTC_TICK=UNAVAILABLE ;;
    *)
        delta=$((last-first))
        elapsed=$((${end%%.*}-${begin%%.*}))
        if [ "$delta" -eq 0 ] && [ "$elapsed" -ge 2 ]; then
            echo RTC_TICK=FAILED_NOT_ADVANCING
        elif [ "$delta" -ge 2 ] && [ "$delta" -le 5 ] && [ "$elapsed" -ge 2 ] && [ "$elapsed" -le 5 ]; then
            echo RTC_TICK=OBSERVED
        else
            echo RTC_TICK=INCONCLUSIVE_TIME_OR_SCHEDULING_CHANGE
        fi ;;
esac
echo 'A ticking RTC alone does not verify alarm wake or suspend recovery.'
