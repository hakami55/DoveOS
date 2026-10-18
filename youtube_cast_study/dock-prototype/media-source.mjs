import { createServer } from 'node:http';
import { randomBytes } from 'node:crypto';
import { once } from 'node:events';
import {resolveVisionMedia} from './vision-source.mjs';
export async function resolveMedia(id,options){
  for(let attempt=0;attempt<2;attempt++){
    try{return await resolveVisionMedia(id,options);}
    catch(e){if(attempt||!['ENOTFOUND','EAI_AGAIN','ECONNRESET','ETIMEDOUT'].includes(e.cause?.code))throw e;
      await new Promise(r=>setTimeout(r,1500));}
  }
}

// TLS stays in Node; the dock's older VLC reads only a private loopback URL.
// Streaming and backpressure keep RAM bounded; no complete video is downloaded.
export class MediaProxy {
  constructor(media) {
    this.media=media;this.token=randomBytes(20).toString('hex');this.bytes={video:0,audio:0};
    this.requests={video:0,audio:0};this.controllers=new Set();this.failures=[];this.retries=[];this.refreshes=0;this.refreshPromise=null;
    this.server=createServer((req,res)=>void this.handle(req,res));
  }
  async start() {
    await new Promise((resolve,reject)=>{this.server.once('error',reject);this.server.listen(0,'127.0.0.1',resolve);});
    const origin=`http://127.0.0.1:${this.server.address().port}/${this.token}`;
    return {video:origin+'/video',audio:origin+'/audio'};
  }
  async refreshMedia(previous) {
    if(this.media!==previous)return;
    if(!previous.videoId)throw new Error('Media refresh unavailable');
    if(!this.refreshPromise)this.refreshPromise=(async()=>{
      const fresh=await resolveMedia(previous.videoId,{audioTrackId:previous.audioTrackId});
      if((fresh.audioTrackId||'')!==(previous.audioTrackId||''))throw new Error('Audio language changed; restart playback');
      for(const kind of ['video','audio'])for(const field of ['Length','Type','Itag','LastModified'])
        if(fresh[kind+field]!==previous[kind+field])throw new Error('Media format changed; restart playback');
      this.media=fresh;this.refreshes++;
    })().finally(()=>{this.refreshPromise=null;});
    await this.refreshPromise;
  }
  async readChunk(kind,offset,last,parent) {
    const size=this.media[kind+'Length'],length=last-offset+1;
    let refreshed=false;
    for(let attempt=0;attempt<3;attempt++) {
      const control=new AbortController(),signal=AbortSignal.any([parent.signal,control.signal]);
      const timer=setTimeout(()=>control.abort(),10000);
      let upstream;const source=this.media;
      try {
        let target=new URL(source[kind+'Url']);
        for(let hop=0;hop<5;hop++) {
          if(target.protocol!=='https:' || !target.hostname.endsWith('.googlevideo.com'))throw new Error('Media redirect rejected');
          upstream=await fetch(target,{redirect:'manual',headers:{Range:`bytes=${offset}-${last}`},signal});
          if(![301,302,303,307,308].includes(upstream.status))break;
          const location=upstream.headers.get('location');await upstream.body?.cancel();
          if(!location)throw new Error('Missing media redirect');target=new URL(location,target);
        }
        if(upstream.status!==206) {
          const error=new Error('Media range refused');error.httpStatus=upstream.status;throw error;
        }
        if(upstream.headers.get('content-range')!==`bytes ${offset}-${last}/${size}`)throw new Error('Incorrect media range');
        const block=Buffer.allocUnsafe(length);let received=0;
        for await(const piece of upstream.body) {
          if(received+piece.length>length)throw new Error('Oversized media range');
          block.set(piece,received);received+=piece.length;
        }
        if(received!==length)throw new Error('Incomplete media range');
        return block;
      } catch(error) {
        control.abort();try{await upstream?.body?.cancel();}catch{}
        if(!parent.signal.aborted&&!refreshed&&attempt<2&&[403,410].includes(error.httpStatus)&&source.videoId){
          clearTimeout(timer);refreshed=true;await this.refreshMedia(source);continue;
        }
        const fatalStatus=error.httpStatus&&error.httpStatus<500&&error.httpStatus!==429;
        if(parent.signal.aborted||attempt===2||fatalStatus)throw error;
        if(this.retries.length<32)this.retries.push({kind,offset,attempt:attempt+1});
        await new Promise(r=>setTimeout(r,500*(attempt+1)));
      } finally {clearTimeout(timer);}
    }
  }
  async handle(req,res) {
    const kind=req.url===`/${this.token}/video`?'video':req.url===`/${this.token}/audio`?'audio':null;
    if(!kind || !['GET','HEAD'].includes(req.method)) {res.writeHead(404);res.end();return;}
    if(req.headers.range && !/^bytes=\d+-\d*$/.test(req.headers.range)) {res.writeHead(416);res.end();return;}
    const control=new AbortController();this.controllers.add(control);
    res.on('close',()=>{control.abort();this.controllers.delete(control);});
    let stage='request';
    try {
      this.requests[kind]++;
      const size=this.media[kind+'Length'];
      const range=/^bytes=(\d+)-(\d*)$/.exec(req.headers.range||'');
      const start=range?Number(range[1]):0,end=range&&range[2]?Math.min(Number(range[2]),size-1):size-1;
      if(!Number.isSafeInteger(start)||start>end||start>=size) {res.writeHead(416,{'Content-Range':`bytes */${size}`});res.end();return;}
      const headers={'Content-Type':this.media[kind+'Type'],'Content-Length':end-start+1,'Accept-Ranges':'bytes'};
      if(range)headers['Content-Range']=`bytes ${start}-${end}/${size}`;
      if(req.method==='HEAD') {res.writeHead(range?206:200,headers);res.end();return;}
      // Finish and verify each <=1 MiB upstream request before playback pacing.
      // Otherwise slow AAC consumption holds Google's socket under backpressure
      // for tens of seconds; a socket close can expose a truncated AAC packet.
      // Retry a whole chunk before emitting any of it, so bytes cannot duplicate.
      for(let offset=start;offset<=end;offset+=1048576) {
        const last=Math.min(offset+1048575,end);
        stage='upstream';const block=await this.readChunk(kind,offset,last,control);
        stage='local-transfer';
        if(!res.headersSent)res.writeHead(range?206:200,headers);
        for(let at=0;at<block.length;at+=65536) {
          if(control.signal.aborted)throw new Error('Local player disconnected');
          const piece=block.subarray(at,Math.min(at+65536,block.length));this.bytes[kind]+=piece.length;
          if(!res.write(piece))await once(res,'drain',{signal:control.signal});
        }
      }
      res.end();
    } catch(error) {
      if(!control.signal.aborted&&this.failures.length<32)
        this.failures.push({kind,stage,type:error.name,httpStatus:error.httpStatus,code:['ECONNRESET','ETIMEDOUT','UND_ERR_CONNECT_TIMEOUT','UND_ERR_SOCKET'].includes(error.cause?.code)?error.cause.code:undefined});
      if(!res.headersSent){res.writeHead(502);res.end();}else res.destroy();
    }
  }
  async close() {
    for(const c of this.controllers)c.abort();this.controllers.clear();
    this.server.closeAllConnections();await new Promise(resolve=>this.server.close(resolve));
  }
}
