// AKENO STREAM PS5 - AKENO STREAM's own pages for the embedded browser.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// youtube: the official YouTube IFrame Player API
//   (https://developers.google.com/youtube/iframe_api_reference), large
//   TV buttons, and reports of what the player did. Nothing is extracted:
//   the video plays in YouTube's own embedded player.
// captest: the playback lab - measures what the console's browser engine
//   supports (codecs, Media Source Extensions, Encrypted Media Extensions,
//   storage that survives a restart) and really plays AKENO STREAM's own clip
//   as an MP4 file, through MediaSource, as an HLS playlist and inside a frame
//   from another origin, reporting each result.
// frame: the lab's cross-origin frame (localhost instead of 127.0.0.1).
// The scripts send only these results; they read nothing from other sites.
#include "web/local_pages.hpp"

namespace akeno::web
{
std::string_view pages_css()
{
    return R"AKENO(
html,body{margin:0;height:100%;background:#0b0e17;color:#f4f6fb;
 font-family:-apple-system,"SF Pro Text","Helvetica Neue",Arial,sans-serif;font-size:26px}
body.player{display:flex;flex-direction:column;overflow:hidden}
#stage{flex:1;position:relative;background:#000}
#stage>div,#stage>iframe{position:absolute;inset:0;width:100%;height:100%;border:0}
#bar{display:flex;align-items:center;gap:18px;padding:18px 36px;background:#131a29;
 border-top:2px solid #2a3348}
.btn{font:inherit;font-weight:600;color:#f4f6fb;background:#212b42;border:3px solid transparent;
 border-radius:40px;padding:14px 30px;cursor:pointer;min-height:68px}
.btn.primary{background:#ff3d5a}
.btn:focus,.btn:hover{border-color:#fff;outline:none;background:#2b3754}
.btn.primary:focus,.btn.primary:hover{background:#ff5c74}
#status{margin-left:auto;color:#b1bbcf;font-size:22px;max-width:40%;text-align:right}
#message{position:absolute;left:10%;right:10%;top:30%;padding:40px;border-radius:24px;
 background:rgba(24,32,51,.96);font-size:30px;line-height:1.4;text-align:center}
main{max-width:1600px;margin:0 auto;padding:40px 60px 120px}
h1{font-size:46px;margin:0 0 8px}
.lead{color:#b1bbcf;margin:0 0 28px}
table{width:100%;border-collapse:collapse;font-size:22px}
th{text-align:left;color:#76819a;font-weight:600;padding:18px 10px 6px}
td{padding:10px;border-top:1px solid #2a3348;vertical-align:top}
td.s{width:150px;font-weight:700}
.yes{color:#35c79a}.no{color:#ff5c6c}.partial{color:#ffc247}.unknown{color:#76819a}.info{color:#5aa9ff}
#top{position:sticky;top:0;background:#0b0e17;padding:20px 0;display:flex;gap:18px;align-items:center;z-index:2}
video{width:480px;height:270px;background:#000;border-radius:16px}
#keys{color:#b1bbcf;font-size:22px}
)AKENO";
}

std::string_view youtube_page_html()
{
    return R"AKENO(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="referrer" content="strict-origin-when-cross-origin">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AKENO STREAM - YouTube</title>
<link rel="stylesheet" href="akeno.css">
</head><body class="player">
<div id="stage"><div id="player"></div><div id="message" hidden></div></div>
<div id="bar" role="toolbar" aria-label="Player controls">
<button id="back" class="btn" type="button">&#9664; Back to AKENO</button>
<button id="prev" class="btn" type="button" hidden>&#9198; Previous</button>
<button id="play" class="btn primary" type="button">&#9654; Play</button>
<button id="next" class="btn" type="button" hidden>Next &#9197;</button>
<button id="fs" class="btn" type="button">Full screen</button>
<button id="mute" class="btn" type="button">Mute</button>
<span id="status">Loading the YouTube player...</span>
</div>
<script src="youtube.js"></script>
</body></html>
)AKENO";
}

std::string_view youtube_page_js()
{
    return R"AKENO((function () {
'use strict';
var q = new URLSearchParams(location.search);
var vid = q.get('v') || '', list = q.get('list') || '', start = parseInt(q.get('t') || '0', 10) || 0;
if (!/^[A-Za-z0-9_-]{11}$/.test(vid)) vid = '';
if (!/^[A-Za-z0-9_-]{2,64}$/.test(list)) list = '';
if (start < 0 || start > 172800) start = 0;
var loadedAt = Date.now(), player = null, lastState = -2, everPlayed = false, sentErrors = 0;
var $ = function (id) { return document.getElementById(id); };

function send(type, value, detail) {
  try {
    var x = new XMLHttpRequest();
    x.open('POST', 'event', true);
    x.setRequestHeader('Content-Type', 'application/json');
    x.send(JSON.stringify({type: type, value: String(value == null ? '' : value),
                           detail: String(detail || '').slice(0, 200)}));
  } catch (e) {}
}
function backToApp() {
  send('state', 'exit', 'back button');
  try { if (player && player.pauseVideo) player.pauseVideo(); } catch (e) {}
  try { var x = new XMLHttpRequest(); x.open('POST', 'close', true); x.send(''); } catch (e) {}
  status('Returning to AKENO STREAM...');
}
function status(text) { $('status').textContent = text; }
function message(text) { var m = $('message'); m.textContent = text; m.hidden = !text; }

var reasons = {
  2: 'The video address is not valid.',
  5: 'The video cannot be played by this browser (HTML5 player error) - a codec or feature the console browser lacks.',
  100: 'The video was not found: it was removed or is private.',
  101: 'The owner of this video does not allow it to be played in other apps and sites.',
  150: 'The owner of this video does not allow it to be played in other apps and sites.',
  152: 'This video cannot be played in an embedded player.',
  153: 'YouTube did not accept the player\'s identification (missing HTTP Referer).'
};
var states = {'-1': 'unstarted', 0: 'ended', 1: 'playing', 2: 'paused', 3: 'buffering', 5: 'cued'};

window.addEventListener('error', function (e) {
  send('log', 'js-error', (e.message || '') + ' @' + String(e.filename || '').split('/').pop() + ':' + e.lineno);
});
send('loaded', '1', navigator.userAgent);
if (!vid && !list) { message('No video was given.'); send('error', 'no-video', 'no valid video or playlist id'); }

var apiTimer = setTimeout(function () {
  if (!(window.YT && window.YT.Player)) {
    send('api', 'timeout', 'YT.Player not available after 20 s');
    message('The YouTube player did not load. Check the network connection, then go back and try again.');
    status('Player not loaded');
  }
}, 20000);
var tag = document.createElement('script');
tag.src = 'https://www.youtube.com/iframe_api';
tag.onerror = function () {
  clearTimeout(apiTimer);
  send('api', 'failed', 'https://www.youtube.com/iframe_api could not be loaded');
  message('The YouTube player could not be loaded (network or certificate problem).');
  status('Player not loaded');
};
document.head.appendChild(tag);

window.onYouTubeIframeAPIReady = function () {
  clearTimeout(apiTimer);
  send('api', 'ok', '');
  var vars = {autoplay: 1, playsinline: 1, rel: 0, fs: 1, controls: 1, enablejsapi: 1,
              origin: location.origin, widget_referrer: location.origin};
  if (start) vars.start = start;
  if (list) { vars.listType = 'playlist'; vars.list = list; $('prev').hidden = false; $('next').hidden = false; }
  var options = {width: '100%', height: '100%', playerVars: vars,
    events: {onReady: onReady, onStateChange: onState, onError: onError,
             onPlaybackQualityChange: function (e) { send('quality', e.data, ''); }}};
  if (vid) options.videoId = vid;
  player = new YT.Player('player', options);
};

function onReady() {
  send('ready', Math.round((Date.now() - loadedAt) / 100) / 10, 'seconds after page load');
  status('Ready');
  try { player.playVideo(); } catch (e) {}
  setTimeout(function () {
    if (!everPlayed && lastState !== 3) {
      send('autoplay', 'blocked', 'state ' + (states[lastState] || lastState) + ' 6 s after ready');
      status('Press Play to start');
      $('play').focus();
    }
  }, 6000);
  $('play').focus();
}
function onState(e) {
  lastState = e.data;
  send('state', e.data, states[e.data] || '');
  $('play').innerHTML = e.data === 1 || e.data === 3 ? '&#10074;&#10074; Pause' : '&#9654; Play';
  if (e.data === 1) {
    message('');
    status('Playing');
    if (!everPlayed) {
      everPlayed = true;
      send('playing', Math.round((Date.now() - loadedAt) / 100) / 10, 'seconds after page load');
    }
  } else if (e.data === 2) status('Paused');
  else if (e.data === 3) status('Buffering...');
  else if (e.data === 0) { status('Finished'); send('ended', '1', ''); }
}
function onError(e) {
  var text = reasons[e.data] || ('YouTube player error ' + e.data + '.');
  if (sentErrors++ < 10) send('error', e.data, text);
  message(text + ' (error ' + e.data + ')');
  status('Error ' + e.data);
}

$('back').onclick = backToApp;
$('play').onclick = function () {
  if (!player || !player.getPlayerState) return;
  var s = player.getPlayerState();
  if (s === 1 || s === 3) player.pauseVideo(); else player.playVideo();
};
$('prev').onclick = function () { if (player && player.previousVideo) { player.previousVideo(); send('playlist', 'previous', ''); } };
$('next').onclick = function () { if (player && player.nextVideo) { player.nextVideo(); send('playlist', 'next', ''); } };
$('mute').onclick = function () {
  if (!player || !player.isMuted) return;
  if (player.isMuted()) { player.unMute(); $('mute').textContent = 'Mute'; send('volume', 'unmuted', ''); }
  else { player.mute(); $('mute').textContent = 'Sound on'; send('volume', 'muted', ''); }
};
$('fs').onclick = function () {
  var el = $('stage'), f = el.requestFullscreen || el.webkitRequestFullscreen;
  if (!f) { send('fullscreen', 'unsupported', 'no Fullscreen API'); status('Full screen is not available'); return; }
  try {
    var r = f.call(el);
    if (r && r.then) r.then(function () { send('fullscreen', 'ok', ''); },
                            function (err) { send('fullscreen', 'refused', String(err)); });
    else send('fullscreen', 'requested', '');
  } catch (err) { send('fullscreen', 'refused', String(err)); }
};

// D-pad: move between the buttons; the system cursor works as well.
var keysLogged = 0;
document.addEventListener('keydown', function (e) {
  if (keysLogged++ < 25) send('input', e.key || '', 'keyCode ' + e.keyCode);
  var buttons = Array.prototype.filter.call(document.querySelectorAll('#bar .btn'), function (b) { return !b.hidden; });
  var i = buttons.indexOf(document.activeElement);
  if (e.key === 'ArrowRight' || e.keyCode === 39) { buttons[(i + 1) % buttons.length].focus(); e.preventDefault(); }
  else if (e.key === 'ArrowLeft' || e.keyCode === 37) { buttons[(i + buttons.length - 1) % buttons.length].focus(); e.preventDefault(); }
}, true);

setInterval(function () { try { var x = new XMLHttpRequest(); x.open('GET', 'ping', true); x.send(null); } catch (e) {} }, 3000);
setInterval(function () {
  if (player && player.getCurrentTime && lastState === 1)
    send('time', Math.round(player.getCurrentTime()), 'seconds into the video');
}, 30000);
})();
)AKENO";
}

std::string_view capability_page_html()
{
    return R"AKENO(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AKENO STREAM - Playback lab</title>
<link rel="stylesheet" href="akeno.css">
</head><body>
<main>
<div id="top"><button id="back" class="btn primary" type="button">&#9664; Back to AKENO</button>
<button id="fs" class="btn" type="button">Test full screen</button>
<span id="progress">Testing...</span></div>
<h1>Playback lab</h1>
<p class="lead">What the PS5 browser inside AKENO STREAM can play: formats, streaming
(MediaSource, HLS), encrypted video (Clear Key), video inside other sites' frames, DRM,
storage and sound. Every test uses
AKENO STREAM's own short clip on this console. The results are saved in AKENO STREAM
(Websites &rarr; Playback Lab, and Settings &rarr; Diagnostics &rarr; Browser). Nothing else is
sent anywhere.</p>
<div id="lab"></div>
<p id="keys">Press buttons on the controller: the keys the page receives are recorded too.</p>
<table><tbody id="results"></tbody></table>
</main>
<script src="captest.js"></script>
</body></html>
)AKENO";
}

std::string_view capability_page_js()
{
    return R"AKENO((function () {
'use strict';
var results = [], $ = function (id) { return document.getElementById(id); };
var shownGroup = '';
function add(id, group, name, status, detail) {
  var r = {id: id, group: group, name: name, status: status, detail: String(detail == null ? '' : detail).slice(0, 280)};
  results.push(r);
  var body = $('results');
  if (group !== shownGroup) {
    shownGroup = group;
    var h = document.createElement('tr'), th = document.createElement('th');
    th.colSpan = 3; th.textContent = group; h.appendChild(th); body.appendChild(h);
  }
  var tr = document.createElement('tr');
  var a = document.createElement('td'), b = document.createElement('td'), c = document.createElement('td');
  a.textContent = name; b.textContent = status; b.className = 's ' + status; c.textContent = r.detail;
  tr.appendChild(a); tr.appendChild(b); tr.appendChild(c); body.appendChild(tr);
  return r;
}
function post(path, body) {
  try { var x = new XMLHttpRequest(); x.open('POST', path, true); x.setRequestHeader('Content-Type', 'application/json'); x.send(body); } catch (e) {}
}
// Results go to AKENO STREAM after every step, so leaving early keeps them.
function report(list, done) { post('report', JSON.stringify({version: 2, done: !!done, results: list})); }
function step(text) { $('progress').textContent = text; }
function wait(ms) { return new Promise(function (r) { setTimeout(r, ms); }); }
function timeout(p, ms) { return Promise.race([p, wait(ms).then(function () { throw new Error('timed out after ' + ms + ' ms'); })]); }
var mediaErrors = {1: 'aborted', 2: 'network error', 3: 'decode error', 4: 'format not supported'};
function mediaError(v) { return v.error ? (mediaErrors[v.error.code] || v.error.code) + (v.error.message ? ' (' + v.error.message + ')' : '') : 'unknown'; }
function makeVideo() {
  var v = document.createElement('video');
  v.muted = true; v.playsInline = true; v.setAttribute('playsinline', ''); v.preload = 'auto';
  $('lab').appendChild(v);
  return v;
}
function dispose(v) { try { v.pause(); v.removeAttribute('src'); v.load(); } catch (e) {} if (v.parentNode) v.parentNode.removeChild(v); }
function getBytes(path) {
  return new Promise(function (resolve, reject) {
    var x = new XMLHttpRequest();
    x.open('GET', path, true); x.responseType = 'arraybuffer';
    x.onload = function () { x.status === 200 && x.response ? resolve(x.response) : reject(new Error('HTTP ' + x.status)); };
    x.onerror = function () { reject(new Error('network error')); };
    x.send();
  });
}
// Plays v until 1 s of media has passed; play() refusals are told apart from failures.
function watch(v, name, onDone) {
  var done = false, stage = 'load';
  function finish(status, detail) { if (done) return; done = true; onDone(status, detail); }
  v.addEventListener('timeupdate', function () { if (v.currentTime > 1.0) finish('yes', 'reached ' + v.currentTime.toFixed(1) + ' s'); });
  v.addEventListener('error', function () { finish('no', 'media error during ' + stage + ': ' + mediaError(v)); });
  setTimeout(function () { finish('no', 'no progress after 12 s (stage ' + stage + ', readyState ' + v.readyState + ')'); }, 12000);
  return {
    stage: function (s) { stage = s; },
    play: function () {
      stage = 'play';
      var p; try { p = v.play(); } catch (e) { finish('no', 'play(): ' + e.name); return; }
      if (p && p.then) p.then(null, function (e) {
        if (e && e.name === 'NotAllowedError')
          finish(v.readyState >= 2 ? 'partial' : 'unknown', 'loaded (readyState ' + v.readyState + ') but the browser refused to start without a click');
        else finish('no', 'play(): ' + (e && e.name));
      });
    },
    fail: finish
  };
}
$('back').onclick = function () { post('close', ''); step('Returning to AKENO STREAM...'); };
$('back').focus();

var keys = 0;
document.addEventListener('keydown', function (e) {
  $('keys').textContent = 'Last key: "' + (e.key || '') + '" (keyCode ' + e.keyCode + ')';
  if (keys++ < 25) post('event', JSON.stringify({type: 'input', value: e.key || '', detail: 'keyCode ' + e.keyCode}));
}, true);

function browserFacts() {
  var g = 'Browser';
  add('browser.ua', g, 'User agent', 'info', navigator.userAgent);
  add('browser.secure', g, 'Secure context (needed for DRM)', window.isSecureContext ? 'yes' : 'no',
      window.isSecureContext ? 'this page counts as secure' : 'DRM results below cannot be trusted from this page - use the secure DRM check');
  add('browser.screen', g, 'Screen and window', 'info', screen.width + 'x' + screen.height + ', window ' +
      innerWidth + 'x' + innerHeight + ', pixel ratio ' + (window.devicePixelRatio || 1));
  add('browser.cookies', g, 'Cookies enabled', navigator.cookieEnabled ? 'yes' : 'no', '');
  add('browser.wasm', g, 'WebAssembly', typeof WebAssembly === 'object' ? 'yes' : 'no', '');
  var gl = null; try { gl = document.createElement('canvas').getContext('webgl'); } catch (e) {}
  add('browser.webgl', g, 'WebGL', gl ? 'yes' : 'no', gl ? String(gl.getParameter(gl.VERSION)) : '');
  add('browser.fullscreen', g, 'Fullscreen API', (document.fullscreenEnabled || document.webkitFullscreenEnabled) ? 'yes' : 'no',
      document.fullscreenEnabled ? 'standard' : document.webkitFullscreenEnabled ? 'webkit prefix' : 'press "Test full screen" to try it on a video');
  var pads = navigator.getGamepads ? Array.prototype.filter.call(navigator.getGamepads(), function (p) { return !!p; }).length : -1;
  add('browser.gamepad', g, 'Gamepad API', pads < 0 ? 'no' : 'info', pads < 0 ? '' : pads + ' controller(s) visible to pages');
  add('browser.sw', g, 'Service workers', 'serviceWorker' in navigator ? 'yes' : 'no', '');
  add('browser.mediasession', g, 'Media Session API', 'mediaSession' in navigator ? 'yes' : 'no', '');
  var w = 'Web platform (what web players use)';
  add('js.worker', w, 'Web Workers', typeof Worker === 'function' ? 'yes' : 'no', 'HLS.js and DASH players move work into workers');
  add('js.fetch', w, 'fetch()', typeof fetch === 'function' ? 'yes' : 'no', '');
  add('js.blob', w, 'Blob URLs', window.URL && URL.createObjectURL ? 'yes' : 'no', 'MediaSource players attach through them');
  add('js.promise', w, 'Promises', typeof Promise === 'function' ? 'yes' : 'no', '');
  add('browser.storage_access', w, 'Storage Access API', document.hasStorageAccess ? 'yes' : 'no', 'players in frames ask for cookies through it');
}

function storage() {
  var g = 'Storage and sessions', now = new Date().toISOString();
  try {
    var prev = localStorage.getItem('akeno.captest.last');
    localStorage.setItem('akeno.captest.last', now);
    add('storage.local', g, 'Local storage', 'yes', '');
    add('storage.local_kept', g, 'Local storage kept since the last test', prev ? 'yes' : 'unknown',
        prev ? 'last test: ' + prev : 'first test, or it was not kept - run the test again after closing the browser and after restarting the app');
  } catch (e) { add('storage.local', g, 'Local storage', 'no', String(e)); }
  var m = document.cookie.match(/(?:^|; )akeno_captest=([^;]*)/);
  document.cookie = 'akeno_captest=' + encodeURIComponent(now) + '; Max-Age=31536000; Path=/; SameSite=Strict';
  add('storage.cookie_kept', g, 'Cookies kept since the last test', m ? 'yes' : 'unknown',
      m ? 'last test: ' + decodeURIComponent(m[1]) : 'first test, or it was not kept - run the test again after closing the browser and after restarting the app');
  try { sessionStorage.setItem('akeno', '1'); add('storage.session', g, 'Session storage', 'yes', ''); }
  catch (e) { add('storage.session', g, 'Session storage', 'no', String(e)); }
  return new Promise(function (resolve) {
    if (!window.indexedDB) { add('storage.indexeddb', g, 'IndexedDB', 'no', ''); resolve(); return; }
    try {
      var r = indexedDB.open('akeno-captest', 1);
      r.onsuccess = function () { add('storage.indexeddb', g, 'IndexedDB', 'yes', ''); try { r.result.close(); } catch (e) {} resolve(); };
      r.onerror = function () { add('storage.indexeddb', g, 'IndexedDB', 'no', String(r.error)); resolve(); };
      setTimeout(resolve, 3000);
    } catch (e) { add('storage.indexeddb', g, 'IndexedDB', 'no', String(e)); resolve(); }
  });
}

function codecs() {
  var v = document.createElement('video'), a = document.createElement('audio');
  var video = [['codec.h264_base', 'H.264 Baseline', 'video/mp4; codecs="avc1.42E01E"'],
               ['codec.h264', 'H.264 High', 'video/mp4; codecs="avc1.640028"'],
               ['codec.hevc', 'HEVC (H.265)', 'video/mp4; codecs="hvc1.1.6.L120.90"'],
               ['codec.vp9', 'VP9 (WebM)', 'video/webm; codecs="vp9"'],
               ['codec.vp9_mp4', 'VP9 (MP4)', 'video/mp4; codecs="vp09.00.10.08"'],
               ['codec.av1', 'AV1', 'video/mp4; codecs="av01.0.05M.08"'],
               ['codec.hls', 'HLS played natively', 'application/vnd.apple.mpegurl'],
               ['codec.dash', 'MPEG-DASH played natively', 'application/dash+xml']];
  var audio = [['codec.aac', 'AAC', 'audio/mp4; codecs="mp4a.40.2"'],
               ['codec.mp3', 'MP3', 'audio/mpeg'],
               ['codec.opus', 'Opus', 'audio/webm; codecs="opus"'],
               ['codec.ac3', 'Dolby AC-3', 'audio/mp4; codecs="ac-3"'],
               ['codec.eac3', 'Dolby E-AC-3', 'audio/mp4; codecs="ec-3"'],
               ['codec.flac', 'FLAC', 'audio/flac']];
  function verdict(answer) { return answer === 'probably' ? 'yes' : answer === 'maybe' ? 'partial' : 'no'; }
  video.forEach(function (c) { var r = v.canPlayType(c[2]); add(c[0], 'Video formats (canPlayType)', c[1], verdict(r), r || 'no answer'); });
  audio.forEach(function (c) { var r = a.canPlayType(c[2]); add(c[0], 'Audio formats (canPlayType)', c[1], verdict(r), r || 'no answer'); });
  var g = 'Streaming (Media Source Extensions)';
  var MS = window.MediaSource || window.WebKitMediaSource, MMS = window.ManagedMediaSource;
  add('mse.available', g, 'MediaSource', MS ? 'yes' : (MMS ? 'partial' : 'no'),
      MS ? '' : MMS ? 'only ManagedMediaSource' : 'adaptive web players (YouTube, most streaming sites) need it');
  add('mse.managed', g, 'ManagedMediaSource', MMS ? 'yes' : 'no', '');
  var src = MS || MMS;
  if (src && src.isTypeSupported) {
    [['mse.h264', 'H.264 + AAC', 'video/mp4; codecs="avc1.640028,mp4a.40.2"'],
     ['mse.clip', 'The lab clip (H.264 High 3.0 + AAC)', 'video/mp4; codecs="avc1.64001e,mp4a.40.2"'],
     ['mse.ts', 'MPEG-TS segments directly', 'video/mp2t; codecs="avc1.64001e,mp4a.40.2"'],
     ['mse.vp9', 'VP9', 'video/webm; codecs="vp9"'],
     ['mse.av1', 'AV1', 'video/mp4; codecs="av01.0.05M.08"'],
     ['mse.hevc', 'HEVC', 'video/mp4; codecs="hvc1.1.6.L120.90"']].forEach(function (c) {
      var ok = false; try { ok = src.isTypeSupported(c[2]); } catch (e) {}
      add(c[0], g, c[1], ok ? 'yes' : 'no', c[0] === 'mse.ts' && !ok ? 'normal: HLS.js converts TS to MP4 itself' : '');
    });
  }
}

function drm() {
  var g = 'DRM (Encrypted Media Extensions)';
  var secure = window.isSecureContext;
  if (window.WebKitMediaKeys && window.WebKitMediaKeys.isTypeSupported) {
    var fps = false; try { fps = window.WebKitMediaKeys.isTypeSupported('com.apple.fps.1_0', 'video/mp4'); } catch (e) {}
    add('drm.legacy_fps', g, 'Legacy WebKitMediaKeys (FairPlay)', fps ? 'yes' : 'no', 'the older Safari interface');
  } else add('drm.legacy_fps', g, 'Legacy WebKitMediaKeys (FairPlay)', 'no', 'not present');
  add('drm.legacy_ms', g, 'Legacy MSMediaKeys (PlayReady)', window.MSMediaKeys ? 'yes' : 'no', '');
  if (!navigator.requestMediaKeySystemAccess) {
    add('drm.eme', g, 'EME (requestMediaKeySystemAccess)', secure ? 'no' : 'unknown',
        secure ? 'the browser offers no DRM to web pages' : 'not available to this page (not a secure context) - use the secure DRM check');
    return Promise.resolve();
  }
  add('drm.eme', g, 'EME (requestMediaKeySystemAccess)', 'yes', 'present; key systems below');
  var systems = [['drm.widevine', 'Widevine', 'com.widevine.alpha'],
                 ['drm.playready', 'PlayReady', 'com.microsoft.playready.recommendation'],
                 ['drm.playready_legacy', 'PlayReady (legacy name)', 'com.microsoft.playready'],
                 ['drm.fairplay', 'FairPlay', 'com.apple.fps'],
                 ['drm.fairplay_1', 'FairPlay 1.0', 'com.apple.fps.1_0'],
                 ['drm.clearkey', 'Clear Key (no protection, test only)', 'org.w3.clearkey']];
  // Several configurations: the browser takes the first one it supports.
  var configs = [{initDataTypes: ['cenc', 'keyids'],
                  videoCapabilities: [{contentType: 'video/mp4; codecs="avc1.42E01E"'}],
                  audioCapabilities: [{contentType: 'audio/mp4; codecs="mp4a.40.2"'}]},
                 {initDataTypes: ['sinf', 'skd'],
                  videoCapabilities: [{contentType: 'video/mp4'}]},
                 {videoCapabilities: [{contentType: 'video/mp4'}]}];
  return systems.reduce(function (chain, s) {
    return chain.then(function () {
      step('Testing DRM: ' + s[1] + '...');
      return timeout(navigator.requestMediaKeySystemAccess(s[2], configs), 5000).then(function (access) {
        var c = access.getConfiguration ? access.getConfiguration() : {};
        var robust = c.videoCapabilities && c.videoCapabilities[0] && c.videoCapabilities[0].robustness;
        var detail = 'key system available' + (robust ? ', robustness ' + robust : '') +
                     (c.initDataTypes ? ', init data ' + c.initDataTypes.join('/') : '');
        if (!access.createMediaKeys) { add(s[0], g, s[1], 'yes', detail); return; }
        return timeout(access.createMediaKeys(), 5000).then(function () {
          add(s[0], g, s[1], 'yes', detail + ', keys created');
        }, function (e) {
          add(s[0], g, s[1], 'partial', detail + ', but creating keys failed: ' + (e && e.name));
        });
      }, function (e) {
        add(s[0], g, s[1], secure ? 'no' : 'unknown', (e && (e.name + ': ' + e.message)) || 'refused');
      });
    });
  }, Promise.resolve());
}

function webAudio() {
  var g = 'Audio';
  var AC = window.AudioContext || window.webkitAudioContext;
  if (!AC) { add('audio.webaudio', g, 'Web Audio', 'no', ''); return Promise.resolve(); }
  try {
    var ctx = new AC();
    return timeout(ctx.resume ? ctx.resume() : Promise.resolve(), 3000).then(function () {
      add('audio.webaudio', g, 'Web Audio', ctx.state === 'running' ? 'yes' : 'partial', 'state ' + ctx.state + ', ' + ctx.sampleRate + ' Hz');
      try { ctx.close(); } catch (e) {}
    }, function (e) { add('audio.webaudio', g, 'Web Audio', 'partial', 'state ' + ctx.state + ': ' + e.message); });
  } catch (e) { add('audio.webaudio', g, 'Web Audio', 'no', String(e)); return Promise.resolve(); }
}

var PG = 'Playback (AKENO STREAM\'s own clip)';
// 1. A plain MP4 file, first with sound: shows the autoplay rules too.
function progressive() {
  step('Playing an MP4 file...');
  var v = makeVideo();
  v.muted = false;
  return new Promise(function (resolve) {
    var w = watch(v, 'mp4', function (status, how) {
      var frames = v.getVideoPlaybackQuality ? v.getVideoPlaybackQuality().totalVideoFrames : (v.webkitDecodedFrameCount || 0);
      var audioBytes = v.webkitAudioDecodedByteCount;
      add('playback.video', PG, 'MP4 file plays', status, how + (frames ? ', ' + frames + ' frames' : ''));
      add('playback.audio', PG, 'Audio decoded', audioBytes === undefined ? 'unknown' : (audioBytes > 0 ? 'yes' : 'no'),
          audioBytes === undefined ? 'this browser does not report it - listen for the tone' : audioBytes + ' bytes of audio decoded');
      dispose(v);
      resolve();
    });
    v.src = 'test.mp4';
    var p; try { p = v.play(); } catch (e) { w.fail('no', 'play(): ' + e.name); return; }
    if (p && p.then) {
      p.then(function () { add('playback.autoplay_sound', PG, 'Starts with sound without a click', 'yes', ''); },
             function (e) {
               add('playback.autoplay_sound', PG, 'Starts with sound without a click', 'no', (e && e.name) + ' - sites will ask for a click first');
               v.muted = true;
               v.play().then(function () { add('playback.autoplay_muted', PG, 'Starts muted without a click', 'yes', ''); },
                             function (e2) { add('playback.autoplay_muted', PG, 'Starts muted without a click', 'no', e2 && e2.name);
                                             w.fail('unknown', 'the browser refused to start the video without a click'); });
             });
    }
  });
}

var MSE_TYPE = 'video/mp4; codecs="avc1.64001e,mp4a.40.2"';
function mediaSource() { return window.MediaSource || window.WebKitMediaSource || window.ManagedMediaSource; }
// Gives v a fragmented MP4 through MediaSource and starts it, as HLS.js,
// DASH and Shaka players do.
function feedMse(v, w, MS, path) {
  var ms;
  try { ms = new MS(); } catch (e) { w.fail('no', 'new MediaSource: ' + e.name); return; }
  if (MS === window.ManagedMediaSource) v.disableRemotePlayback = true;
  w.stage('sourceopen');
  ms.addEventListener('sourceopen', function () {
    var sb;
    w.stage('addSourceBuffer');
    try { sb = ms.addSourceBuffer(MSE_TYPE); } catch (e) { w.fail('no', 'addSourceBuffer: ' + e.name); return; }
    w.stage('download');
    getBytes(path).then(function (buffer) {
      w.stage('appendBuffer');
      sb.addEventListener('error', function () { w.fail('no', 'SourceBuffer error while appending'); });
      sb.addEventListener('updateend', function () {
        try { if (ms.readyState === 'open') ms.endOfStream(); } catch (e) {}
        w.play();
      });
      try { sb.appendBuffer(buffer); } catch (e) { w.fail('no', 'appendBuffer: ' + e.name); }
    }, function (e) { w.fail('no', 'download: ' + e.message); });
  });
  v.src = URL.createObjectURL(ms);
}

// 2. MediaSource with a fragmented MP4: what HLS.js, DASH and Shaka players do.
function msePlayback() {
  var name = 'MediaSource plays fragmented MP4 (like HLS.js / DASH players)';
  var MS = mediaSource();
  if (!MS) { add('playback.mse', PG, name, 'no', 'no MediaSource in this browser'); return Promise.resolve(); }
  step('Playing through MediaSource...');
  return new Promise(function (resolve) {
    var v = makeVideo();
    var w = watch(v, 'mse', function (status, detail) { add('playback.mse', PG, name, status, detail); dispose(v); resolve(); });
    feedMse(v, w, MS, 'test-frag.mp4');
  });
}

// 3. The same clip encrypted (ISO 'cenc') and played through EME with Clear
// Key, the W3C test key system: its key is published right here and protects
// nothing. It shows whether this browser can decrypt and play encrypted video
// at all - what DRM-protected sites need besides their own key system.
var CK_KID = 'ASNFZ4mrze8BI0VniavN7w', CK_KEY = '_ty6mHZUMhD-3LqYdlQyEA';
function ascii(text) {
  var bytes = new Uint8Array(text.length);
  for (var i = 0; i < text.length; ++i) bytes[i] = text.charCodeAt(i) & 0x7f;
  return bytes;
}
function clearKeyPlayback() {
  var name = 'Encrypted video plays (Clear Key through EME)';
  var MS = mediaSource();
  if (!navigator.requestMediaKeySystemAccess) {
    add('playback.clearkey', PG, name, 'unknown', 'no EME in this page - the decryption pipeline was not tested');
    return Promise.resolve();
  }
  if (!MS) { add('playback.clearkey', PG, name, 'unknown', 'no MediaSource to give the encrypted clip to'); return Promise.resolve(); }
  step('Playing an encrypted clip (Clear Key)...');
  var config = [{initDataTypes: ['keyids', 'cenc'],
                 videoCapabilities: [{contentType: 'video/mp4; codecs="avc1.64001e"'}],
                 audioCapabilities: [{contentType: 'audio/mp4; codecs="mp4a.40.2"'}]}];
  return timeout(navigator.requestMediaKeySystemAccess('org.w3.clearkey', config), 5000)
    .then(function (access) { return timeout(access.createMediaKeys(), 5000); })
    .then(function (keys) {
      return new Promise(function (resolve) {
        var v = makeVideo(), licensed = false;
        var w = watch(v, 'clearkey', function (status, detail) {
          add('playback.clearkey', PG, name, status, detail + (status === 'yes' ? ', decrypted' : licensed ? ' (key delivered)' : ''));
          dispose(v); resolve();
        });
        w.stage('setMediaKeys');
        v.setMediaKeys(keys).then(function () {
          var session = keys.createSession('temporary');
          session.addEventListener('message', function () {
            w.stage('license');
            var license = JSON.stringify({keys: [{kty: 'oct', kid: CK_KID, k: CK_KEY}], type: 'temporary'});
            session.update(ascii(license)).then(function () { licensed = true; },
                                                function (e) { w.fail('no', 'session.update: ' + (e && e.name)); });
          });
          w.stage('generateRequest');
          return session.generateRequest('keyids', ascii(JSON.stringify({kids: [CK_KID]})));
        }).then(function () { feedMse(v, w, MS, 'test-cenc.mp4'); },
                function (e) { w.fail('no', 'EME: ' + (e && e.name) + (e && e.message ? ': ' + e.message : '')); });
      });
    }, function (e) {
      add('playback.clearkey', PG, name, 'unknown',
          'Clear Key is not offered (' + ((e && e.name) || 'refused') + ') - the decryption pipeline was not tested');
    });
}

// 4. An HLS playlist given straight to the video element (Safari-style).
function hlsNative() {
  step('Playing an HLS playlist natively...');
  return new Promise(function (resolve) {
    var v = makeVideo();
    var can = v.canPlayType('application/vnd.apple.mpegurl');
    var w = watch(v, 'hls', function (status, detail) {
      add('playback.hls_native', PG, 'HLS playlist plays natively', status, detail + '; canPlayType "' + (can || '') + '"');
      dispose(v); resolve();
    });
    v.src = 'test.m3u8';
    w.play();
  });
}

// 5. Video inside a frame from another origin, the way most sites embed their
// players; the frame also reports whether it may keep cookies.
function framePlayback() {
  step('Playing inside a frame from another site...');
  var origin = 'http://localhost:' + location.port;
  return new Promise(function (resolve) {
    var f = document.createElement('iframe'), loaded = false, done = false, cookies = false;
    function finish(status, detail) {
      if (done) return; done = true;
      add('playback.iframe', PG, 'Video plays in a frame from another site', status, detail);
      if (!cookies) add('browser.third_party_cookies', 'Storage and sessions', 'Cookies inside frames from other sites', 'unknown', 'the test frame did not report');
      setTimeout(function () { if (f.parentNode) f.parentNode.removeChild(f); }, 100);
      resolve();
    }
    window.addEventListener('message', function (e) {
      if (e.origin !== origin || !e.data || e.data.akeno !== 'frame') return;
      if (e.data.kind === 'loaded') loaded = true;
      else if (e.data.kind === 'cookies') {
        cookies = true;
        add('browser.third_party_cookies', 'Storage and sessions', 'Cookies inside frames from other sites',
            e.data.status, String(e.data.detail || '').slice(0, 200));
      } else if (e.data.kind === 'result') finish(e.data.status, String(e.data.detail || '').slice(0, 200));
    });
    setTimeout(function () {
      finish(loaded ? 'no' : 'unknown', loaded ? 'the frame loaded but did not play within 16 s'
                                                : 'the test frame did not load (this browser may not reach "localhost")');
    }, 16000);
    f.setAttribute('allow', 'autoplay; fullscreen; encrypted-media');
    f.setAttribute('allowfullscreen', '');
    f.width = 480; f.height = 270; f.style.border = '0';
    f.src = origin + location.pathname.replace(/captest$/, 'frame');
    $('lab').appendChild(f);
  });
}

// Full screen needs a button press, so it has its own button.
$('fs').onclick = function () {
  var name = 'A video goes full screen';
  var v = makeVideo(), done = false;
  v.loop = true; v.src = 'test.mp4';
  function finish(status, detail) {
    if (done) return; done = true;
    report([add('playback.fullscreen', PG, name, status, detail)], false);
    setTimeout(function () {
      try { (document.exitFullscreen || document.webkitExitFullscreen || function () {}).call(document); } catch (e) {}
      try { if (v.webkitExitFullscreen) v.webkitExitFullscreen(); } catch (e) {}
      dispose(v); $('fs').focus();
    }, 3000);
  }
  ['fullscreenchange', 'webkitfullscreenchange'].forEach(function (ev) {
    document.addEventListener(ev, function () { if (document.fullscreenElement || document.webkitFullscreenElement) finish('yes', ev); });
  });
  v.addEventListener('webkitbeginfullscreen', function () { finish('yes', 'webkitbeginfullscreen'); });
  var play = v.play(); if (play && play.then) play.then(null, function () {});
  var request = v.requestFullscreen || v.webkitRequestFullscreen || v.webkitEnterFullscreen;
  if (!request) { finish('no', 'the video element has no full-screen function'); return; }
  try {
    var r = request.call(v);
    if (r && r.then) r.then(null, function (e) { finish('no', 'refused: ' + (e && e.name)); });
  } catch (e) { finish('no', 'refused: ' + e.name); }
  setTimeout(function () { finish('no', 'no full-screen change within 5 s'); }, 5000);
};

browserFacts();
storage()
  .then(function () { codecs(); report(results, false); return drm(); })
  .then(webAudio)
  .then(function () { report(results, false); return progressive(); })
  .then(function () { report(results, false); return msePlayback(); })
  .then(function () { report(results, false); return clearKeyPlayback(); })
  .then(function () { report(results, false); return hlsNative(); })
  .then(function () { report(results, false); return framePlayback(); })
  .then(function () {
    report(results, true);
    step('Done: ' + results.length + ' results saved in AKENO STREAM. Try "Test full screen", then go back.');
    $('back').focus();
  }, function (e) {
    add('captest.failed', 'Test', 'The test stopped', 'no', String(e));
    report(results, true);
  });
})();
)AKENO";
}

