// AKENO STREAM PS5 - The video-only page in a real browser, against official-style mock embeds.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Run by BrowserPage.* in tests/unit/test_video_only.cpp, which serves the
// page from AKENO STREAM's real loopback page server (its real
// Content-Security-Policy) and afterwards checks what the app received.
//
//   node tests/browser/video_only.cjs <scenario> <watch page address>
//
// Chromium (Playwright) loads the page. Requests to the mock services never
// leave the machine: Playwright answers them.
//   https://peertube.test/videos/embed/<id>   a PeerTube embed that speaks
//       the documented Embed API wire protocol (jschannel framing over
//       postMessage: "__ready" handshake, requests {id, method, params},
//       answers {id, result}, notifications "ready", "playbackStatusUpdate"
//       every 500 ms, "playbackStatusChange") with a simulated clock instead
//       of real video. Written from the protocol, not from PeerTube's code.
//   https://www.youtube.com/iframe_api        a stand-in for the IFrame Player
//       API that creates the player frame and reports states like YouTube's.
// Scenarios: peertube, peertube-legacy, peertube-silent, frame-denied, csp,
// youtube, youtube-error. Exit code 0: every check passed.
'use strict';
const { chromium } = require('playwright');

const [scenario, address] = process.argv.slice(2);
const DURATION = 30;

// ---------------------------------------------------------------------------
// The mock PeerTube embed, configured per scenario.
function peertubeEmbed(cfg) {
  return `<!doctype html><html><head><meta charset="utf-8"><title>PeerTube embed (mock)</title></head>
<body style="margin:0;background:#111;color:#eee;font:20px sans-serif"><p id="clock">0</p>
<script>
(function () {
  var cfg = ${JSON.stringify(cfg)};
  var scope = 'peertube', linked = false, queue = [], api = {}, apiReady = false;
  var log = window.__log = {calls: [], handshakeErrors: 0, handshake: '', params: location.search};
  // Messages to the parent: jschannel posts JSON text to window.parent with origin '*'.
  function raw(m) { window.parent.postMessage(JSON.stringify(m), '*'); }
  function post(m) { if (linked) raw(m); else queue.push(m); }
  function notify(method, params) { post({method: scope + '::' + method, params: params}); }
  function link(type) {
    if (linked) { log.handshakeErrors++; throw new Error('received ready message while in ready state.'); }
    linked = true;
    log.handshake = type;
    if (type === 'publish-request') raw({method: scope + '::__ready', params: {type: 'publish-reply', publish: []}});
    else if (type === 'ping') raw({method: scope + '::__ready', params: 'pong'});
    while (queue.length) raw(queue.shift());
  }
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent) return;
    var m;
    try { m = JSON.parse(e.data); } catch (x) { return; }
    if (!m || typeof m !== 'object') return;
    var parts = typeof m.method === 'string' ? m.method.split('::') : [];
    if (parts.length && parts[0] !== scope) return;
    var name = parts[1];
    if (name === '__ready') {
      var p = m.params;
      link(p === 'ping' || p === 'pong' ? p : p && p.type);
      return;
    }
    if (m.id && name) {
      log.calls.push({method: name, params: m.params});
      if (!api[name]) { post({id: m.id, error: 'method_not_found', message: name}); return; }
      var result = api[name](m.params);
      post(result === undefined ? {id: m.id} : {id: m.id, result: result});
    }
  });

  // The "video": a clock that runs while playing.
  var q = new URLSearchParams(location.search);
  var state = 'unstarted', position = parseInt(q.get('start') || '0', 10) || 0, last = Date.now();
  function tick() {
    var now = Date.now();
    if (state === 'playing') {
      position += (now - last) / 1000;
      if (position >= cfg.duration) { position = cfg.duration; change('ended'); }
    }
    last = now;
    document.getElementById('clock').textContent = position.toFixed(1);
  }
  function change(s) { if (state !== s) { state = s; notify('playbackStatusChange', s); } }
  api.isReady = function () { return apiReady; };
  api.isPlaying = function () { return state === 'playing'; };
  api.play = function () { tick(); if (state === 'ended') position = 0; change('playing'); };
  api.pause = function () { tick(); change('paused'); };
  api.seek = function (t) { tick(); position = Math.max(0, Math.min(cfg.duration, Number(t))); };
  api.getCurrentTime = function () { tick(); return position; };
  api.getVolume = function () { return 1; };
  api.setVolume = function () {};
  window.__mock = {
    jump: function (t) { tick(); position = t; },
    state: function () { return state; }
  };
  // jschannel opens the channel itself, right after it is built.
  if (cfg.handshake !== 'silent')
    setTimeout(function () {
      raw({method: scope + '::__ready', params: cfg.handshake === 'ping' ? 'ping' : {type: 'publish-request', publish: []}});
    }, 0);
  // The player is ready once the video is loaded.
  setTimeout(function () {
    apiReady = true;
    notify('ready', true);
    setInterval(function () {
      tick();
      // duration is a string in the documented example
      notify('playbackStatusUpdate', {position: position, volume: 1, duration: String(cfg.duration), playbackState: state});
    }, 500);
  }, cfg.apiDelay);
})();
</script></body></html>`;
}

