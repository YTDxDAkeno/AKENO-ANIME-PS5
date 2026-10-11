# Video-only player (official embeds)

AKENO STREAM's video-only player shows one video in its service's **own,
officially supported embedded player** on a page AKENO serves itself: the
picture, AKENO's controller controls and nothing of the service's website
around it. It sits between AKENO's native player and a plain website visit:

| Route | What plays | Used when |
| --- | --- | --- |
| **AKENO player** (native) | AKENO's own FFmpeg/PS5-decoder player | The provider names a DRM-free file or stream (MP4, HLS, TS, MKV), or looks one up (PeerTube `uuid@host`) |
| **Video-only player** | The service's official embed: YouTube IFrame Player API, PeerTube Embed API | The episode names an embed of one of these services, or you choose *Video-only player* for a PeerTube episode |
| **Website** | The provider's own page in the PS5 browser, labelled *its own player, not AKENO's* | Only a website page is known, or the embed refuses to be framed and the website may still play it |
| **Unavailable** | Nothing | DRM, a sign-in, a region restriction, a missing video, no video at all - each said in one sentence |

DRM, sign-ins, region limits and frame restrictions are reported, never worked
around (`src/anime/playback_route.cpp`).

## Using it

- **Anime**: a series from a PeerTube provider (for example
  `video.blender.org`) shows **Video-only player** next to *Play* /
  *Continue*. It opens the same episode in PeerTube's player and shares its
  saved position with AKENO's player: stop in one, continue in the other.
- **Websites -> Playback Lab -> Video-only player (official embeds)**: the
  YouTube test video (Big Buck Bunny, Blender Foundation, CC BY), any
  YouTube or PeerTube address (`https://<instance>/w/<id>`,
  `/videos/watch/<id>`, `/videos/embed/<id>`, YouTube watch / youtu.be /
  embed links), or a shortcut to Anime.
- A website provider whose episode pages carry a schema.org `embedUrl` of
  YouTube or PeerTube plays there too (episodes without a video file).

## Controls

| Controller (as the PS5 browser delivers it) | Action |
| --- | --- |
| Cross on a button / Enter | That button |
| D-pad left/right | Move between *Back*, *-10 s*, *Play/Pause*, *+10 s*, *Full screen* |
| Space, `k`, Media Play/Pause | Play / pause |
| PageUp/PageDown, `j`/`l`, Media Rewind/Fast-forward | -10 s / +10 s |
| Escape / browser Back (Circle, where the browser sends it) | Back to AKENO STREAM |
| Gamepad API (where offered): Square / Triangle / L1 / R1 / L2 / R2 | Play/pause / full screen / -10 s / +10 s / -30 s / +30 s |
| L3 + R3 together (if keys reach AKENO) | Close the browser |

Cross and Circle stay with the browser (pointer and back) on the Gamepad API,
so one press never acts twice; a press that arrives both as a key and as a
Gamepad button within 250 ms counts once. The controls hide 4 s after the
video starts and return on any key.

*Back* saves the position, pauses the embed and asks AKENO to close the
browser; AKENO returns to the screen it came from (the series in Anime).

## States the page reports

`loaded` -> (`embed frame-loaded`, `embed linked` for PeerTube; `api ok` for
YouTube) -> `ready` -> `playing` / `paused` / `buffering` / `ended`, plus
`progress` (`position/duration` every 5 s while playing, on pause, at the end
and on exit), `seek`, `fullscreen` (`ok`, `refused`, `unsupported`),
`autoplay blocked`, `error` (`embed-timeout`, `csp`, `bad-target`, YouTube's
codes 2/5/100/101/150/152/153) and `state exit`. AKENO STREAM turns them into
results under *Settings -> Diagnostics -> Browser -> Video-only player
(PeerTube / YouTube)* and saves the position under the episode's key (at most
every 15 s, and at once on pause, end and exit).

If autoplay is refused, the page says *Press X to start*; if a press on Play
does not start it either (the console's browser may not pass a press across
frames), it says to move the pointer onto the picture and press X.

## Security

- **One frame origin per session.** The page's `Content-Security-Policy`
  (`src/web/local_pages.cpp`) allows exactly the embed's origin in
  `frame-src` (`https://<instance>` for PeerTube; YouTube's two player hosts
  and its script hosts for YouTube), `default-src 'none'`, `connect-src
  'self'`, `frame-ancestors 'none'`. A PeerTube host must be a strictly
  validated DNS name (`web::valid_embed_host`) before it reaches that header.
  A page asked to frame anything else is refused by the browser and says so
  (`error csp`).
- **Sandboxed PeerTube frame**: `allow-scripts allow-same-origin` only - no
  pop-ups, no navigating AKENO's page; `allow="autoplay; fullscreen"`.
- **Frame restrictions are tested, not bypassed.** Before opening a PeerTube
  video AKENO requests its embed page and reads `X-Frame-Options` and
  `Content-Security-Policy: frame-ancestors` the way browsers apply them
  (`web::frame_policy`). A refusal, HTTP 404/410 (removed) or 401/403
  (private, sign-in) ends there with the reason; for a refusal or a private
  video, Square opens the video's own website, labelled as such. A frame the
  browser still refuses shows *The PeerTube player did not answer within
  20 s* with the likely reasons.
