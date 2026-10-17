#!/bin/sh
set -eu
P=/home/hakami55/dovedex-adb-wifi
SOURCE=/home/hakami55/dovedeX-v01/adb-source
LIB=/home/hakami55/dovedeX-v01/runtime/usr/lib/arm-linux-gnueabihf/android
mkdir -p "$P"
cp "$SOURCE/adb/client/main.cpp" "$P/main.cpp"
python3 - "$P/main.cpp" <<'PY'
import sys
p=sys.argv[1]
s=open(p).read()
# This experimental executable is TCP-only, by construction. No USB discovery,
# USB cleanup, emulator discovery or mDNS helper startup is called.
assert s.count('    usb_init();')==1 and s.count('    usb_cleanup();')==1
s=s.replace('    usb_init();','    fprintf(stderr, "DOVEDEX_TCP_ONLY_USB_DISABLED\\n");')
s=s.replace('    usb_cleanup();','    // USB was never initialized in the DoveDeX TCP-only executable.')
s=s.replace('    local_init(DEFAULT_ADB_LOCAL_TRANSPORT_PORT);','    // Only explicitly selected TCP endpoints may be connected.')
s=s.replace('    init_mdns_transport_discovery();','    // Manual endpoint connection avoids an unverified mDNS runtime.')
s=s.replace('            adb_wait_for_device_initialization();','            // No USB scan to wait for in this TCP-only executable.')
open(p,'w').write(s)
PY
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu
cat > "$P/stdatomic.h" <<'EOF'
#pragma once
#include <atomic>
using std::atomic_bool;
using std::atomic_int;
using std::atomic_uint;
using std::atomic_load_explicit;
using std::atomic_store_explicit;
using std::memory_order_acquire;
using std::memory_order_release;
using std::memory_order_relaxed;
EOF
cd "$SOURCE"
SOURCES="adb/adb_client.cpp adb/bugreport.cpp $P/main.cpp adb/console.cpp adb/commandline.cpp adb/file_sync_client.cpp adb/line_printer.cpp adb/services.cpp adb/shell_service_protocol.cpp"
/usr/bin/arm-linux-gnueabihf-g++ -std=c++14 -O2 -mno-unaligned-access \
 -I"$P" -Iinclude -Iadb -Ibase/include -Iliblog/include -Ilibcutils/include -D_Nonnull= -D_Nullable= -DADB_VERSION='"DoveDeX TCP-only 8.1.0"' -DADB_HOST=1 -D_GNU_SOURCE \
 $SOURCES -Wl,-rpath-link,"$LIB" -L"$LIB" -l:libadb.so.0 -l:libbase.so.0 -lpthread \
 -o /mnt/c/vscode/HPlapdock/analysis/dovedex-20261003/adb-wifi
cp "$P/main.cpp" /mnt/c/vscode/HPlapdock/analysis/dovedex-20261003/adb-main-wifi.cpp
file /mnt/c/vscode/HPlapdock/analysis/dovedex-20261003/adb-wifi