// A stand-in for https://www.youtube.com/iframe_api (runs in AKENO's page).
function youtubeApi(cfg) {
  return `(function () {
  var cfg = ${JSON.stringify(cfg)};
  window.YT = {Player: function (id, opts) {
    var self = this, state = -1, t = (opts.playerVars && opts.playerVars.start) || 0, last = Date.now();
    var log = window.__yt = {calls: [], vars: opts.playerVars, videoId: opts.videoId};
    var frame = document.createElement('iframe');
    frame.src = 'https://www.youtube.com/embed/' + opts.videoId + '?enablejsapi=1';
    frame.id = id;
    var old = document.getElementById(id);
    old.parentNode.replaceChild(frame, old);
    function tick() { var now = Date.now(); if (state === 1) t += (now - last) / 1000; last = now; }
    function set(s) { tick(); state = s; opts.events.onStateChange({data: s, target: self}); }
    this.playVideo = function () { log.calls.push('play'); set(1); };
    this.pauseVideo = function () { log.calls.push('pause'); set(2); };
    this.seekTo = function (s) { log.calls.push('seek ' + Math.round(s)); tick(); t = s; };
    this.getCurrentTime = function () { tick(); return t; };
    this.getDuration = function () { return 60; };
    this.getPlayerState = function () { return state; };
    setTimeout(function () {
      if (cfg.error) opts.events.onError({data: cfg.error, target: self});
      else opts.events.onReady({target: self});
    }, 100);
  }};
  setTimeout(function () { window.onYouTubeIframeAPIReady(); }, 0);
})();`;
}

