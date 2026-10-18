"""Private control-library copy without the optional recommendation parser."""
from pathlib import Path
import shutil,json,hashlib,tarfile
b=Path(__file__).resolve().parents[2];d=Path(__file__).resolve().parent
source=b/'pc_cast_bridge/node_modules/yt-cast-receiver';target=d/'yt-lite'
if json.loads((source/'package.json').read_text())['version']!='2.1.0':raise RuntimeError('Review new library version before packaging')
for p in (source/'dist').rglob('*.js'):
 q=target/p.relative_to(source/'dist');q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,q)
(target/'package.json').write_text(json.dumps({'type':'module','license':'MIT','author':'Patrick Kan','version':'2.1.0'}))
(target/'NOTICE.txt').write_text('Private modified copy of yt-cast-receiver 2.1.0 by Patrick Kan.\nhttps://github.com/patrickkfkan/yt-cast-receiver\nUpstream package declares the MIT license; no separate LICENSE file is present in the published package or repository.\nDefaultPlaylistRequestHandler, BindParams, Player, Session and YouTubeApp are modified for this DoveOS preview.\n')
shutil.copyfile(source/'package.json',target/'upstream-package.json')
(target/'lib/app/DefaultPlaylistRequestHandler.js').write_text('''// DoveOS preview: navigate only the playlist explicitly supplied by the phone.
// No recommendation lookups or watch-history updates. Original MIT library license retained.
import PlaylistRequestHandler from './PlaylistRequestHandler.js';
export default class PhoneQueueHandler extends PlaylistRequestHandler {
 async getPreviousNextVideos(target,playlist){
  const ids=playlist.videoIds,index=ids.indexOf(target.id);
  const video=i=>i>=0&&i<ids.length&&/^[A-Za-z0-9_-]{11}$/.test(ids[i])?
    {id:ids[i],client:target.client,context:{...target.context,index:i,playlistId:playlist.id}}:null;
  return {previous:video(index-1),next:index>=0?video(index+1):null};
 }
}
''')
# BrowserChannel AID acknowledges received arrays, not outgoing status messages.
# Reference: google/closure-library closure/goog/net/browserchannel.js,
# makeForwardChannelRequest_ and onInput_. Advancing it on send can skip commands.
bp=target/'lib/app/BindParams.js'
text=bp.read_text();old='''                if (!AID) {
                    this.AID++;
                }
'''
if text.count(old)!=1:raise RuntimeError('BindParams acknowledgement patch no longer matches')
bp.write_text(text.replace(old,'                // DoveOS: never acknowledge an unseen server message.\n'))
# The native adapter releases the old decoder in doPlay. Do not announce the
# replacement video as STOPPED between queue selection and LOADING.
for name,old,new in [
 ('lib/Player.js','if (this.status === PLAYER_STATUSES.PLAYING) {\n            await this.stop();',
  'if (this.status === PLAYER_STATUSES.PLAYING && !this.handlesAtomicReplacement) {\n            await this.stop();'),
 ('lib/app/YouTubeApp.js','if (__classPrivateFieldGet(this, _YouTubeApp_player, "f").status !== PLAYER_STATUSES.STOPPED) {',
  'if (!(stateAfterSet.current && __classPrivateFieldGet(this, _YouTubeApp_player, "f").handlesAtomicReplacement) && __classPrivateFieldGet(this, _YouTubeApp_player, "f").status !== PLAYER_STATUSES.STOPPED) {')]:
 p=target/name;text=p.read_text()
 if text.count(old)!=1:raise RuntimeError('Atomic replacement patch no longer matches '+name)
 p.write_text(text.replace(old,new))