- **Messages**: only from the embed's own frame and origin; requests the
  embed sends to AKENO are answered *method not found*. Nothing is read from
  the service beyond the player's state.
- YouTube: `controls=0`, `disablekb=1`, `origin` and `widget_referrer` set to
  the page's origin; AKENO's controls sit **below** the picture, never over
  the player (YouTube's API policies), and collapse while it plays.

## PeerTube Embed API: the wire protocol

AKENO does not bundle `@peertube/embed-api` (AGPL-3.0; it wraps `jschannel`,
LGPL): the page has a small client written from the documentation
(<https://docs.joinpeertube.org/api/embed-player>) and the protocol as
`jschannel` 1.0.2 (what PeerTube ships) puts it on the wire:

- Every message is `JSON.stringify`'d and sent with `postMessage`; method
  names carry the scope: `peertube::<name>`. The embed posts to its parent
  with target origin `*`; AKENO checks `event.source` and `event.origin` and
  posts back to the instance's origin only.
- **Handshake**: the embed sends `{"method":"peertube::__ready","params":
  {"type":"publish-request","publish":[]}}` as soon as its channel exists; the
  parent answers once with `{"type":"publish-reply","publish":[]}`. (Older
  `jschannel`: `"ping"` / `"pong"`; both are accepted.) A second handshake
  makes the embed's channel throw, so AKENO answers only the first, and asks
  itself (a `publish-request`) only if the embed stayed silent for 1.5 s
  after its frame loaded. Messages before the handshake are queued by the
  embed, not lost.
- **Requests** `{"id":N,"method":"peertube::play","params":...}` with N > 0;
  answers `{"id":N,"result":...}` or `{"id":N,"error":"...","message":"..."}`.
  Used: `isReady`, `play`, `pause`, `seek` (seconds).
- **Notifications** (no id): `ready` (`true`) once the video is loaded;
  `playbackStatusUpdate` every 500 ms `{position, volume, duration,
  playbackState}` with `playbackState` `unstarted` / `playing` / `paused` /
  `ended` (and `duration` sometimes a string); `playbackStatusChange`
  (`playing`, `paused`, `ended`); `resolutionUpdate`, `volumeChange`
  (ignored).
- Embed parameters used: `api=1&autoplay=1&controls=0&title=0&warningTitle=0&
  peertubeLink=0` and `start=<seconds>`.

## Tests

- `tests/unit/test_video_only.cpp`: documented addresses only (and refusals:
  playlists, `http:`, look-alike paths, forged hosts for the CSP header),
  PeerTube `uuid@host`, the browsers' frame rules, and the routes (native ->
  embed -> website -> unavailable; DRM/sign-in/region/missing never fall
  through).
- `tests/unit/test_websites.cpp`: the page's policy per session, a whole
  session through the App (frame check, page events, seeking, pause saving the
  position, *Back*, resume at the saved position), a framing refusal with the
  website fallback, a removed and a private video, YouTube error 150.
- `tests/browser/video_only.cjs` (run by `BrowserPage.*`): the real page from
  the real page server in Chromium (Playwright), against an official-style
  mock PeerTube embed at `https://peertube.test` speaking the protocol above
  (publish, ping/pong and silent handshakes), a framing refusal
  (`X-Frame-Options: DENY`), a CSP refusal of another instance, and a stand-in
  for YouTube's IFrame API (playing, seeking, pause, error 150). Checks:
  embed parameters, the sandbox, one handshake, play once ready, keyboard
  and Gamepad seeking (one seek per press), pause/play, controls hiding,
  full screen, the end, *Back*. Opt-in locally:

  ```bash
  npm install --prefix /tmp/pw --no-save playwright@1.56.1 && /tmp/pw/node_modules/.bin/playwright install chromium
  AKENO_BROWSER_TESTS=1 NODE_PATH=/tmp/pw/node_modules make test-unit GTEST_ARGS='--gtest_filter=BrowserPage.*'
  ```

  `AKENO_BROWSER_SHOTS=<dir>` saves pictures of the page. CI runs it on every
  push (Build workflow).

Chromium is not the PS5's WebKit: these tests prove the page logic, the
protocol and the security policy, not the console. The console results come
from the hardware checklist ([HARDWARE_ACCEPTANCE.md](HARDWARE_ACCEPTANCE.md),
section 16).

## Known limits

- Not yet run on a PS5. Whether WebKit on firmware 12.20 lets a press on
  AKENO's Play button start a cross-origin embed, offers the Fullscreen API
  inside the browser dialog, delivers L1/R1 as keys or exposes the Gamepad
  API is what section 16 measures.
- Next/previous episode and subtitles stay in AKENO's own player; the
  video-only player plays one episode and returns.
- PeerTube's P2P, resolution and caption choices are the embed's own
  (`controls=0` hides its menu); live videos and password-protected videos are
  not handled specially.
- YouTube playlists play in the existing YouTube page (YouTube mode), not in
  the video-only player.
