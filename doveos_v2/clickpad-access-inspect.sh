echo DEVICE_NODES
ls -l /sys/class/hidraw /sys/class/usb /sys/class/usbmisc /dev/usb /dev/hid* 2>/dev/null || :
echo KERNEL_CONFIG
zcat /proc/config.gz 2>/dev/null | grep -E 'CONFIG_USB_HID|CONFIG_HIDRAW|CONFIG_HID_MULTITOUCH|CONFIG_HIDDEV' || :
echo INPUT_STATE
for x in /sys/bus/hid/devices/0003:03F0:0B51.0002/input/input2/capabilities/*;do echo "$x";cat "$x";done
