#!/bin/sh
# Build a new disk-image file. This script never opens a physical disk.
set -eu
stage=${1:?Usage: build-usb-image.sh STAGED_USB_ROOT OUTPUT.img}
output=${2:?Output .img file required}
test -d "$stage/autorun" && test -f "$stage/DoveOS-v2/doveos-v2.ext2"
test ! -e "$output" || { echo 'Output already exists; choose a new filename.' >&2; exit 2; }
case "$output" in *.img) ;; *) echo 'Output must end in .img' >&2; exit 2;; esac
command -v mkfs.fat >/dev/null
command -v mcopy >/dev/null
command -v fsck.fat >/dev/null
command -v python3 >/dev/null
part=$output.fat32-part
trap 'rm -f "$part"' EXIT
test ! -e "$part"
# 2 GiB disk, partition starts at 1 MiB. Use a drive of at least 4 GB.
truncate -s 2146435072 "$part"
mkfs.fat -F 32 -n DOVEOS -i D0BE0200 "$part"
for entry in "$stage"/*;do
 MTOOLS_SKIP_CHECK=1 mcopy -s -i "$part" "$entry" ::/
done
fsck.fat -n "$part"
python3 - "$part" "$output" <<'PY'
import pathlib,struct,sys
part,out=map(pathlib.Path,sys.argv[1:])
size=2147483648;start=2048;count=(size//512)-start
mbr=bytearray(512)
mbr[440:444]=struct.pack('<I',0xD0BE0200)
mbr[446:462]=struct.pack('<B3sB3sII',0x80,b'\xfe\xff\xff',0x0c,b'\xfe\xff\xff',start,count)
mbr[510:512]=b'\x55\xaa'
with out.open('xb') as disk,part.open('rb') as source:
 disk.truncate(size);disk.seek(0);disk.write(mbr);disk.seek(start*512)
 while data:=source.read(4*1024*1024):disk.write(data)
assert out.stat().st_size==size
print('DoveOS USB image created:',out)
PY
sha256sum "$output" > "$output.sha256"
