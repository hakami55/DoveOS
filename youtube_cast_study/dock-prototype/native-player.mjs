import Player from './yt-lite/lib/Player.js';
import Constants from './yt-lite/lib/Constants.js';
import {spawn} from 'node:child_process';
import {readFileSync,writeFileSync,existsSync,mkdirSync,unlinkSync} from 'node:fs';
import {setTimeout as delay} from 'node:timers/promises';
import {resolveMedia,MediaProxy} from './media-source.mjs';
const Q='/tmp/doveplayer',script='/media/usb0/DoveOS-v2/YouTubeTrial/cast-direct.sh';
export class NativePlayer extends Player {
  get handlesAtomicReplacement(){return true;}
  constructor(){super();this.volume={level:50,muted:false};this.position=0;this.duration=0;this.lastError=null;this.working=false;this.child=null;this.stopping=false;this.commandQueue=Promise.resolve();this.closing=false;}
  snapshot(){
    try{const [state,pos,duration,shown,dropped]=readFileSync(Q+'/state','utf8').trim().split('\n');
      return {state,position:Number(pos)||0,duration:Number(duration)||0,shown:Number(shown)||0,dropped:Number(dropped)||0};
    }catch{return null;}
  }
  saveSession(reason){
    try{writeFileSync('/tmp/doveos-youtube-live/last-session.json',JSON.stringify({at:new Date().toISOString(),
      reason,videoId:this.videoId,position:this.position,state:this.snapshot(),bytes:this.proxy?.bytes,
      requests:this.proxy?.requests,failures:this.proxy?.failures,retries:this.proxy?.retries,refreshes:this.proxy?.refreshes,error:this.lastError})+'\n',{mode:0o600});}catch{}
  }
  command(fn){const task=this.commandQueue.then(async()=>{
    if(this.closing)return false;this.working=true;
    try{this.lastError=null;return await fn();}
    catch(e){this.lastError=e.message.replace(/https?:\/\/\S+/g,'[media]');
      try{await this.retire();this.saveSession('command-error');await this.proxy?.close();this.proxy=null;}catch{}return false;}
    finally{this.working=false;}});
    this.commandQueue=task.catch(()=>{});return task;
  }
  async retire(){
    const child=this.child;if(!child)return;
    this.stopping=true;
    if(child.exitCode===null)child.kill('SIGTERM');
    for(let i=0;i<300&&child.exitCode===null;i++)await delay(100);
    if(child.exitCode===null)throw new Error('Player is still releasing its display buffers.');
    this.position=this.snapshot()?.position??this.position;this.child=null;this.stopping=false;
    if(existsSync(Q+'/retained'))throw new Error('Decoder display recovery needs attention.');
  }
  async launch(position){
    if(this.closing)throw new Error('Receiver is stopping.');
    if(existsSync('/tmp/doveos-direct-display')||existsSync(Q+'/playing'))throw new Error('Another player owns the display.');
    mkdirSync(Q,{recursive:true});try{unlinkSync(Q+'/state');}catch{}
    await this.doSetVolume(this.volume);
    const child=spawn('/bin/sh',[script,this.urls.video,String(Math.max(0,position)),'0'],
      {env:{...process.env,DOVE_YT_AUDIO:this.urls.audio},stdio:['ignore','ignore','ignore']});
    this.child=child;let spawnError;child.once('error',e=>{spawnError=e;});
    for(let i=0;i<600;i++){
      if(spawnError)throw new Error('Could not launch the native player.');
      if(child.exitCode!==null)throw new Error('Native player could not start.');
      const s=this.snapshot();if(s?.state==='Playing'&&s.shown>0){this.position=s.position;return true;}
      await delay(100);
    }
    await this.retire();throw new Error('Native player startup timed out.');
  }
  async doPlay(video,position=0){return this.command(async()=>{
    if(!/^[A-Za-z0-9_-]{11}$/.test(video.id))throw new Error('Invalid YouTube video.');
    await this.retire();if(this.proxy)this.saveSession('replace');await this.proxy?.close();this.proxy=null;this.videoId=video.id;
    const media=await resolveMedia(video.id);this.duration=media.duration;this.position=position;
    this.proxy=new MediaProxy(media);this.urls=await this.proxy.start();global.gc?.();
    return this.launch(position);
  });}
  async doPause(){return this.command(async()=>{await this.retire();return true;});}
  async doResume(){return this.command(()=>this.launch(this.position));}
  async doSeek(position){return this.command(async()=>{
    const paused=this.status===Constants.PLAYER_STATUSES.PAUSED;
    await this.retire();this.position=Math.max(0,Math.min(this.duration-0.1,Number(position)||0));
    return paused?true:this.launch(this.position);
  });}
  async doStop(){return this.command(async()=>{await this.retire();this.saveSession('stop');await this.proxy?.close();this.proxy=null;return true;});}
  async doSetVolume(volume){
    this.volume={level:Math.max(0,Math.min(100,Number(volume.level)||0)),muted:Boolean(volume.muted)};
    mkdirSync(Q,{recursive:true});writeFileSync(Q+'/cast-volume',String(this.volume.muted?0:Math.round(this.volume.level))+'\n',{mode:0o600});return true;
  }
  async doGetVolume(){return this.volume;}
  async doGetPosition(){return this.child?this.snapshot()?.position??this.position:this.position;}
  async doGetDuration(){return this.duration;}
  async reportProgress(now=performance.now()){
    if(this.working||this.stopping||this.closing||!this.child||this.child.exitCode!==null||
       this.status!==Constants.PLAYER_STATUSES.PLAYING||now-(this.lastProgressAt??-Infinity)<5000)return false;
    this.lastProgressAt=now;
    await this.notifyExternalStateChange();
    return true;
  }
  async syncState(){
    if(this.working||this.stopping||!this.child)return;
    const child=this.child,s=this.snapshot();
    if(child.exitCode!==null){
      this.position=s?.position??this.position;this.child=null;
      if(child.exitCode===21)await this.notifyExternalStateChange(Constants.PLAYER_STATUSES.PAUSED);
      else {if(child.exitCode!==0)this.lastError='Native playback stopped with an error.';
        this.saveSession('player-exit-'+child.exitCode);
        await this.notifyExternalStateChange(Constants.PLAYER_STATUSES.STOPPED);await this.proxy?.close();this.proxy=null;}
    }
  }
  async close(){this.closing=true;await this.commandQueue;await this.retire();if(this.proxy)this.saveSession('receiver-close');await this.proxy?.close();this.proxy=null;}
}
