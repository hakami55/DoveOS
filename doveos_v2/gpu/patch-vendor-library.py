"""Apply the measured compatibility changes to an exact user-supplied driver.

Usage: python3 patch-vendor-library.py INPUT_DRIVER OUTPUT_DRIVER
This file contains patch logic only. Obtain and use the driver under its license.
"""
from pathlib import Path
import sys,hashlib
source,target=map(Path,sys.argv[1:])
data=bytearray(source.read_bytes())
assert hashlib.sha256(data).hexdigest()=='6759b7faf7d98a3eaea52956b11ec34386636aed006ad0875d63f1b3e623010e'
assert data[0x81804:0x81810]==bytes.fromhex('b3f5807f18bf6ff00100f5d1')
data[0x81804:0x8180e]=bytes.fromhex('23f00103b3f5807f00bf')
assert data[0x7db36:0x7db38]==bytes.fromhex('0323')
data[0x7db36:0x7db38]=bytes.fromhex('0223')
assert hashlib.sha256(data).hexdigest()=='d58c02391d27a72570d5989b6f1746280f3fe70640489dc9ede72ee010d5fd85'
with target.open('xb') as output:output.write(data)
print('Exact compatibility-patched driver created.')