std::string_view frame_page_html()
{
    return R"AKENO(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<title>AKENO STREAM - frame test</title>
<link rel="stylesheet" href="akeno.css">
</head><body style="margin:0;background:#000">
<video id="v" muted playsinline preload="auto" style="width:100%;height:100%;border-radius:0"></video>
<script src="frame.js"></script>
</body></html>
)AKENO";
}

std::string_view frame_page_js()
{
    return R"AKENO((function () {
'use strict';
var parentOrigin = 'http://127.0.0.1:' + location.port;
function send(kind, status, detail) {
  try { parent.postMessage({akeno: 'frame', kind: kind, status: status, detail: String(detail || '').slice(0, 200)}, parentOrigin); } catch (e) {}
}
send('loaded', 'info', '');
var stamp = String(Date.now());
try { document.cookie = 'akeno_frame=' + stamp + '; Path=/'; } catch (e) {}
var kept = document.cookie.indexOf('akeno_frame=' + stamp) >= 0;
send('cookies', kept ? 'yes' : 'no', kept ? 'a frame from another site may set cookies' : 'blocked: players in frames cannot keep sign-ins');
var v = document.getElementById('v'), done = false;
function finish(status, detail) { if (done) return; done = true; send('result', status, detail); try { v.pause(); } catch (e) {} }
v.addEventListener('timeupdate', function () { if (v.currentTime > 1.0) finish('yes', 'reached ' + v.currentTime.toFixed(1) + ' s'); });
v.addEventListener('error', function () { finish('no', 'media error ' + (v.error ? v.error.code : '')); });
v.src = 'test.mp4';
var p; try { p = v.play(); } catch (e) { finish('no', 'play(): ' + e.name); }
if (p && p.then) p.then(null, function (e) {
  finish(e && e.name === 'NotAllowedError' ? 'partial' : 'no', 'play(): ' + (e && e.name) + (e && e.name === 'NotAllowedError' ? ' - frames need a click to start' : ''));
});
setTimeout(function () { finish('no', 'no progress after 12 s (readyState ' + v.readyState + ')'); }, 12000);
})();
)AKENO";
}
} // namespace akeno::web
