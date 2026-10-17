echo RTC_DETAILS
for p in name date time hctosys;do echo "$p";cat /sys/class/rtc/rtc0/$p 2>/dev/null;done
