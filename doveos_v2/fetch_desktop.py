"""Stage verified Ubuntu Focal ARM GUI packages; never installs them on the dock."""
import apt
import apt.progress.base
import concurrent.futures
import json
from pathlib import Path
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
WORK = Path('/home/hakami55/doveos-v2-build')
CACHE = WORK / 'desktop-debs'
STAGE = WORK / 'desktop-extracted'
RUNTIME = WORK / 'desktop-runtime'
for folder in (CACHE, STAGE, RUNTIME):
    folder.mkdir(parents=True, exist_ok=True)
cache = apt.Cache()
roots = ['xvfb','jwm','pcmanfm','xterm','x11-apps','x11-utils','x11-xserver-utils',
         'x11-xkb-utils','xfonts-base','fonts-dejavu-core','tango-icon-theme','dbus',
         'libxtst6','libxfixes3','libx11-dev','libxtst-dev','libxfixes-dev','libxdamage-dev']
skip = {'debconf','debconf-2.0','dpkg','ucf','fontconfig-config','libc-dev-bin',
        'libc6-dev','linux-libc-dev','libc-dev','perl','perl-base','python3',
        'python3-minimal','sensible-utils','adduser','passwd','lsb-base',
        'init-system-helpers','x11-common','libpam-systemd','systemd','systemd-sysv'}
packages = {}
queue = list(roots)
while queue:
    name = queue.pop(0)
    if name in skip or name in packages:
        continue
    key = name + ':armhf'
    pkg = cache[key] if key in cache and cache[key].candidate else cache.get(name)
    if not pkg or not pkg.candidate:
        raise RuntimeError('Missing package: ' + name)
    candidate = pkg.candidate
    if candidate.architecture not in ('armhf', 'all'):
        raise RuntimeError('Wrong architecture: ' + name)
    packages[name] = candidate
    for dep in candidate.get_dependencies('Depends', 'PreDepends'):
        options = [d.name.split(':')[0] for d in dep.or_dependencies]
        if any(n in skip or n in packages or n in roots for n in options):
            continue
        if 'x-terminal-emulator' in options:
            queue.append('xterm')
            continue
        selected = next((n for n in options if (n+':armhf' in cache and cache[n+':armhf'].candidate)
                         or (n in cache and cache[n].candidate and cache[n].candidate.architecture=='all')), None)
        if not selected:
            raise RuntimeError('Unresolved dependency: ' + str(options))
        queue.append(selected)
print('Fetching', len(packages), 'verified ARM runtime packages', flush=True)
def fetch(item):
    name, version = item
    filename = version.fetch_binary(str(CACHE), progress=apt.progress.base.AcquireProgress())
    return name, filename
with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
    downloads = list(pool.map(fetch, packages.items()))
for name, filename in downloads:
    subprocess.run(['dpkg-deb', '-x', filename, str(STAGE)], check=True)
# Keep userspace and application data. Do not import service boot scripts.
for part in ('lib/arm-linux-gnueabihf', 'usr/lib/arm-linux-gnueabihf', 'usr/bin',
             'usr/share', 'usr/lib/menu-cache', 'usr/lib/X11', 'usr/lib/jwm',
             'etc/fonts', 'etc/gtk-2.0', 'etc/xdg', 'etc/dbus-1', 'etc/X11'):
    source = STAGE / part
    if source.exists():
        (RUNTIME / part).mkdir(parents=True, exist_ok=True)
        subprocess.run(['rsync', '-a', str(source)+'/', str(RUNTIME / part)+'/'], check=True)
# Drop build headers and manuals from the runtime image; provenance remains.
for part in ('usr/share/man','usr/share/locale','usr/lib/arm-linux-gnueabihf/dri'):
    path = RUNTIME / part
    if path.exists():
        shutil.rmtree(path)
for path in (RUNTIME / 'usr/lib/arm-linux-gnueabihf').glob('libLLVM*'):
    path.unlink()
(RUNTIME / 'lib').mkdir(exist_ok=True)
loader = RUNTIME / 'lib/ld-linux-armhf.so.3'
if not loader.exists():
    loader.symlink_to('arm-linux-gnueabihf/ld-linux-armhf.so.3')
for library in (STAGE / 'lib').glob('*.so*'):
    if library.name != 'ld-linux-armhf.so.3':
        shutil.copy2(library, RUNTIME / 'lib' / library.name, follow_symlinks=False)
manifest = {n: {'version':v.version,'architecture':v.architecture,'uri':v.uri}
            for n,v in packages.items()}
(HERE / 'artifacts/desktop-packages.json').write_text(json.dumps(manifest, indent=2))
(RUNTIME / '.ready').write_text('Verified Focal ARM GUI runtime\n')
subprocess.run(['update-mime-database',str(RUNTIME/'usr/share/mime')],check=True)
print('DESKTOP_RUNTIME_READY', flush=True)
