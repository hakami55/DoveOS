import apt
import subprocess
from pathlib import Path
base=Path('/home/hakami55/doveos-v2-build')
cache=apt.Cache()
file=cache['libxdamage-dev:armhf'].candidate.fetch_binary(str(base/'desktop-debs'))
subprocess.run(['dpkg-deb','-x',file,str(base/'desktop-extracted')],check=True)
