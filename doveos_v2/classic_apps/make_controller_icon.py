"""Original pixel gamepad artwork for the native classic icon set."""
from pathlib import Path
import struct,zlib,binascii
HERE=Path(__file__).resolve().parent/'icons'
shape=[(5,5),(18,5),(21,8),(23,18),(21,21),(18,21),(15,17),(8,17),(5,21),(2,21),(0,18),(2,8)]
def inside(x,y):
 hit=False
 for i,(a,b) in enumerate(shape):
  c,d=shape[i-1]
  if (b>y)!=(d>y) and x<(c-a)*(y-b)/(d-b)+a:hit=not hit
 return hit
pix=[[(0,0,0,0) for x in range(24)] for y in range(24)]
for y in range(24):
 for x in range(24):
  if inside(x+.5,y+.5):
   color=(192,192,192,255)
   if not inside(x+.5,y-.5) or not inside(x-.5,y+.5):color=(255,255,255,255)
   if not inside(x+.5,y+1.5) or not inside(x+1.5,y+.5):color=(64,64,64,255)
   pix[y][x]=color
def rect(x,y,w,h,c):
 for yy in range(y,y+h):
  for xx in range(x,x+w):pix[yy][xx]=c+(255,)
def circle(x,y,r,c):
 for yy in range(max(0,y-r),min(24,y+r+1)):
  for xx in range(max(0,x-r),min(24,x+r+1)):
   if (xx-x)**2+(yy-y)**2<=r*r:pix[yy][xx]=c+(255,)
rect(5,8,2,7,(0,0,0));rect(3,10,6,2,(0,0,0))
for x,y,c in [(18,8,(0,80,200)),(20,10,(190,170,0)),(16,10,(0,130,0)),(18,12,(180,0,0))]:circle(x,y,1,c)
rect(10,9,2,1,(80,80,80));rect(13,9,2,1,(80,80,80))
circle(9,14,2,(96,96,96));circle(9,14,1,(0,0,0));circle(14,14,2,(96,96,96));circle(14,14,1,(0,0,0))
def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',binascii.crc32(kind+data)&0xffffffff)
for size in (16,24,48):
 raw=b''.join(b'\0'+b''.join(bytes(pix[y*24//size][x*24//size]) for x in range(size)) for y in range(size))
 png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',size,size,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
 (HERE/f'controller-{size}.png').write_bytes(png)
