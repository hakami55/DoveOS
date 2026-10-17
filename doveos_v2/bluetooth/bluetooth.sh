#!/bin/sh
# DoveOS Bluetooth trial: BlueZ runs in RAM; stock firmware is not altered.
PATH=/usr/local/bin/bluetooth:/usr/bin:/bin:/usr/sbin:/sbin
export PATH
umask 077
BASE=/tmp/doveos-bluetooth
USB=/media/usb0/DrMoSa/Bluetooth
RESULT=$BASE/result
. "$USB/bluetooth-radio.sh"
say(){ printf '%s\n' "$1" > "$RESULT"; }
save_debug(){
    [ -d "$USB" ] || return
    for name in result daemon.log le-scan.log classic-scan.log mgmt-scan.log le-scan-status.log pair.log connect.log; do
        [ -r "$BASE/$name" ] && cp "$BASE/$name" "$USB/$name" 2>/dev/null || :
    done
    if [ -d /var/lib/bluetooth ]; then
        # FAT USB drives cannot store the colons in Bluetooth address folders.
        if tar -cf "$USB/bonds.tar.new" -C /var/lib/bluetooth . 2>/dev/null; then
            mv "$USB/bonds.tar.new" "$USB/bonds.tar"
        else
            rm -f "$USB/bonds.tar.new"
        fi
    fi
}
cleanup(){
    radio_release
    [ "${scan_locked:-0}" = 1 ] && rm -f "$BASE/scan.lock/pid" && rmdir "$BASE/scan.lock" 2>/dev/null || :
    save_debug
}
trap cleanup 0
ctl(){
    (cd "$BASE" && ./lib/ld-linux-armhf.so.3 --library-path ./lib ./bluetoothctl "$@")
}
start(){
    mkdir -p "$BASE"
    /bin/sh "$USB/bluetooth-policy.sh" || {
        say 'Could not prepare Bluetooth audio permissions.'; return 1;
    }
    if [ -f "$BASE/policy-reconnect-needed" ]; then
        save_debug
        for proc in /proc/[0-9]*; do
            [ -r "$proc/cmdline" ] || continue
            cmd=$(tr '\000' ' ' <"$proc/cmdline")
            case "$cmd" in *'./bluetoothd -n -P input,hog'*)
                kill "${proc##*/}" 2>/dev/null || :;;
            esac
        done
        sleep 1
        rm -f "$BASE/policy-reconnect-needed"
    fi
    if [ -r "$USB/bluetooth-transport.sh" ]; then
        /bin/sh "$USB/bluetooth-transport.sh" start >"$BASE/transport-start.log" 2>&1 || {
            say 'Bluetooth transport could not start; original radio setup restored.'; return 1;
        }
    fi
    [ -d /sys/class/bluetooth/hci0 ] || { say 'Built-in Bluetooth radio unavailable.'; return 1; }
    if [ ! -x "$BASE/bluetoothd" ]; then
        [ -r "$USB/bluetooth-runtime.tar.gz" ] || { say 'Bluetooth runtime missing from USB.'; return 1; }
        (cd "$BASE" && gunzip -c "$USB/bluetooth-runtime.tar.gz" | tar xf -) || {
            say 'Could not load Bluetooth runtime into RAM.'; return 1;
        }
    fi
    if ! ps | grep -q '[b]luetoothd -n'; then
        if ! mount | grep -q ' on /var/lib '; then
            mkdir -p /tmp/doveos-varlib
            cp -a /var/lib/. /tmp/doveos-varlib/ || { say 'Could not prepare Bluetooth storage in RAM.'; return 1; }
            mount --bind /tmp/doveos-varlib /var/lib || { say 'Could not mount Bluetooth storage in RAM.'; return 1; }
        fi
        mkdir -p /var/lib/bluetooth
        if [ -r "$USB/bonds.tar" ]; then
            tar -xf "$USB/bonds.tar" -C /var/lib/bluetooth 2>/dev/null || :
        elif [ -d "$USB/bonds" ]; then
            cp -a "$USB/bonds/." /var/lib/bluetooth/ 2>/dev/null || :
        fi
        (cd "$BASE" && nohup ./lib/ld-linux-armhf.so.3 --library-path ./lib ./bluetoothd -n -P input,hog >daemon.log 2>&1 </dev/null &)
        n=0
        while [ "$n" -lt 8 ]; do
            sleep 1
            ctl --timeout 5 show 2>/dev/null | grep -q '^Controller ' && break
            n=$((n+1))
        done
    fi
    # bluetoothctl --timeout waits its full timeout even after printing show.
    # A live powered adapter can answer this one bounded D-Bus request directly.
    if dbus-send --system --print-reply --reply-timeout=1000 --dest=org.bluez \
        /org/bluez/hci0 org.freedesktop.DBus.Properties.Get \
        string:org.bluez.Adapter1 string:Powered 2>/dev/null | grep -q 'boolean true'; then
        say 'Bluetooth ready. F8 scans; Enter pairs the selected device.'
        return 0
    fi
    ctl --timeout 5 show 2>/dev/null | grep -q '^Controller ' || { say 'BlueZ did not detect the radio.'; return 1; }
    if ! ctl --timeout 5 show 2>/dev/null | grep -q 'Powered: yes'; then
        printf 'power on\n' | ctl >"$BASE/power.log" 2>&1
        sleep 1
    fi
    ctl --timeout 5 show 2>/dev/null | grep -q 'Powered: yes' || { say 'Bluetooth could not be powered on.'; return 1; }
    ctl --timeout 5 system-alias DoveOS-LapDock >/dev/null 2>&1 || :
    say 'Bluetooth ready. F8 scans; Enter pairs the selected device.'
}
valid_address(){
    printf '%s\n' "$1" | grep -Eq '^[[:xdigit:]]{2}(:[[:xdigit:]]{2}){5}$'
}
remember_device_name(){
    # Preserve genuine/custom aliases. Only replace unnamed/address placeholders.
    alias=$(sed -n 's/^[[:space:]]*Alias: //p' "$BASE/device-info.log" | head -n 1)
    alias_normal=$(printf '%s' "$alias" | tr 'A-Z-' 'a-z:')
    address_normal=$(printf '%s' "$address" | tr 'A-Z' 'a-z')
    if [ "$alias_normal" != "$address_normal" ];then
        case "$alias_normal" in ''|unknown|'unknown device'|unnamed|'unnamed device'|'(unknown)'|'n/a') ;; *) return 0;; esac
    fi
    name=$(awk -v addr="$address" '
        tolower($1)==tolower(addr) {
            sub(/^[^ \t]+[ \t]*/, "");sub(/\r$/, "");sub(/[ \t]+$/, "");sub(/ \(paired\)$/, "")
            low=tolower($0);compact=low;gsub(/-/, ":", compact)
            if(length($0)>0 && length($0)<=89 && low!="unknown" && low!="unknown device" && low!="unnamed" && low!="unnamed device" && low!="(unknown)" && low!="n/a" && compact!=tolower(addr)) found=$0
        }
        END {if(length(found))print found}
    ' "$USB/classic-scan.log" "$USB/le-scan.log" "$BASE/classic-scan.log" "$BASE/le-scan.log" 2>/dev/null)
    [ -n "$name" ] || return 0
    device=$(printf '%s' "$address" | tr ':' '_' | tr 'a-f' 'A-F')
    dbus-send --system --print-reply --reply-timeout=1500 --dest=org.bluez "/org/bluez/hci0/dev_$device" org.freedesktop.DBus.Properties.Set string:org.bluez.Device1 string:Alias "variant:string:$name" >"$BASE/name-save.log" 2>&1 || :
}
keyboard_candidate(){
    grep -q 'Icon: input-keyboard' "$BASE/device-info.log" ||
    grep -q 'Appearance: 0x03c1' "$BASE/device-info.log" ||
    grep -Fxq "$address" "$USB/keyboard-devices.txt" 2>/dev/null ||
    { ! grep -q 'UUID:' "$BASE/device-info.log" && grep -q 'Connected: yes' "$BASE/device-info.log"; }
}
finish_pair(){
    radio_release
    ctl --timeout 3 trust "$address" >/dev/null 2>&1 || :
    ctl --timeout 3 info "$address" >"$BASE/device-info.log" 2>/dev/null || :
    remember_device_name
    if grep -q '0000110b-0000-1000-8000-00805f9b34fb' "$BASE/device-info.log";then
        /bin/sh "$USB/bluetooth.sh" bluetooth-connect "$address"
        return $?
    fi
    if grep -q '00001812-0000-1000-8000-00805f9b34fb' "$BASE/device-info.log" &&
       grep -q 'Icon: input-mouse' "$BASE/device-info.log";then
        if /bin/sh "$USB/bluetooth-hid-service.sh" start mouse "$address";then return 0;fi
        detail=$(cat "$RESULT" 2>/dev/null)
        say "Paired; input not ready. $detail"
        return 1
    fi
    if keyboard_candidate;then
        if /bin/sh "$USB/bluetooth-hid-service.sh" start keyboard "$address";then return 0;fi
        detail=$(cat "$RESULT" 2>/dev/null)
        say "Paired; input not ready. $detail"
        return 1
    fi
    say 'Paired. Press Reconnect to connect a supported profile.'
}
case "${1:-}" in
 bluetooth-start)
    start
    ;;
 bluetooth-scan)
    mkdir -p "$BASE"
    if ! mkdir "$BASE/scan.lock" 2>/dev/null; then
        owner=$(cat "$BASE/scan.lock/pid" 2>/dev/null || :)
        if [ -n "$owner" ] && kill -0 "$owner" 2>/dev/null; then
            say 'A Bluetooth scan is already running.'; exit 1
        fi
        rm -f "$BASE/scan.lock/pid"
        rmdir "$BASE/scan.lock" 2>/dev/null || :
        mkdir "$BASE/scan.lock" 2>/dev/null || { say 'Bluetooth scanner busy.'; exit 1; }
    fi
    scan_locked=1
    echo $$ >"$BASE/scan.lock/pid"
    start || exit 1
    radio_acquire 30 || { say 'Bluetooth is busy with another connection. Retry Find devices shortly.';exit 1; }
    : >"$BASE/le-scan.log"
    : >"$BASE/classic-scan.log"
    # Classic MGMT registers devices with BlueZ. The old vendor kernel rejects
    # LE MGMT discovery, so a bounded HCI scanner reads LE advertising separately.
    [ -r "$USB/mgmt-bredr-scan" ] || { say 'Bluetooth scanner missing from USB.'; exit 1; }
    cp "$USB/mgmt-bredr-scan" "$BASE/mgmt-bredr-scan" || {
        say 'Could not load Bluetooth scanner into RAM.'; exit 1;
    }
    chmod 700 "$BASE/mgmt-bredr-scan"
    say 'Scanning Bluetooth Classic devices...'
    classic_ok=1
    "$BASE/lib/ld-linux-armhf.so.3" --library-path "$BASE/lib" \
        "$BASE/mgmt-bredr-scan" >"$BASE/classic-scan.log" 2>"$BASE/mgmt-scan.log" || {
        classic_ok=0
    }
    # Update names only for devices actually found in this scan.
    ctl --timeout 5 devices | while read -r tag address name; do
        [ "$tag" = Device ] || continue
        valid_address "$address" || continue
        grep -q "^$address " "$BASE/classic-scan.log" || continue
        printf '%s %s\n' "$address" "$name"
    done >>"$BASE/classic-scan.log"
    # Saved Classic bonds remain selectable even when not discoverable nearby.
    ctl --timeout 5 paired-devices | while read -r tag address name; do
        [ "$tag" = Device ] && valid_address "$address" || continue
        grep -q 'BR/EDR' "/var/lib/bluetooth/10:9F:A9:00:00:01/$address/info" 2>/dev/null || continue
        printf '%s %s (paired)\n' "$address" "$name"
    done >>"$BASE/classic-scan.log"
    le_ok=0
    if [ "$classic_ok" = 1 ] && [ -r "$USB/le-scan" ]; then
        say 'Scanning Bluetooth Low Energy devices...'
        cp "$USB/le-scan" "$BASE/le-scan" && chmod 700 "$BASE/le-scan" &&
        "$BASE/lib/ld-linux-armhf.so.3" --library-path "$BASE/lib" \
            "$BASE/le-scan" >"$BASE/le-scan.log" 2>"$BASE/le-scan-status.log" && le_ok=1
    fi
    if [ "$classic_ok" = 1 ] && [ "$le_ok" = 1 ]; then
        say 'Scan finished. Classic and Low Energy devices are listed. Select a device to pair.'
    else
        say 'Partial scan: available devices are listed. Check Bluetooth diagnostics for the failed scan.'
        exit 1
    fi
    ;;
 bluetooth-pair-le|bluetooth-pair-classic)
    address=${2:-}
    valid_address "$address" || { say 'Invalid Bluetooth address.'; exit 1; }
    start || exit 1
    radio_acquire 30 || { say 'Bluetooth is busy with another connection. Retry Pair shortly.';exit 1; }
    if [ "$1" = bluetooth-pair-classic ] &&
       ! ctl --timeout 3 info "$address" 2>/dev/null | grep -Fq "Device $address ("; then
        # The old daemon can omit raw-discovered devices from its D-Bus list.
        # Bond through MGMT while BlueZ remains running, so it stores the key.
        if [ -r "$USB/mgmt-pair" ]; then
            # The USB filesystem is noexec. Load the bounded helper into RAM.
            cp "$USB/mgmt-pair" "$BASE/mgmt-pair.new" &&
            chmod 700 "$BASE/mgmt-pair.new" &&
            mv "$BASE/mgmt-pair.new" "$BASE/mgmt-pair" || {
                say 'Could not prepare the Bluetooth pairing helper.'; exit 1;
            }
            "$BASE/mgmt-pair" "$address" >"$BASE/pair.log" 2>&1 || :
            if ctl --timeout 3 info "$address" 2>/dev/null | grep -q 'Paired: yes';then
                ctl --timeout 3 trust "$address" >/dev/null 2>&1 || :
                finish_pair
                exit 0
            fi
            say 'Pairing failed. Keep the device in pairing mode and try again.'
            exit 1
        fi
        say 'Bluetooth pairing helper is missing from USB.'
        exit 1
    fi
    # LE discovery on this dock may not populate BlueZ. Connect through HCI
    # first so BlueZ can see the remote device before pairing.
    if [ "$1" = bluetooth-pair-le ]; then
        DOVEOS_RADIO_LE_PENDING=1
        DOVEOS_RADIO_LE_ADDRESS=$address
        DOVEOS_RADIO_LOG="$BASE/le-connect.log"
        /usr/local/bin/bluetooth/hcitool lecc "$address" >"$BASE/le-connect.log" 2>&1 &
        sleep 3
    fi
    (printf 'agent NoInputNoOutput\ndefault-agent\npairable on\n';
     sleep 1; printf 'pair %s\n' "$address"; sleep 18;
     printf 'trust %s\ninfo %s\nquit\n' "$address" "$address") |
        ctl >"$BASE/pair.log" 2>&1
    if ctl --timeout 5 info "$address" 2>/dev/null | grep -q 'Paired: yes'; then
        finish_pair
    else
        say 'Pairing failed. Keep the device in pairing mode and scan again.'
    fi
    ;;
 bluetooth-disconnect)
    address=${2:-}
    valid_address "$address" || { say 'Invalid Bluetooth address.';exit 1; }
    mkdir -p "$BASE"
    # Intentional Disconnect cancels automatic HID recovery, keeping bonds.
    if [ -r "$USB/bluetooth-hid-service.sh" ];then
        /bin/sh "$USB/bluetooth-hid-service.sh" stop mouse "$address" &&
        /bin/sh "$USB/bluetooth-hid-service.sh" stop keyboard "$address" || {
            say 'Input is still stopping. Retry Disconnect shortly.';exit 1;
        }
    fi
    for f in bluetooth-input.sh bluetooth-keyboard.sh;do
        [ ! -r "$USB/$f" ] || /bin/sh "$USB/$f" stop "$address" || {
            say 'Input could not stop. Retry Disconnect.';exit 1;
        }
    done
    device=$(printf '%s' "$address" | tr ':' '_' | tr 'a-f' 'A-F')
    dbus-send --system --print-reply --reply-timeout=5000 --dest=org.bluez "/org/bluez/hci0/dev_$device" org.bluez.Device1.Disconnect >"$BASE/disconnect.log" 2>&1 || :
    sleep 1
    if /usr/local/bin/bluetooth/hcitool con | grep -F "$address" | grep -Eq 'state 1([[:space:]]|$)';then
        say 'The Bluetooth link is still connected. Retry Disconnect.';exit 1
    fi
    say 'Disconnected. Pairing saved; choose Reconnect when ready.'
    ;;
 bluetooth-forget)
    address=${2:-}
    valid_address "$address" || { say 'Invalid Bluetooth address.'; exit 1; }
    start || exit 1
    if [ -r "$USB/bluetooth-hid-service.sh" ];then
        /bin/sh "$USB/bluetooth-hid-service.sh" stop mouse "$address" &&
        /bin/sh "$USB/bluetooth-hid-service.sh" stop keyboard "$address" || {
            say 'Bluetooth input is stopping. Retry Forget shortly.';exit 1;
        }
    fi
    if [ -r "$USB/bluetooth-input.sh" ];then
        /bin/sh "$USB/bluetooth-input.sh" stop "$address" || {
            say 'The mouse input session could not stop. Retry Forget.';exit 1;
        }
    fi
    if [ -r "$USB/bluetooth-keyboard.sh" ];then
        /bin/sh "$USB/bluetooth-keyboard.sh" stop "$address" || {
            say 'The keyboard input session could not stop. Retry Forget.';exit 1;
        }
    fi
    ctl --timeout 5 remove "$address" >"$BASE/forget.log" 2>&1 || :
    if ! grep -q 'Device has been removed' "$BASE/forget.log"; then
        say 'Could not forget this device. Find devices, select it, and try again.'
        exit 1
    fi
    if ctl --timeout 3 info "$address" 2>/dev/null | grep -q 'Paired: yes'; then
        say 'The device is still paired. Please try Forget again.'; exit 1
    fi
    # Persist the removal now; do not leave the previous bond in the USB archive.
    if ! tar -cf "$USB/bonds.tar.new" -C /var/lib/bluetooth . 2>/dev/null ||
       ! mv "$USB/bonds.tar.new" "$USB/bonds.tar"; then
        say 'Device forgotten for this session, but USB save failed.'; exit 1
    fi
    if [ "$(cat "$USB/audio-device.txt" 2>/dev/null)" = "$address" ]; then
        rm -f "$USB/audio-device.txt"
    fi
    say 'Device forgotten. Put it in pairing mode, click Find devices, then Pair.'
    ;;
 bluetooth-connect)
    address=${2:-}
    valid_address "$address" || { say 'Invalid Bluetooth address.'; exit 1; }
    start || exit 1
    ctl --timeout 3 info "$address" >"$BASE/device-info.log" 2>/dev/null || :
    remember_device_name
    if ! grep -q '0000110b-0000-1000-8000-00805f9b34fb' "$BASE/device-info.log"; then
        # Input devices must never depend on the headphone audio service.
        ctl --timeout 3 trust "$address" >/dev/null 2>&1 || :
        if grep -q '00001812-0000-1000-8000-00805f9b34fb' "$BASE/device-info.log" &&
           grep -q 'Icon: input-mouse' "$BASE/device-info.log"; then
            /bin/sh "$USB/bluetooth-hid-service.sh" start mouse "$address"
            exit $?
        fi
        if keyboard_candidate;then
            /bin/sh "$USB/bluetooth-hid-service.sh" start keyboard "$address"
            exit $?
        fi
        (printf 'connect %s\n' "$address";sleep 8;printf 'info %s\nquit\n' "$address") |
            ctl >"$BASE/connect.log" 2>&1
        if ctl --timeout 3 info "$address" 2>/dev/null | grep -q 'Connected: yes'; then
            say 'Bluetooth connected. This device may need its own input support.'
        else
            say 'Connection failed. Wake the saved device and try Reconnect again.';exit 1
        fi
        exit 0
    fi
    /bin/sh "$USB/bluetooth-audio.sh" start >"$BASE/audio-start.log" 2>&1 || {
        say 'Bluetooth audio service could not start.'; exit 1;
    }
    ctl --timeout 3 trust "$address" >/dev/null 2>&1 || :
    if ctl --timeout 3 info "$address" 2>/dev/null | grep -q '0000110b-0000-1000-8000-00805f9b34fb'; then
        # Connect the audio sink explicitly; some devices fail a generic
        # request which also tries their unsupported handsfree/serial profiles.
        device=$(printf '%s' "$address" | tr ':' '_' | tr 'a-f' 'A-F')
        radio_acquire 30 || { say 'Bluetooth is busy with another connection. Try Reconnect shortly.';exit 1; }
        /tmp/doveos-bluealsa/bounded-run 14 "$(command -v dbus-send)" \
            --system --print-reply --reply-timeout=12000 --dest=org.bluez \
            "/org/bluez/hci0/dev_$device" org.bluez.Device1.ConnectProfile \
            string:0000110b-0000-1000-8000-00805f9b34fb >"$BASE/connect.log" 2>&1 || :
        radio_release
    else
        (printf 'agent NoInputNoOutput\ndefault-agent\nconnect %s\n' "$address";
         sleep 8; printf 'info %s\nquit\n' "$address") |
            ctl >"$BASE/connect.log" 2>&1
    fi
    if ctl --timeout 5 info "$address" 2>/dev/null | grep -q 'Connected: yes'; then
        if /bin/sh "$USB/bluetooth-audio.sh" info "$address" >"$BASE/audio-info.log" 2>&1; then
            printf '%s\n' "$address" >"$USB/audio-device.txt"
            say 'Bluetooth audio connected. Use Test sound to check your headphones or speaker.'
        else
            say 'Device connected. No Bluetooth audio output is available for this device.';exit 1
        fi
    else
        if grep -q 'Host is down' "$BASE/connect.log";then
            say 'Audio device did not answer. Turn it on and disconnect it from other devices, then Reconnect.'
        else say 'Bluetooth audio connection failed. Check Bluetooth diagnostics.';fi
        exit 1
    fi
    ;;
 bluetooth-audio-test)
    address=${2:-}
    valid_address "$address" || { say 'Select an audio device first.'; exit 1; }
    start || exit 1
    if /bin/sh "$USB/bluetooth-audio.sh" test "$address" >"$BASE/audio-test.log" 2>&1; then
        say 'Sound test finished: three quiet low/high tone pairs.'
    else
        say 'Sound test failed. Turn on the audio device and press Reconnect.'
        exit 1
    fi
    ;;
 *) say 'Unknown Bluetooth action.'; exit 1;;
esac
