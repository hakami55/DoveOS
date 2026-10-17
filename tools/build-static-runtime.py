"""Build the immutable runtime tree from the release's exact asset archives.

Usage: python3 tools/build-static-runtime.py USB_ROOT OUTPUT_TREE
Create static-runtime.ext2 from OUTPUT_TREE with the documented ext2 builder.
"""
from pathlib import Path
import sys,tarfile,hashlib
usb,out=map(Path,sys.argv[1:]);base=usb/'DoveOS-v2'
assert not out.exists();out.mkdir(parents=True)
archives={'theme':base/'classic-theme.tar.gz','rgb':base/'GPU/rgb/runtime.tar.gz','cursors':base/'Cursors/runtime.tar.gz'}
theme_files={'sound-test.wav','sound-test-left.wav','sound-test-right.wav','jwm','doveos-timekeeper'}
all_entries=[]
for group,archive in archives.items():
    entries=[]
    with tarfile.open(archive) as source:
        for item in source:
            rel=item.name[2:] if item.name.startswith('./') else item.name
            assert not rel.startswith('/') and '..' not in Path(rel).parts
            if not item.isfile() or (group=='theme' and rel not in theme_files):continue
            data=source.extractfile(item).read();dest=out/group/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
            line=hashlib.sha256(data).hexdigest()+'  '
            entries.append(line+rel+'\n');all_entries.append(line+group+'/'+rel+'\n')
    assert entries
    (out/(group+'.sha256')).write_bytes(''.join(sorted(entries)).encode())
    (out/(group+'.source-sha256')).write_bytes((hashlib.sha256(archive.read_bytes()).hexdigest()+'\n').encode())
(out/'files.sha256').write_bytes(''.join(sorted(all_entries)).encode())
print('Prepared',len(all_entries),'immutable runtime files.')
