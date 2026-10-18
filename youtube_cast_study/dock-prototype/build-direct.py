"""Build an isolated HTTP-to-hardware trial from the established display lease code."""
from pathlib import Path
import subprocess, shutil, re
b=Path('/mnt/c/vscode/HPlapdock')
d=b/'youtube_cast_study/dock-prototype'
s=(b/'airplay_receiver_study/airplay_hw_bridge.c').read_text()
s=s.replace('#include "../moonlight_dock_prototype/hw_video.h"', '#include "../../moonlight_dock_prototype/hw_video.h"')
s=s.replace('#include "airplay_sps.h"', '#include "../../airplay_receiver_study/airplay_sps.h"')
s=s.replace('failed=airplay_normalize_sps(frame,size,&coded,&coded_size);\n        if(!failed)failed=hw_submit(coded,coded_size);\n        free(coded);', 'failed=hw_submit(frame,size);')
start=s.index('    const char *pipeline_text=')
end=s.index('    GstElement *pipeline=',start)
s=s[:start]+'''    const char *pipeline_text=
      "souphttpsrc name=video ! qtdemux ! h264parse config-interval=-1 "
      "! video/x-h264,stream-format=(string)byte-stream,alignment=(string)au "
      "! appsink name=out emit-signals=true sync=true max-buffers=2 drop=false "
      "souphttpsrc name=audio ! qtdemux ! avdec_aac ! audioconvert ! audioresample "
      "! audio/x-raw,format=S16LE,rate=48000,channels=2 "
      "! volume name=volume volume=0 ! alsasink device=plughw:RTKAlsaCard,0 "
      "buffer-time=100000 latency-time=10000";
'''+s[end:]
mark='    GstElement *sink=gst_bin_get_by_name(GST_BIN(pipeline),"out");'
s=s.replace(mark,'''    const char *video_url=getenv("DOVE_YT_VIDEO"),*audio_url=getenv("DOVE_YT_AUDIO");
    if(!video_url||!audio_url||strncmp(video_url,"http://127.0.0.1:",17)||
       strncmp(audio_url,"http://127.0.0.1:",17))return 2;
    GstElement *v=gst_bin_get_by_name(GST_BIN(pipeline),"video");
    GstElement *a=gst_bin_get_by_name(GST_BIN(pipeline),"audio");
    g_object_set(v,"location",video_url,NULL);g_object_set(a,"location",audio_url,NULL);
    gst_object_unref(v);gst_object_unref(a);
    if(claim_display()||start_decoder(1280,720)){release_display();return 3;}
'''+mark)
s=s.replace('    for(unsigned i=0;i<duration*20&&!ended;i++){','''    uint64_t trial_start=LiGetMicroseconds();
    for(unsigned i=0;i<duration*20&&!ended;i++){
        if(i%20==0){gint64 pos=0;gst_element_query_position(pipeline,GST_FORMAT_TIME,&pos);
            printf("YOUTUBE_PROGRESS elapsed_ms=%llu position_ms=%lld frames=%u\\n",
                (unsigned long long)((LiGetMicroseconds()-trial_start)/1000),
                (long long)(pos/1000000),input_frames);}
''')
(d/'youtube-direct.c').write_text(s)
env=__import__('os').environ.copy()
env['PKG_CONFIG_LIBDIR']='/usr/lib/arm-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig'
inc=b/'hp_lapdock_usb_research/wireless_updater_20260927/decoder_probe/include'
src=b/'moonlight_dock_prototype/src'
flags=subprocess.check_output(['pkg-config','--cflags','--libs','gstreamer-app-1.0'],env=env,text=True).split()
subprocess.run(['arm-linux-gnueabihf-gcc','-O2','-Wall','-Wno-unused-function','-march=armv7-a','-mfpu=neon-vfpv4','-mfloat-abi=hard',
 '-I'+str(inc),'-I'+str(src/'third_party/h264bitstream'),'-I'+str(src/'third_party/moonlight-common-c/src'),'-I'+str(src/'libgamestream'),
 str(d/'youtube-direct.c'),*[str(src/'third_party/h264bitstream'/n) for n in ['h264_stream.c','h264_nal.c','h264_sei.c']],*flags,
 '-lm','-lpthread','-ldl','-rdynamic','-o',str(d/'youtube-direct')],env=env,check=True)
# Only extra HTTP/MP4 plugins and dependencies absent from the existing AirPlay image.
r=Path('/home/hakami55/doveos-youtube-extra-20261008');r.mkdir(exist_ok=True)
(r/'plugins').mkdir(exist_ok=True);(r/'lib').mkdir(exist_ok=True)
air=Path('/home/hakami55/doveos-airplay-runtime-20261005')
pending=[]
for name in ['soup','isomp4']:
 p=Path('/usr/lib/arm-linux-gnueabihf/gstreamer-1.0')/('libgst'+name+'.so')
 shutil.copyfile(p,r/'plugins'/p.name);pending.append(p)
known=set()
while pending:
 p=pending.pop()
 for name in re.findall(r'Shared library: \[([^]]+)\]',subprocess.check_output(['readelf','-d',str(p)],text=True)):
  if name in known or (air/'lib'/name).exists():continue
  known.add(name)
  q=next((x/name for x in [Path('/lib/arm-linux-gnueabihf'),Path('/usr/lib/arm-linux-gnueabihf')] if (x/name).exists()),None)
  if q is None:raise RuntimeError('Missing library '+name)
  shutil.copyfile(q,r/'lib'/name);pending.append(q)
subprocess.run(['tar','-czf',str(d/'http-extra.tar.gz'),'-C',str(r),'.'],check=True)
print('DIRECT_BUILD_OK',sum(p.stat().st_size for p in r.rglob('*') if p.is_file()))
