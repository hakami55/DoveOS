// Public, unsigned-in YouTube media experiment. Client configuration follows
// yt-dlp's current youtube/_base.py VISIONOS entry (reviewed 2026-10-08).
export function selectOriginalAudio(formats,requiredTrackId) {
  const available=formats.filter(f=>f.url&&!f.drmFamilies&&!f.drmTrackType);
  const aac=available.filter(f=>/^audio\/mp4\b/.test(f.mimeType)&&/mp4a\.40\.2/.test(f.mimeType));
  const name=f=>typeof f.audioTrack?.displayName==='string'?f.audioTrack.displayName.toLowerCase():'';
  const original=f=>name(f).includes('original')&&!name(f).includes('descriptive');
  const quality=f=>Number(f.bitrate)||0;
  const best=list=>list.sort((a,b)=>Number(Boolean(a.isDrc))-Number(Boolean(b.isDrc))||quality(b)-quality(a))[0];
  if(requiredTrackId){
    const same=best(aac.filter(f=>f.audioTrack?.id===requiredTrackId));
    if(!same)throw new Error('Original audio track changed or is unavailable. Restart this video.');
    return same;
  }
  const originals=formats.filter(original);
  if(originals.length){
    const ids=new Set(originals.map(f=>f.audioTrack?.id).filter(Boolean));
    const selected=best(aac.filter(f=>original(f)||ids.has(f.audioTrack?.id)));
    if(!selected)throw new Error('This video has no supported AAC version of its original audio.');
    return selected;
  }
  // Older, single-language videos have no track metadata. Never select a
  // descriptive alternative simply because it occurs first in the response.
  return best(aac.filter(f=>!f.audioTrack))||
    best(aac.filter(f=>f.audioTrack?.audioIsDefault&&!name(f).includes('descriptive')))||
    best(aac.filter(f=>!name(f).includes('descriptive')));
}
export async function resolveVisionMedia(videoId,{audioTrackId}={}) {
  if(!/^[A-Za-z0-9_-]{11}$/.test(videoId))throw new Error('Invalid video ID');
  const client={clientName:'VISIONOS',clientVersion:'1.02',deviceMake:'Apple',
    deviceModel:'RealityDevice17,1',osName:'visionOS',osVersion:'26.5.23O471',hl:'en',timeZone:'UTC',utcOffsetMinutes:0,
    userAgent:'Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15'};
  const page=await fetch('https://www.youtube.com/watch?v='+videoId,{signal:AbortSignal.timeout(20000)});
  if(!page.ok)throw new Error('YouTube session unavailable');
  // The session identifier occurs near the beginning. Cancel immediately once
  // found; retaining the multi-megabyte watch page strains the dock's RAM.
  let visitor,tail='',size=0;
  for await(const piece of page.body) {
    size+=piece.length;if(size>8*1024*1024)throw new Error('YouTube session page too large');
    tail+=new TextDecoder().decode(piece);
    visitor=/"VISITOR_DATA"\s*:\s*"([^"\\]+)"/.exec(tail)?.[1];
    if(visitor)break;
    tail=tail.slice(-8192);
  }
  if(!visitor)throw new Error('YouTube visitor session unavailable');
  const response=await fetch('https://www.youtube.com/youtubei/v1/player?prettyPrint=false',{
    method:'POST',headers:{'Content-Type':'application/json','User-Agent':client.userAgent,
      'X-YouTube-Client-Name':'101','X-YouTube-Client-Version':client.clientVersion,
      'Origin':'https://www.youtube.com','X-Goog-Visitor-Id':visitor},
    body:JSON.stringify({context:{client},videoId,contentCheckOk:true,racyCheckOk:true,
      playbackContext:{contentPlaybackContext:{html5Preference:'HTML5_PREF_WANTS'}}}),
    signal:AbortSignal.timeout(20000)});
  if(!response.ok)throw new Error('YouTube player HTTP '+response.status);
  const info=await response.json();
  if(info.playabilityStatus?.status!=='OK')throw new Error('YouTube video unavailable: '+String(info.playabilityStatus?.reason||info.error?.status||'unknown').slice(0,180));
  const formats=[...info.streamingData?.formats||[],...info.streamingData?.adaptiveFormats||[]];
  const video=formats.filter(f=>f.url&&!f.drmFamilies&&!f.drmTrackType&&/avc1/.test(f.mimeType)&&f.height<=720)
    .sort((a,b)=>b.height-a.height||(a.fps||30)-(b.fps||30))[0];
  const audio=selectOriginalAudio(formats,audioTrackId);
  if(!video||!audio)throw new Error('Public H.264/AAC formats unavailable');
  for(const f of [video,audio]) {
    const u=new URL(f.url);
    if(u.protocol!=='https:'||!u.hostname.endsWith('.googlevideo.com'))throw new Error('Unexpected media host');
    if(!Number.isSafeInteger(Number(f.contentLength))||Number(f.contentLength)<1)throw new Error('Media size unavailable');
  }
  return {videoUrl:video.url,audioUrl:audio.url,videoLength:Number(video.contentLength),audioLength:Number(audio.contentLength),
    videoType:video.mimeType,audioType:audio.mimeType,height:video.height,fps:video.fps,
    duration:Number(info.videoDetails?.lengthSeconds),videoCodec:'H.264',audioCodec:'AAC',sourceClient:'VISIONOS',videoId,
    videoItag:video.itag,audioItag:audio.itag,videoLastModified:video.lastModified,audioLastModified:audio.lastModified,
    audioTrackId:audio.audioTrack?.id||'',audioLanguage:audio.audioTrack?.id?.split('.')[0]||'',
    audioTrackName:typeof audio.audioTrack?.displayName==='string'?audio.audioTrack.displayName:'Original / single audio track'};
}
