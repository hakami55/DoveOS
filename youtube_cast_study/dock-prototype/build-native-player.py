"""Private network-capable DovePlayer variant. Does not alter the local-file app."""
from pathlib import Path
import subprocess, os, hashlib, json
b=Path('/mnt/c/vscode/HPlapdock');d=b/'youtube_cast_study/dock-prototype';q=b/'doveos_v2/doveplayer'
s=(q/'player.c').read_text()
s=s.replace('static const char *filename;', 'static const char *filename,*audio_filename;')
s=s.replace('static atomic_int stopping,failed,audio_ready,audio_finished,video_finished;',
 '''static atomic_int stopping,failed,audio_ready,audio_finished,video_finished;
static atomic_int cast_volume=50;''')
s=s.replace('static double now(void)', '''static void cast_update_volume(void){
 FILE *vol=fopen("/tmp/doveplayer/cast-volume","r");int gain=50;
 if(vol){if(fscanf(vol,"%d",&gain)==1&&gain>=0&&gain<=100)atomic_store(&cast_volume,gain);fclose(vol);}
}
static int cast_loopback_url(const char *url){
 if(!url||strncmp(url,"http://127.0.0.1:",17)||strpbrk(url,"\\r\\n"))return 0;
 char *end;long port=strtol(url+17,&end,10);
 return end>url+17&&*end=='/'&&port>0&&port<=65535;
}
static double now(void)''')
s=s.replace(' *next_pts=pts+(double)count/48000;', ''' int gain=atomic_load(&cast_volume);
 if(gain<100){int16_t *samples=(int16_t*)pcm;for(int i=0;i<count*2;i++)samples[i]=(int16_t)((int32_t)samples[i]*gain/100);}
 *next_pts=pts+(double)count/48000;''')
s=s.replace('if(now()-report>=.25){save_state("Playing");report=now();}',
 '''if(now()-report>=.25){
   cast_update_volume();
   save_state("Playing");report=now();}''')
s=s.replace('av_dict_set(&options,"protocol_whitelist","file",0);',
 '''av_dict_set(&options,"protocol_whitelist","http,tcp",0);
 av_dict_set(&options,"rw_timeout","45000000",0);
 av_dict_set(&options,"probesize","262144",0);''')
s=s.replace('avformat_open_input(ctx,filename,NULL,&options)', 'avformat_open_input(ctx,ctx==&af?audio_filename:filename,NULL,&options)')
begin=s.index(' char *resolved=realpath(argv[1],NULL);')
end=s.index(' filename=resolved;',begin)
s=s[:begin]+''' const char *au=getenv("DOVE_YT_AUDIO");
 if(!cast_loopback_url(argv[1])||!cast_loopback_url(au)){
  puts("CAST_INPUT_ERROR private loopback URLs required");return 2;}
 char *resolved=strdup(argv[1]);audio_filename=au;
 if(!resolved)return 2;
 cast_update_volume();
'''+s[end:]
s=s.replace('audio_index=use_audio?av_find_best_stream(vf,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0):-1;', 'audio_index=use_audio?0:-1;')
s=s.replace('audio_index=av_find_best_stream(af,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0);',
 'audio_index=av_find_best_stream(af,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0);\n  if(audio_index<0){fail("audio stream unavailable");goto finish;}')
old=''' int eof=0;
 while(!atomic_load(&stopping)&&!action){
  if(!eof){int rc=av_read_frame(af,p);if(rc<0){if(rc!=AVERROR_EOF&&!action){fail("audio read");break;}eof=1;}
   else if(p->stream_index!=audio_index){av_packet_unref(p);continue;}}
  int rc=avcodec_send_packet(ac,eof?NULL:p);av_packet_unref(p);
  if(rc<0&&rc!=AVERROR_EOF){fail("audio packet decode");break;}'''
new=''' int eof=0,consecutive_bad=0,window_bad=0,total_bad=0;double error_window=now();
 while(!atomic_load(&stopping)&&!action){
  if(!eof){int rc=av_read_frame(af,p);if(rc<0){if(rc!=AVERROR_EOF&&!action){fail("audio read");break;}eof=1;}
   else if(p->stream_index!=audio_index){av_packet_unref(p);continue;}}
  int rc=avcodec_send_packet(ac,eof?NULL:p);
  if(rc==AVERROR_INVALIDDATA&&!eof){
   if(now()-error_window>60){error_window=now();window_bad=0;}
   consecutive_bad++;window_bad++;total_bad++;
   fprintf(stderr,"CAST_AUDIO_BAD_PACKET code=%d pts=%lld pos=%lld bytes=%d consecutive=%d minute=%d total=%d\\n",
     rc,(long long)p->pts,(long long)p->pos,p->size,consecutive_bad,window_bad,total_bad);
   if(total_bad==1&&p->size>0&&p->size<=65536){
    FILE *bad=fopen("/tmp/doveos-youtube-live/audio-error.bin","wb");
    if(bad){fwrite(p->data,1,p->size,bad);fclose(bad);}
   }
   av_packet_unref(p);
   if(consecutive_bad>3||window_bad>10||total_bad>64){fail("repeated invalid audio packets");break;}
   avcodec_flush_buffers(ac);continue;
  }
  av_packet_unref(p);
  if(rc<0&&rc!=AVERROR_EOF){fprintf(stderr,"CAST_AUDIO_DECODE_CODE %d\\n",rc);fail("audio packet decode");break;}
  consecutive_bad=0;'''
assert s.count(old)==1,'Review upstream audio decode loop before applying recovery'
s=s.replace(old,new)
(d/'cast-player.c').write_text(s)
f=Path('/home/hakami55/doveos-mpv-trial-20261007/bundle/lib');h=Path('/home/hakami55/doveplayer-sdk/root/usr/include/arm-linux-gnueabihf')
env=os.environ.copy();env['LD_LIBRARY_PATH']='/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu'
subprocess.run(['/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc','--sysroot=/opt/lapdock-focal-cross','-O2','-std=gnu11','-Wall','-Wextra','-Wno-deprecated-declarations',
 '-march=armv7-a','-mfpu=neon-vfpv4','-mfloat-abi=hard','-I'+str(q),'-I'+str(h),str(d/'cast-player.c'),'-L'+str(f),'-Wl,-rpath-link,'+str(f),
 '-l:libavformat.so.58','-l:libavcodec.so.58','-l:libavutil.so.56','-l:libswresample.so.3','-lpthread','-lm','-o',str(d/'cast-player')],env=env,check=True)
w=(q/'play.sh').read_text()
w=w.replace('cp "$B/doveplayer-core" "$B/doveplayer-broker" "$B/doveplayer-audio" "$Q/"',
 '''cp "$B/doveplayer-broker" "$B/doveplayer-audio" "$Q/"
cp /media/usb0/DoveOS-v2/YouTubeTrial/cast-player "$Q/doveplayer-core"''')
(d/'cast-direct.sh').write_text(w)
(d/'player-provenance.json').write_text(json.dumps({'source':'doveos_v2/doveplayer/player.c','sha256':hashlib.sha256((q/'player.c').read_bytes()).hexdigest(),'changes':'Private loopback HTTP, separate audio source, volume control, bounded read timeout and invalid AAC packet recovery; production app unchanged'},indent=2))
print('CAST_PLAYER_BUILD_OK')
