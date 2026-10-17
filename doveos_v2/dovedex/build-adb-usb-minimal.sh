#!/bin/sh
set -eu
P=/home/hakami55/dovedex-adb-wifi
SOURCE=/home/hakami55/dovedeX-v01/adb-source
LIB=/home/hakami55/dovedeX-v01/runtime/usr/lib/arm-linux-gnueabihf/android
OUT=/mnt/c/vscode/HPlapdock/analysis/dovedex-20261003
python3 - "$P/main.cpp" "$P/main-usb.cpp" <<'PY'
import sys
s=open(sys.argv[1]).read()
assert 'DOVEDEX_TCP_ONLY_USB_DISABLED' in s
s=s.replace('    fprintf(stderr, "DOVEDEX_TCP_ONLY_USB_DISABLED\\n");',
            '    fprintf(stderr, "DOVEDEX_USB_INIT_BEGIN\\n");usb_init();fprintf(stderr, "DOVEDEX_USB_INIT_RETURNED\\n");')
open(sys.argv[2],'w').write(s)
PY
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu
cd "$SOURCE"
mkdir -p "$P/libusb"
cp /usr/include/libusb-1.0/libusb.h "$P/libusb/libusb.h"
COMMON="-include string.h -include sys/sysmacros.h -I$P -Iinclude -Iadb -Ibase/include -Iliblog/include -Ilibcutils/include -Ilibcrypto_utils/include -I/home/hakami55/dovedeX-v01/boringssl-source/src/include -I/usr/include/libusb-1.0 -D_Nonnull= -D_Nullable= -DADB_HOST=1 -D_GNU_SOURCE"
python3 - "$SOURCE/adb/client/usb_libusb.cpp" "$P/usb-libusb-minimal.cpp" <<'PY'
import sys
s=open(sys.argv[1]).read()
a=s.index('        rc = libusb_set_interface_alt_setting(')
b=s.index('    } else {', a)
s=s[:a]+'        LOG(INFO) << "DOVEDEX_USB_CLAIM_ONLY: no alt-setting or clear-halt";\n'+s[b:]
open(sys.argv[2],'w').write(s)
PY
FILES="adb/adb.cpp adb/adb_io.cpp adb/adb_listeners.cpp adb/adb_trace.cpp adb/adb_utils.cpp adb/fdevent.cpp adb/sockets.cpp adb/socket_spec.cpp adb/sysdeps/errno.cpp adb/transport.cpp adb/transport_local.cpp adb/transport_usb.cpp adb/sysdeps_unix.cpp adb/sysdeps/posix/network.cpp adb/client/usb_dispatch.cpp $P/usb-libusb-minimal.cpp adb/client/usb_linux.cpp adb/diagnose_usb.cpp adb/adb_auth_host.cpp adb/services.cpp adb/transport_mdns_unsupported.cpp"
/usr/bin/arm-linux-gnueabihf-g++ -std=c++14 -O2 -mno-unaligned-access -fPIC $COMMON \
 -DADB_VERSION='"DoveDeX portable 8.1.0"' $FILES -shared -Wl,-soname,libadb.so.0 \
 -Wl,-rpath-link,"$LIB" -L"$LIB" -L/home/hakami55/dovedeX-v01/runtime/lib/arm-linux-gnueabihf \
 -l:libbase.so.0 -l:libcutils.so.0 -l:libcrypto_utils.so.0 -l:libcrypto.so.0 -l:libusb-1.0.so.0 -lpthread -latomic \
 -o "$OUT/libadb-portable.so.0"
FILES="adb/adb_client.cpp adb/bugreport.cpp $P/main-usb.cpp adb/console.cpp adb/commandline.cpp adb/file_sync_client.cpp adb/line_printer.cpp adb/services.cpp adb/shell_service_protocol.cpp"
/usr/bin/arm-linux-gnueabihf-g++ -std=c++14 -O2 -mno-unaligned-access $COMMON \
 -DADB_VERSION='"DoveDeX USB test 8.1.0"' $FILES -Wl,-rpath-link,"$LIB" -L"$OUT" -L"$LIB" \
 -l:libadb-portable.so.0 -l:libbase.so.0 -lpthread -o "$OUT/adb-usb"
cp "$P/main-usb.cpp" "$OUT/adb-main-usb.cpp"
file "$OUT/adb-usb"
