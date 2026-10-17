set -e
M=/tmp/doveos-v2-root
mkdir -p /tmp/doveos-menu-final
gunzip -c /media/usb0/DoveOS-v2/classic-theme.tar.gz | tar -x -C /tmp/doveos-menu-final
cp /tmp/doveos-menu-final/jwmrc /tmp/doveos-theme/live-etc/jwmrc
cp /tmp/doveos-menu-final/jwmrc /media/usb0/DoveOS-v2/home/.jwmrc
chroot "$M" /bin/sh -c 'DISPLAY=:7 HOME=/root LD_LIBRARY_PATH=/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf /usr/bin/jwm -p'
chroot "$M" /bin/sh -c 'DISPLAY=:7 HOME=/root LD_LIBRARY_PATH=/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf /usr/bin/jwm -restart'
sleep 2
echo MENU_SPACING_VERIFIED
