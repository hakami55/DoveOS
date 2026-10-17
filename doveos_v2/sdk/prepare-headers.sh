#!/bin/sh
set -eu
ROOT=${DOVEOS_SDK_BUILDROOT:-/home/hakami55/doveos-gtk-sdk}
mkdir -p "$ROOT/debs" "$ROOT/sysroot"
cd "$ROOT/debs"
apt-get download libgtk2.0-dev:armhf libglib2.0-dev:armhf libatk1.0-dev:armhf libpango1.0-dev:armhf libcairo2-dev:armhf libgdk-pixbuf2.0-dev:armhf libfontconfig1-dev:armhf libfreetype-dev:armhf libharfbuzz-dev:armhf libpixman-1-dev:armhf libpng-dev:armhf libx11-dev:armhf libxext-dev:armhf libxrender-dev:armhf libffi-dev:armhf zlib1g-dev:armhf libpcre3-dev:armhf libpcre2-dev:armhf libgraphite2-dev:armhf libfribidi-dev:armhf libxcb1-dev:armhf libxau-dev:armhf libxdmcp-dev:armhf x11proto-dev libthai-dev:armhf libdatrie-dev:armhf libexpat1-dev:armhf uuid-dev:armhf libmount-dev:armhf libblkid-dev:armhf libselinux1-dev:armhf libsepol1-dev:armhf libxft-dev:armhf libxcb-shm0-dev:armhf libxcb-render0-dev:armhf
for file in ./*.deb;do dpkg-deb -x "$file" "$ROOT/sysroot";done
cp /usr/share/pkgconfig/shared-mime-info.pc "$ROOT/sysroot/usr/share/pkgconfig/"
cp -a "${DOVEOS_RUNTIME:-/home/hakami55/doveos-v2-build/desktop-extracted}/usr/lib/arm-linux-gnueabihf/." "$ROOT/sysroot/usr/lib/arm-linux-gnueabihf/"
echo SDK_HEADERS_READY