// ---------------------------------------------------------------------------
let failures = 0;
function check(ok, what) {
  console.log((ok ? 'ok   ' : 'FAIL ') + what);
  if (!ok) failures++;
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// AKENO_BROWSER_SHOTS=<directory>: pictures of the page at the main steps.
let page = null;
async function shot(name) {
  const dir = process.env.AKENO_BROWSER_SHOTS;
  if (dir && page) await page.screenshot({ path: dir + '/video-only-' + scenario + '-' + name + '.png' });
}
async function until(fn, ms, what) {
  const end = Date.now() + ms;
  for (;;) {
    let v;
    try { v = await fn(); } catch (e) { v = false; }
    if (v) return v;
    if (Date.now() > end) { check(false, what + ' (timed out)'); return false; }
    await sleep(50);
  }
}

async function main() {
  const browser = await chromium.launch();
  const context = await browser.newContext({ viewport: { width: 1920, height: 1080 } });
  page = await context.newPage();
  const errors = [], requested = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('request', (r) => requested.push(r.url()));
  // The Gamepad API as the console's browser may offer it: one standard pad.
  await page.addInitScript(() => {
    const buttons = Array.from({ length: 17 }, () => ({ pressed: false, value: 0 }));
    window.__pad = { id: 'Wireless Controller (mock)', index: 0, mapping: 'standard', connected: true, buttons, axes: [0, 0, 0, 0] };
    navigator.getGamepads = () => [window.__pad];
  });

  const cfg = {
    duration: DURATION,
    apiDelay: 300,
    handshake: scenario === 'peertube-legacy' ? 'ping' : scenario === 'peertube-silent' ? 'silent' : 'publish',
  };
  await context.route('https://peertube.test/**', (route) =>
    route.fulfill({ status: 200, contentType: 'text/html; charset=utf-8', body: peertubeEmbed(cfg),
                    headers: scenario === 'frame-denied' ? { 'X-Frame-Options': 'DENY' } : {} }));
  await context.route('https://other.test/**', (route) =>
    route.fulfill({ status: 200, contentType: 'text/html', body: peertubeEmbed(cfg) }));
  await context.route('https://www.youtube.com/iframe_api', (route) =>
    route.fulfill({ status: 200, contentType: 'text/javascript',
                    body: youtubeApi({ error: scenario === 'youtube-error' ? 150 : 0 }) }));
  await context.route('https://www.youtube.com/embed/**', (route) =>
    route.fulfill({ status: 200, contentType: 'text/html', body: '<!doctype html><title>YouTube player (mock)</title>' }));

  await page.goto(address);
  const status = () => page.textContent('#status');
  const note = () => page.textContent('#note');
  const embedFrame = () => page.frames().find((f) => f.url().startsWith('https://peertube.test/'));
  const calls = async (method) => {
    const f = embedFrame();
    return f ? (await f.evaluate(() => window.__log.calls)).filter((c) => c.method === method) : [];
  };

  if (scenario.startsWith('peertube')) {
    check(await until(async () => (await status()) === 'Playing', 8000, 'starts playing by itself'),
          'status reads Playing');
    const frame = await page.$('#player iframe');
    const src = await frame.getAttribute('src');
    for (const p of ['api=1', 'autoplay=1', 'controls=0', 'peertubeLink=0', 'title=0', 'start=5'])
      check(src.includes(p), 'embed address has ' + p);
    check(src.startsWith('https://peertube.test/videos/embed/'), 'documented embed path');
    check((await frame.getAttribute('sandbox')) === 'allow-scripts allow-same-origin', 'frame is sandboxed (no pop-ups, no navigation)');
    const log = await embedFrame().evaluate(() => window.__log);
    check(log.handshakeErrors === 0, 'one handshake only');
    check(log.handshake === { 'peertube': 'publish-reply', 'peertube-legacy': 'pong', 'peertube-silent': 'publish-request' }[scenario],
          'handshake ' + log.handshake);
    check((await calls('isReady')).length >= 1, 'asks isReady');
    check((await calls('play')).length === 1, 'play() once the player is ready');
    await until(async () => /^0:0[5-9]$/.test(await page.textContent('#time')), 3000, 'time shows the start position');
    await shot('playing');

    if (scenario === 'peertube') {
      // Seek with the keyboard (R1/L1 arrive as PageDown/PageUp on some browsers).
      await page.keyboard.press('PageDown');
      const seeks = await until(async () => { const s = await calls('seek'); return s.length === 1 && s; }, 2000, 'seek sent');
      check(seeks && seeks[0].params >= 14 && seeks[0].params <= 18, 'seek +10 s from about 5 s: ' + (seeks && seeks[0].params));
      // Square / Space pauses and plays.
      await page.keyboard.press(' ');
      check(await until(async () => (await status()) === 'Paused', 2000, 'pauses'), 'Space pauses');
      check(await embedFrame().evaluate(() => window.__mock.state()) === 'paused', 'the embed paused');
      check(!(await page.isHidden('#panel')), 'controls stay visible while paused');
      await shot('paused');
      await page.keyboard.press(' ');
      check(await until(async () => (await status()) === 'Playing', 2000, 'plays again'), 'Space plays again');
      // The controls leave the picture while it plays, and come back on a key.
      check(await until(() => page.isHidden('#panel'), 6000, 'controls hide'), 'controls hide while playing');
      await shot('video-only');
      await page.keyboard.press('ArrowUp');
      check(!(await page.isHidden('#panel')), 'a key shows the controls');
      // A Gamepad R1 press seeks forward once, however long it is held.
      const before = (await calls('seek')).length;
      await page.evaluate(() => { window.__pad.buttons[5] = { pressed: true, value: 1 }; });
      await sleep(450);
      await page.evaluate(() => { window.__pad.buttons[5] = { pressed: false, value: 0 }; });
      await sleep(200);
      check((await calls('seek')).length === before + 1, 'Gamepad R1: one seek per press');
      // Full screen: the request is made and reported (allowed or refused).
      await page.click('#fs');
      await sleep(300);
      // The end of the video.
      await embedFrame().evaluate((d) => window.__mock.jump(d - 0.6), DURATION);
      check(await until(async () => (await status()) === 'Finished', 3000, 'ends'), 'reaches Finished');
      check((await page.textContent('#time')) === '0:30', 'time shows the end');
      check(await page.evaluate(() => document.activeElement.id) === 'back', 'Back has the focus at the end');
      // Back to AKENO: the position goes first, then the close request.
      const closing = page.waitForRequest((r) => r.url().endsWith('/close') && r.method() === 'POST', { timeout: 3000 });
      await page.keyboard.press('Enter');
      check(!!(await closing.catch(() => null)), 'Back asks AKENO STREAM to close the browser');
    } else {
      await page.click('#back');
      await sleep(300);
    }
  } else if (scenario === 'frame-denied') {
    check(await until(async () => (await note()).includes('did not answer within 3 s'), 8000, 'explains the silent player'),
          'a framing refusal becomes a clear error');
    check(await page.getAttribute('#note', 'class') === 'error', 'shown as an error');
    check((await status()) === 'Error', 'status reads Error');
    check(await page.evaluate(() => document.activeElement.id) === 'back', 'Back has the focus');
    await shot('error');
  } else if (scenario === 'csp') {
    check(await until(async () => (await note()).includes('refused to load'), 5000, 'CSP refusal'),
          'a frame from another instance is refused by AKENO\'s page policy');
    check(!requested.some((u) => u.startsWith('https://other.test/')), 'the other instance was never contacted');
  } else if (scenario === 'youtube') {
    check(await until(async () => (await status()) === 'Playing', 5000, 'plays'), 'status reads Playing');
    const vars = await page.evaluate(() => window.__yt.vars);
    check(vars.controls === 0 && vars.disablekb === 1 && vars.start === 5 && vars.playsinline === 1,
          'player variables: no YouTube controls or keys, start 5, inline');
    check(vars.origin === new URL(address).origin, 'origin is the page\'s own');
    check(await until(() => page.isVisible('#player'), 1000, 'frame'), 'YouTube player frame is shown');
    // The controls stay below the picture, never over it (YouTube's policies).
    const stage = await page.$eval('#stage', (e) => e.getBoundingClientRect().bottom);
    const panel = await page.$eval('#panel', (e) => e.getBoundingClientRect().top);
    check(panel >= stage - 1, 'controls do not cover the player');
    await page.keyboard.press('l');
    await until(async () => (await page.evaluate(() => window.__yt.calls)).some((c) => c.startsWith('seek ')), 2000, 'seek');
    await page.keyboard.press('k');
    check(await until(async () => (await status()) === 'Paused', 2000, 'pause'), 'k pauses');
    const closing = page.waitForRequest((r) => r.url().endsWith('/close'), { timeout: 3000 });
    await page.keyboard.press('Escape');
    check(!!(await closing.catch(() => null)), 'Escape (circle) returns to AKENO STREAM');
  } else if (scenario === 'youtube-error') {
    check(await until(async () => (await note()).includes('does not allow'), 5000, 'error 150'),
          'YouTube error 150 is explained');
    check((await note()).includes('(error 150)'), 'the code is shown');
  } else {
    check(false, 'unknown scenario ' + scenario);
  }
  await sleep(300); // let the last events reach the page server
  check(errors.length === 0, 'no script errors' + (errors.length ? ': ' + errors.join(' | ') : ''));
  await browser.close();
}

main().then(
  () => process.exit(failures ? 1 : 0),
  (e) => { console.log('FAIL ' + (e && e.stack || e)); process.exit(1); });
