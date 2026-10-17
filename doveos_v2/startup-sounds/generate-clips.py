from pathlib import Path
import math,wave,array,json,hashlib,struct
R=Path(__file__).resolve().parents[1];out=R/'startup-sounds/clips';out.mkdir(parents=True,exist_ok=True)
rate=48000
presets={
'dove-dawn':(2.0,[(0.0,0.8,523.25,.22),(.23,.95,659.25,.20),(.48,1.1,783.99,.19),(1.18,.6,261.63,.07)]),
'dove-wings':(1.6,[(i*.13,.65,f,.15) for i,f in enumerate([587.33,739.99,880,1174.66])]),
'dove-nest':(1.9,[(0,1.4,349.23,.16),(0,1.4,440,.13),(.22,1.35,523.25,.13),(.8,.9,261.63,.1)])}
def write(name,frames):
 with wave.open(str(out/(name+'.wav')),'wb') as w:w.setparams((2,2,rate,0,'NONE','not compressed'));w.writeframes(frames)
for name,(duration,notes) in presets.items():
 a=array.array('h')
 for i in range(int(duration*rate)):
  t=i/rate;v=0.
  for start,length,f,amp in notes:
   x=t-start
   if 0<=x<length:
    attack=min(1,x/.035);tail=math.sin(math.pi*x/length)**.65;env=attack*tail*math.exp(-2.6*x/length)
    v+=amp*env*(math.sin(2*math.pi*f*x)+.2*math.sin(2*math.pi*2*f*x))
  sample=int(max(-.8,min(.8,v))*32767);a.extend((sample,sample))
 write(name,a.tobytes())
events={'information':(659.25,783.99),'warning':(440,349.23),'question':(523.25,659.25),'error':(293.66,220)}
for event,notes in events.items():
 a=array.array('h')
 for i in range(int(.34*rate)):
  t=i/rate;f=notes[0 if t<.17 else 1];x=t%.17
  v=.14*math.sin(math.pi*x/.17)**2*math.sin(2*math.pi*f*x)
  sample=int(v*32767);a.extend((sample,sample))
 write('dove-'+event,a.tobytes())
manifest={f.name:{'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in out.glob('*.wav')}
(out.parent/'clips-manifest.json').write_bytes((json.dumps(manifest,indent=2)+'\n').encode())
print('Generated original DoveOS melodies and event tones.')
