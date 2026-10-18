import YouTubeCastReceiver from './yt-lite/lib/YouTubeCastReceiver.js';
import {networkInterfaces} from 'node:os';
import {writeFileSync,renameSync,existsSync,unlinkSync} from 'node:fs';
import {NativePlayer} from './native-player.mjs';
import {safeLogger,diagnostic} from './safe-diagnostics.mjs';
const Q='/tmp/doveos-youtube-live';
const address=networkInterfaces().wlan0?.find(x=>x.family==='IPv4'&&!x.internal)?.address;
if(!address)throw new Error('Wi-Fi has no address. Connect DoveOS to your router first.');
const player=new NativePlayer();let code='',pending=false,error='',stopping=false,recovering=false,attempts=0,recovery;
const trace=(event,details={})=>console.log('NATIVE_CONTROL',JSON.stringify({at:new Date().toISOString(),event,...details}));
let progressPending=false;
const receiverLogger={...safeLogger,
  debug(message,data){
    if(typeof message!=='string')return;
    if(message==='[DoveOS] connection-reason')trace('connection-reason',data);
    if(/^\[yt-cast-receiver\] '(setPlaylist|updatePlaylist)' message payload:$/.test(message)&&data){
      trace('playlist-shape',{hasList:Boolean(data.listId),hasVideo:Boolean(data.videoId),
        videoCount:typeof data.videoIds==='string'?data.videoIds.split(',').filter(Boolean).length:0,
        indexType:typeof data.currentIndex,index:Number.isFinite(Number(data.currentIndex))?Number(data.currentIndex):null});
    }
    const name=/Incoming message: '([A-Za-z0-9_]{1,64})'$/.exec(message)?.[1];
    if(name)trace('message',{name});
    const outgoing=/Sending messages? '([A-Za-z0-9_ +]{1,256})' with payload:$/.exec(message);
    if(outgoing)trace('sending',{names:outgoing[1]});
    const response=/Response received for sent messages? '([A-Za-z0-9_ +]{1,256})'\. Status: (\d{3})$/.exec(message);
    if(response){trace('acknowledged',{names:response[1],httpStatus:Number(response[2])});
      if(response[1].split(' + ').includes('onStateChange'))progressPending=false;}
    if(message==='[yt-cast-receiver] Resetting player...')trace('disconnect-reset');
  },
  info(message){const name=typeof message==='string'?/^\[yt-cast-receiver\] Player\.(reset|stop|pause|resume)\(\)$/.exec(message)?.[1]:null;
    if(name)trace('player-command',{name});}
};
const receiver=new YouTubeCastReceiver(player,{device:{name:'DoveCast',screenName:'DoveCast'},
  app:{resetPlayerOnDisconnectPolicy:'allExplicitlyDisconnected',preservePlaybackOnPlayOnPhone:true},
  dial:{port:8099,bindToAddresses:[address]},dataStore:false,logger:receiverLogger});
const pairing=receiver.getPairingCodeRequestService();
const status=()=>{
  const playback={[-1]:'Ready',1:'Playing',2:'Paused',3:'Loading video',4:'Stopped'}[player.status]||'Ready';
  const lines=[stopping?'Stopping receiver...':receiver.status==='running'?'Listening as DoveCast':'Connecting to YouTube...',
    code?'TV code: '+code:pending?'Requesting TV code...':'TV code unavailable. Click New TV code to retry.',
    'Wi-Fi: '+address+' | Connected phones: '+receiver.getConnectedSenders().length,
    playback+(player.duration?' | '+Math.floor(player.position)+' / '+Math.floor(player.duration)+' sec':''),
    error||player.lastError||'720p hardware playback. Next video: use Add to queue / Play next.'];
  writeFileSync(Q+'/status.next',lines.join('\n')+'\n',{mode:0o600});renameSync(Q+'/status.next',Q+'/status.txt');
};
pairing.on('request',()=>{pending=true;});
pairing.on('response',value=>{pending=false;const digits=String(value).replace(/[\s-]/g,'');
  if(receiver.status==='running'&&/^\d{12}$/.test(digits)){code=digits.match(/.{1,3}/g).join(' ');error='';console.log('NATIVE_TV_CODE_READY');}
  else{code='';error='Could not obtain a valid TV code.';}});
pairing.on('error',()=>{pending=false;code='';error='TV code request failed. Check internet and retry.';});
receiver.on('error',e=>{error='YouTube connection error.';console.error('NATIVE_CONTROL_ERROR',JSON.stringify(diagnostic(e)));});
receiver.on('senderConnect',()=>{attempts=0;error='';progressPending=false;trace('phone-connected');});
receiver.on('senderDisconnect',(_sender,implicit)=>trace('phone-disconnected',{implicit:Boolean(implicit),position:player.position}));
receiver.on('terminate',e=>{pairing.stop();code='';pending=false;error='YouTube connection dropped; retrying.';
  console.error('NATIVE_CONTROL_DROPPED',JSON.stringify(diagnostic(e)));schedule();});
function schedule(){if(stopping||recovering||recovery||attempts>=3)return;recovery=setTimeout(()=>{recovery=null;void recover();},2000*2**attempts);}
async function recover(){if(stopping||recovering)return;recovering=true;attempts++;
  try{await receiver.start();if(stopping){await receiver.stop();return;}error='';pairing.start();}
  catch(e){error='Cannot connect to YouTube. Check internet and click New TV code.';console.error('NATIVE_RETRY_FAILED',JSON.stringify(diagnostic(e)));}
  finally{recovering=false;if(receiver.status!=='running')schedule();}}
let polling=false;
const poll=setInterval(async()=>{if(polling||stopping)return;polling=true;
  try{if(existsSync(Q+'/stop')){void stop();return;}
    if(existsSync(Q+'/pair')){unlinkSync(Q+'/pair');code='';
      if(receiver.status==='running'){pairing.stop();pairing.start();}else{attempts=0;schedule();}}
    await player.syncState();player.position=await player.doGetPosition();
    if(!progressPending&&receiver.getConnectedSenders().length){
      progressPending=true;
      if(!await player.reportProgress())progressPending=false;
    }
    status();
  }catch(e){console.error('NATIVE_STATE_ERROR',JSON.stringify(diagnostic(e)));}
  finally{polling=false;}},500);
async function stop(){if(stopping)return;stopping=true;clearInterval(poll);clearTimeout(recovery);pairing.stop();status();
  let failed=false;
  try{await player.close();await receiver.stop();}catch(e){failed=true;console.error('NATIVE_STOP_ERROR',JSON.stringify(diagnostic(e)));}
  finally{code='';writeFileSync(Q+'/status.txt',failed?'Receiver cleanup needs attention. Keep the dock powered.\n':'Receiver stopped. DoveOS is ready.\n',{mode:0o600});process.exit(failed?1:0);}}
process.once('SIGTERM',()=>void stop());process.once('SIGINT',()=>void stop());
status();await recover();
