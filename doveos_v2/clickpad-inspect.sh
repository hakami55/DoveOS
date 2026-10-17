echo INPUT
cat /proc/bus/input/devices
for d in /sys/bus/hid/devices/0003:03F0:0B51.*;do
 echo HID=$d
 ls -l "$d/driver" "$d/hidraw" 2>/dev/null
 od -An -tx1 "$d/report_descriptor" 2>/dev/null | head -18
done
ls /sys/kernel/debug/hid