# Current TV client replies to discovery and capability queries. Keep payloads
# minimal and truthful (no Cast v2 identity, account or party-game support).
p=target/'lib/app/Session.js';text=p.read_text();anchor='    get pairingCodeRequestService() {'
assert text.count(anchor)==1
p.write_text(text.replace(anchor,'''    get discoveryDeviceId() {
        return __classPrivateFieldGet(this, _Session_bindParams, "f").id;
    }
'''+anchor))
p=target/'lib/app/YouTubeApp.js';text=p.read_text();anchor="        case 'getNowPlaying': {"
assert text.count(anchor)==1
text=text.replace(anchor,'''        case 'getDiscoveryDeviceId': {
            const id = session.discoveryDeviceId;
            if (id) sendMessages.push(new Message(AID, 'setDiscoveryDeviceId', {discoveryDeviceId:id, loungeDeviceId:id}));
            break;
        }
        case 'getPartyGamesMode': {
            sendMessages.push(new Message(AID, 'onPartyGamesModeChanged', {isActive:false}));
            break;
        }
        case 'getReceiverStatus': {
            sendMessages.push(new Message(AID, 'receiverStatus', {}));
            break;
        }
        case 'onSetDiscoveryDeviceId': break;
'''+anchor)
anchor='                let handledConnectionEvent = false;'
assert text.count(anchor)==1
text=text.replace(anchor,'''                // Only enum/boolean metadata; never log sender IDs or raw payloads.
                const reason = connectionEventDetails?.disconnectBehavior;
                __classPrivateFieldGet(this, _YouTubeApp_logger, "f").debug('[DoveOS] connection-reason', {
                    ui: connectionEventDetails?.ui === 'true' || connectionEventDetails?.ui === true,
                    reason: typeof reason === 'string' && /^MDX_SESSION_DISCONNECT_BEHAVIOR_[A-Z_]{1,80}$/.test(reason) ? reason : 'unspecified'
                });
'''+anchor)
p.write_text(text)
# Match the current TV client's distinction between playing on the phone and
# stopping receiver playback. Opt-in: genuine disconnect/stop keeps its policy.
p=target/'lib/app/YouTubeApp.js';text=p.read_text()
for old,new in [
 ('        this.setResetPlayerOnDisconnectPolicy(options.resetPlayerOnDisconnectPolicy);',
  '        this.preservePlaybackOnPlayOnPhone = options.preservePlaybackOnPlayOnPhone === true;\n        this.setResetPlayerOnDisconnectPolicy(options.resetPlayerOnDisconnectPolicy);'),
 ("                        const isImplicitDisconnect = connectionEventDetails.ui !== 'true';",
  "                        const isImplicitDisconnect = connectionEventDetails.ui !== 'true' && connectionEventDetails.ui !== true;\n                        const preservePlayback = this.preservePlaybackOnPlayOnPhone && connectionEventDetails.disconnectBehavior === 'MDX_SESSION_DISCONNECT_BEHAVIOR_DISCONNECTED_BY_USER_PLAY_ON_PHONE';"),
 ('session, isImplicitDisconnect, AID));', 'session, isImplicitDisconnect, AID, preservePlayback));'),
 ('async function _YouTubeApp_handleSenderDisconnected(sender, session, implicit, AID) {',
  'async function _YouTubeApp_handleSenderDisconnected(sender, session, implicit, AID, preservePlayback = false) {'),
 ('        if (resetPlayer) {', '        if (resetPlayer && !preservePlayback) {')]:
 if text.count(old)!=1:raise RuntimeError('Play-on-phone patch no longer matches: '+old)
 text=text.replace(old,new)
p.write_text(text)
with tarfile.open(d/'yt-lite.tar.gz','w:gz') as t:t.add(target,arcname='yt-lite')
(d/'yt-lite-provenance.json').write_text(json.dumps({'package':'yt-cast-receiver','version':'2.1.0',
 'license':'MIT','original_handler_sha256':hashlib.sha256((source/'dist/lib/app/DefaultPlaylistRequestHandler.js').read_bytes()).hexdigest(),
 'change':'Sender queue, received AID, opt-in atomic replacement, discovery/status replies, sanitized disconnect reason, opt-in preservation on play-on-phone.'},indent=2))
print('YT_LITE_READY')
