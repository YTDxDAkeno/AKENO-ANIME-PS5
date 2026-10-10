# Websites, the YouTube player and Crunchyroll (0.7.0)

AKENO STREAM 0.7.0 opens websites **inside the app**, in the PS5's own web
browser engine, shown over AKENO STREAM as a system dialog. When you close the
browser you are back where you were. The separate browser app is not started.
For what this engine is and what was verified where, see
[BROWSER_RESEARCH.md](BROWSER_RESEARCH.md).

> **Status: built and host-tested, not yet run on a console.** The engine
> was opened from another native homebrew title on firmware 12.70 with the
> same layouts. Whether it opens on your firmware, and what plays in it, is
> what the [hardware checklist](HARDWARE_ACCEPTANCE.md) (section 10) finds
> out. AKENO records every result separately.

## Websites mode

Press R1 until **Websites** (between YouTube and Discover).

| Row | What it does |
| --- | --- |
| Browse → *Search or Enter Address* (or Triangle anywhere in Websites) | Type an address (`youtube.com`, `https://example.com/page`) or words to search for. Addresses open directly; other text is searched with the engine chosen in Settings (DuckDuckGo by default; Google, Bing or Startpage) |
| Browse → *Add Website* | Type the address, then a name. The site's icon is found automatically |
| Browse → *Browser Test* | Measures what the browser supports and saves each result (see below) |
| Your Websites | Your saved sites. Cross opens; **Square** opens the options |
| Sections | YouTube and Crunchyroll, which have their own sections |
| Recently Visited | Sites you opened, newest first (private sites excluded). Square: open, save, remove, clear |

Options for a saved website (Square): Open, Rename, Change address, Show on
Home / Remove from Home, Private (on: visits stay out of Recently Visited and
the site is hidden from Home), Record what works, Refresh icon, Remove.

There is no list of allowed or blocked sites. Any `http://` or `https://`
address you enter can be opened. `javascript:`, `data:`, `file:` and other
schemes, addresses with a user name or password (`user:pass@`) and the
console's own loopback addresses are refused. Plain `http://` works, with a
warning that it is not encrypted.

### Adding websites from a PC

Put `websites.txt` into the install folder (`/data/homebrew/PPSA99276/`), one
site per line:

```text
# Name = address
Crunchyroll = https://www.crunchyroll.com/
My media server = http://192.168.1.20:8096/web/
youtube.com
```

At the next start each address is added to Your Websites **once**. You can
rename or remove it on the console, and the file will not bring it back.
The app never writes this file. Updating the app does not touch your
websites: they live in the app's data folder (`websites.json`, see below).

### In the browser

The browser has its own controls (the console's browser cursor and scrolling,
its own back/forward/reload and address bar, the system keyboard for text
fields). HTML5 video and audio play if the site's player works in the
console's browser. Close the browser to return to AKENO STREAM.

- **Avoid the PS button while the browser is open.** On another title,
  leaving the browser dialog with the PS button panicked the console.
- If the browser offers no way out, press **L3 and R3 together**. AKENO closes
  it if the system passes those buttons on (unverified).
- With *Settings → Check websites before opening* on (default), AKENO first
  checks that the site answers. If it does not, AKENO says why in plain words
  (name not found, no connection, certificate, server error). Cross opens it
  anyway, since the browser checks for itself.

## YouTube

