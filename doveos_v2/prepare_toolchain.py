"""Adapt the existing isolated Focal ARM toolchain to Buildroot's sysroot layout."""
import os
from pathlib import Path
import shutil
import sys

destination = Path(sys.argv[1])
source = Path('/opt/lapdock-focal-cross/usr')
target = source / 'arm-linux-gnueabihf'
sysroot = destination / 'sysroot'
(destination / 'bin').mkdir(parents=True, exist_ok=True)
if not sysroot.exists():
    sysroot.mkdir()
    shutil.copytree(target / 'lib', sysroot / 'lib', symlinks=True)
    (sysroot / 'usr').mkdir()
    shutil.copytree(target / 'include', sysroot / 'usr/include', symlinks=True)
    (sysroot / 'usr/lib').symlink_to('../lib')
for program in (source / 'bin').glob('arm-linux-gnueabihf-*'):
    output = destination / 'bin' / program.name
    if program.name.rsplit('-', 1)[-1] in ('gcc', 'g++', 'cpp', 'c++', 'cc'):
        output.write_text('#!/bin/sh\nexec "' + str(program) + '" --sysroot="' + str(sysroot) + '" "$@"\n')
        output.chmod(0o755)
    elif not output.exists():
        output.symlink_to(program)
print('External ARM toolchain prepared; glibc 2.31, minimum kernel ABI 3.2')
