echo USB_ADDRESS
cat /sys/bus/usb/devices/1-1.3/busnum /sys/bus/usb/devices/1-1.3/devnum
od -An -tx1 /sys/bus/hid/devices/0003:03F0:0B51.0002/report_descriptor
echo HID_NODES
ls /dev/hid* /dev/usb/hid* 2>/dev/null || :
