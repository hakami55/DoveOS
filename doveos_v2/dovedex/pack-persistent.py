from pathlib import Path
import hashlib, io, re, tarfile

p = Path(__file__).resolve().parent
entries = []
with tarfile.open(p / 'adb-usb-runtime.tar.gz', 'r:gz') as source, tarfile.open(p / 'runtime.tar.gz', 'w:gz') as target:
    for item in source:
        if not item.isfile():
            continue
        data = source.extractfile(item).read()
        target.addfile(item, io.BytesIO(data))
        entries.append((item.name, hashlib.sha256(data).hexdigest()))
    for name in ['adb-wifi', 'dovedex-viewer', 'dovedex-rtk-host', 'dovedex-run', 'dovedex-quit', 'scrcpy-server-4.1']:
        data = (p / name).read_bytes()
        item = tarfile.TarInfo(name)
        item.size, item.mode = len(data), 0o700
        target.addfile(item, io.BytesIO(data))
        entries.append((name, hashlib.sha256(data).hexdigest()))
    data = ''.join(digest + '  ' + name + '\n' for name, digest in entries).encode()
    item = tarfile.TarInfo('contents.sha256')
    item.size = len(data)
    target.addfile(item, io.BytesIO(data))

runtime_hash = hashlib.sha256((p / 'runtime.tar.gz').read_bytes()).hexdigest()
file = p / 'backend.sh'
file.write_bytes(re.sub(rb'expected=[0-9a-f]{64}', b'expected=' + runtime_hash.encode(), file.read_bytes()))
file = p / 'launch.sh'
text = re.sub(rb'EXPECTED=[0-9a-f]{64}', b'EXPECTED=' + hashlib.sha256((p / 'dovedex').read_bytes()).hexdigest().encode(), file.read_bytes())
text = text[:text.index(b'exec "$RAM"')] if b'exec "$RAM"' in text else text[:text.index(b'RUNHASH=')]
run_hash = hashlib.sha256((p / 'dovedex-run').read_bytes()).hexdigest()
text += f'''RUNHASH={run_hash}
hash=$(sha256sum "$B/dovedex-run");test "${{hash%% *}}" = "$RUNHASH"
cp "$B/dovedex-run" /tmp/dovedex-ui-run.new
chmod 700 /tmp/dovedex-ui-run.new;mv /tmp/dovedex-ui-run.new /tmp/dovedex-ui-run
exec /tmp/dovedex-ui-run --exec "$RAM" "$@"
'''.encode()
file.write_bytes(text)
names = ['runtime.tar.gz', 'backend.sh', 'native-decoder.sh', 'dovedex-run', 'dovedex', 'launch.sh']
(p / 'final.sha256').write_bytes(''.join(hashlib.sha256((p / name).read_bytes()).hexdigest() + '  ' + name + '\n' for name in names).encode())
print('Verified package:', runtime_hash)
