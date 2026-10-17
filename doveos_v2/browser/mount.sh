set -eu
B=/media/usb0/DoveOS-v2/BrowserTrial
L=/tmp/doveos-webkit-root
Q=/tmp/browser-webkit-work
T=/tmp/modern-browser-tools
test -z "$(pidof surf WebKitWebProcess WebKitNetworkProcess 2>/dev/null || :)"
mkdir -p "$Q/.X11-unix" "$Q/browser-profile/.config/gtk-3.0" "$Q/browser-profile/.cache" "$Q/browser-profile/runtime" "$Q/mesa" "$L"
chmod 1777 "$Q"
mkdir -p "$Q/browser-profile/.surf/styles"
touch "$Q/browser-profile/.surf/styles/default.css"
chmod 644 "$Q/browser-profile/.surf/styles/default.css"
chown -R 65534:65534 "$Q/browser-profile"
chmod 700 "$Q/browser-profile/runtime"
mounted(){ awk -v p="$1" '$2==p{ok=1}END{exit !ok}' /proc/mounts; }
bind(){ mounted "$2" || mount --bind "$1" "$2"; }
for name in webkit mesa;do
 F=/tmp/modern-browser-$name-direct
 mkdir -p "$F"
 if ! mounted "$F";then
  nohup "$T/direct-image" "$B/$name.ext2" "$F" > "$Q/$name-direct.log" 2>&1 </dev/null &
  echo $! > "$Q/$name-direct.pid"
 fi
done
sleep 2
mounted "$L" || mount -t ext2 -o loop,ro /tmp/modern-browser-webkit-direct/image "$L"
mounted "$Q/mesa" || mount -t ext2 -o loop,ro /tmp/modern-browser-mesa-direct/image "$Q/mesa"
bind "$Q" "$L/tmp"
bind /tmp/.X11-unix "$Q/.X11-unix"
bind "$Q/mesa" "$L/tmp/mesa"
bind /proc "$L/proc"
if ! mounted "$L/dev";then mount -t tmpfs -o size=1m tmpfs "$L/dev";fi
mkdir -p "$L/dev/shm"
for n in null zero random urandom;do touch "$L/dev/$n";bind /dev/$n "$L/dev/$n";done
bind /dev/shm "$L/dev/shm"
E=/tmp/modern-webkit-etc
if ! mounted "$L/etc";then mkdir -p "$E";cp -a "$L/etc/." "$E/";mount --bind "$E" "$L/etc";fi
cp /etc/resolv.conf "$E/resolv.conf"
chmod 644 "$E/resolv.conf"
cat /proc/sys/kernel/random/uuid | tr -d '-' > "$E/machine-id"
chmod 644 "$E/machine-id"
cat > "$E/fonts/fonts.conf" <<'EOF'
<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd"><fontconfig><dir>/usr/share/fonts</dir><cachedir>/tmp/browser-profile/.cache/fontconfig</cachedir></fontconfig>
EOF
chmod 644 "$E/fonts/fonts.conf"
chroot "$L" /usr/lib/arm-linux-gnueabihf/gdk-pixbuf-2.0/gdk-pixbuf-query-loaders > "$Q/browser-loaders.cache"
chmod 644 "$Q/browser-loaders.cache"
cat "$Q/webkit-direct.log" "$Q/mesa-direct.log"
echo WEBKIT_DIRECT_MOUNT_READY