Videos play in **YouTube's official embedded player** (the
[IFrame Player API](https://developers.google.com/youtube/iframe_api_reference)),
in the console's browser inside AKENO STREAM. Nothing is extracted or
downloaded by AKENO.

- With an API key (browsing as before): a video's details show **Play**; a
  channel shows **Play uploads** (a playlist with Previous/Next).
- Without a key: YouTube mode offers **Play a link** (paste or type any
  YouTube link, `youtu.be` link, Shorts link, playlist link or video ID) and
  **youtube.com** (the full website in the browser, with sign-in on Google's
  own page).
- The player page has large TV buttons: Back to AKENO, Previous, Play/Pause,
  Next, Full screen, Mute. The D-pad (left/right) moves between them if the
  browser sends arrow keys; the browser cursor works too.
- The page reports to AKENO what happened, and each item is saved separately
  (Settings → Diagnostics → Browser). The items are: page loaded, IFrame API
  loaded, player ready, video playing, pause, play after pause, playlist
  next/previous, full screen, mute, kept playing, back to AKENO, and every
  player error with its meaning:

| Error | Meaning |
| --- | --- |
| 2 | Invalid video ID |
| 5 | The browser cannot play this video (HTML5 player error: a codec or feature is missing) |
| 100 | Video removed or private |
| 101, 150 | The owner does not allow playback in other apps and sites. Use youtube.com instead |
| 152 | Cannot be played in an embedded player |
| 153 | YouTube did not accept the player's identification (missing HTTP Referer) |

How it works: AKENO serves the player page from
`http://127.0.0.1:8095/s/<random token>/youtube` while it is open. YouTube
requires embedding pages to identify themselves with an HTTP `Referer`, which
a page opened directly does not have. The player page sets
`origin`/`widget_referrer` to that address and the
`strict-origin-when-cross-origin` referrer policy. Only the origin
(`http://127.0.0.1:8095/`) reaches YouTube, never the token.

The **YouTube App** card and button remain as a secondary, experimental
option (starting another app from a homebrew title is unconfirmed).

## Crunchyroll

Home → *Crunchyroll*, Anime → *Streaming Services → Crunchyroll*, or Websites
→ Sections.

- **Open crunchyroll.com** opens Crunchyroll's own website in the browser.
  Sign in on Crunchyroll's own page. AKENO never shows a login form and never
  sees your password, cookies or session.
- **Run browser test** measures whether the browser offers a DRM system
  (Widevine, PlayReady, FairPlay) to web pages. Crunchyroll's episodes are
  DRM-protected and play only if one is offered. AKENO does not and will not
  work around DRM.
- **Record results** lets you record, item by item, what happened on your
  console: website renders, sign-in, still signed in next time, video player
  starts, episode plays with sound. A page that renders or a successful sign-in
  is **not** counted as playback.
- The section shows a verdict from these measurements. Until the browser test
  has run it says "Not measured yet".

## Browser test

Websites → *Browser Test*, or Settings → Diagnostics → *Browser* → Cross.
AKENO serves a test page to the browser that checks and reports:

| Group | Items |
| --- | --- |
| Browser | User agent, secure context, screen size, cookies enabled, WebAssembly, WebGL, Fullscreen API, Gamepad API, service workers, Media Session |
| Storage and sessions | Local storage, and whether it and cookies were **kept since the last test** (run it again after closing the browser, and after restarting AKENO) |
| Video formats | H.264 Baseline/High, HEVC, VP9 (WebM/MP4), AV1, native HLS |
| Audio formats | AAC, MP3, Opus, AC-3, E-AC-3, FLAC |
| Streaming (MSE) | MediaSource, ManagedMediaSource, H.264/VP9/AV1/HEVC support |
| DRM (EME) | EME present; Widevine, PlayReady, FairPlay, Clear Key key systems |
| Audio | Web Audio |
| HTML5 playback | Plays the bundled H.264 + AAC MP4 with sound: video plays, audio decoded, starts with sound without a click, starts muted |
| Controller | The keys the page receives when you press buttons |

The results are saved in `web-tests.json` and appear in Settings →
Diagnostics → Browser, in the Crunchyroll section and in the exported
diagnostics report.

## Settings

| Setting | Default | Meaning |
| --- | --- | --- |
| Web search engine | DuckDuckGo | For text that is not an address |
| Check websites before opening | On | Network check and icon lookup before the browser opens |
| Player pages without browser controls | Off | Experimental: AKENO's own pages (YouTube player, browser test) fill the screen without the browser's bar. They keep their Back to AKENO button, and AKENO closes the browser if such a page stops answering for 20 seconds |
| Clear recently visited websites | | Empties Recently Visited |

## Data and privacy

| File in `/download0/akeno/` | Contents |
| --- | --- |
| `websites.json` | Your websites (name, address, icon address, pinned/private, your test marks, visit count) and Recently Visited |
| `web-tests.json` | Browser test, YouTube player and Crunchyroll results |

- AKENO never sees what happens inside the browser: no page content,
  passwords, cookies, storage or keystrokes. Sign-ins are kept (or not) by the
  browser engine itself. The browser test reports whether cookies survive.
- The exported diagnostics report lists **host names only** for your websites
  (no paths or query strings) and redacts anything that looks like a key or
  token.
- The site check sends one ordinary request for the start page with AKENO's
  own HTTP client (TLS verified, no cookies), only when you open or add a site.

## Security model

- Websites are untrusted. They run in the system's browser engine, which is a
  separate component from AKENO. They cannot reach AKENO's files, settings or
  console functions.
- AKENO's own pages are served only on the console's loopback interface, only
  while such a page is open, with a fresh random 128-bit token in every path.
  The server checks the `Host` header (DNS rebinding), limits request sizes,
  sends a strict Content Security Policy, and accepts only a fixed set of
  report events. Those are plain values that it records and never executes.
  The only things a page can ask for are to record a result and to close the
  browser. If the server gets hundreds of refused requests, it stops.
- No proxy, no injected scripts, no certificate exceptions. Third-party
  sites go directly to the browser, which validates their certificates.

## Limits

- The browser dialog is modal: AKENO cannot draw over it, and the browser's
  controls are the system's.
- Whether sign-ins persist across browser sessions and app restarts depends
  on the system browser (EVO-PLAYER-PS5 saw `localStorage` emptied at each
  opening; cookies were not measured). The browser test measures both.
- AKENO cannot clear the browser's cookies: the function exists in the system
  library but was never called on hardware, and importing an unverified
  function could stop the app from starting.
- Sites that require a DRM system the browser lacks will load but not play.
  Sites can also refuse the console's browser.
