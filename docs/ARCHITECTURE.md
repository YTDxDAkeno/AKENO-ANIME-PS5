# AKENO STREAM architecture

## Threads and data flow

```
 main thread (src/main.cpp)          player worker (media/player.cpp)       ProsperoTV backend threads
 ─────────────────────────           ───────────────────────────────        ──────────────────────────
 Pad::poll -> App::handle            HLS: playlist -> variant -> segments   Videodec2 decode
 App::update (jobs, player state)    HTTP TS / local file (FFmpeg remux)    Audiodec / minimp3 / FFmpeg audio
 App::render -> Surface (linear)     -> iptv_stream (TS demux, AUs)  ────>  picture callback (NV12/P010)
 Display::present (detile, flip)                                            -> FrameStore::publish (RGBA)
        ▲                                                                   AudioOut, wall-clock pacing
        └──────────── FrameStore::draw (latest picture) <──────────────────────────────┘
 Jobs (3 workers): HTTP for catalogues, artwork decoding, self-tests; results posted back to the UI thread
```

- The interface is drawn by a portable software rasterizer (`src/gfx`) into
  an ordinary 1920x1080 RGBA buffer. `platform/ps5/display.cpp` copies it into
  the tiled VideoOut scan-out buffer (4x4 micro-tile swizzle), flushes the
  cache and flips on vertical blank. The main loop redraws only when
  something changed (`App::needs_redraw`).
- Text uses FreeType with the bundled Inter fonts, falling back to a built-in
  pixel font if the fonts cannot be read.
- The player (`media::Player`) owns one worker thread per session. It
  resolves HLS once, then downloads segments and pushes them into
  ProsperoTV's `iptv_stream` demuxer, which calls a `DecodeSink`. On the PS5
  that sink is `media/native/native_sink.cpp`, wrapping
  `iptv_native_backend` (hardware decode, audio output, A/V pacing by the
  backend's clock). The host tests use `tests/host/software_sink.cpp`, which
  decodes with FFmpeg instead - everything above the sink is the same code.
- Decoded pictures are converted from NV12/P010 (BT.601/709/2020, limited or
  full range) to RGBA on the decoder thread into a triple buffer
  (`media/frame_store.cpp`); the UI thread draws the newest one.
- Seeking restarts the session at the target position (HLS: the segment that
  contains it; files: FFmpeg seek before remuxing). Pause is applied in the
  backend (`set_paused`).

## The embedded browser (0.7.0)

```
 main thread                                   system (separate component)
 ───────────                                   ───────────────────────────
 BrowserScreen::update -> WebView::update ───> libSceWebBrowserDialog: WebKit page drawn
   (UpdateStatus every frame)                  over AKENO's VideoOut output, its own
 App::render + Display::present every frame    cursor, keyboard, TLS, cookies
        ▲                                                │
        │ events, report, close                          │ AKENO's own pages only
 LocalPages thread (127.0.0.1:8095/s/<token>/) <─────────┘ (YouTube player, capability test)
```

- `platform/web_view.hpp` is the engine interface; `platform/ps5/web_view.cpp`
  drives `libSceWebBrowserDialog` through positional imports
  (`tooling/stubs/`), `tests/host/host_web_view.cpp` is a scripted stand-in.
  Research and evidence: [BROWSER_RESEARCH.md](BROWSER_RESEARCH.md).
- `BrowserScreen` (`app/screen_browser.cpp`) is one visit: optional site check
  (`web/site_probe.cpp`), open, pump, close, then per-capability results in
  `web::WebTestLog`. It is a full-screen overlay, so mode switching is off,
  and it removes itself with `Screen::close_later()` after its update.
- Third-party sites are opened directly; AKENO's own pages come from
  `web/local_pages.cpp` (loopback only, token, Host check, CSP, size limits,
  known events only) with the HTML/JS in `web/pages.cpp`.

## Source layout

| Path | Contents |
| --- | --- |
| `src/main.cpp` | Console entry point and main loop |
| `src/app/` | Application shell, screens, settings/history store, diagnostics, controller mapping |
| `src/ui/` | Theme, painter (widgets, icons), artwork cache, on-screen keyboard |
| `src/gfx/` | Surface, fonts, image decoding (stb_image), NV12 conversion, QR codes |
| `src/media/` | Player, HLS parser, FFmpeg remuxer and probe, frame store |
| `src/media/native/` | PS5 hardware decode sink |
| `src/net/` | libcurl HTTP client (TLS verification on, size and time limits, cancellation) |
| `src/providers/` | Open catalogue, AniList, YouTube Data API, Crunchyroll status |
| `src/web/` | Addresses and search, saved websites, site check, loopback page server and pages, browser test results |
| `tooling/stubs/` | Link-only stubs for the system browser dialog and common-dialog modules |
| `src/core/` | JSON, URLs, files, background jobs |
| `src/platform/` | Platform interfaces; `ps5/` implements them for the console |
| `third_party/` | ProsperoTV backend and demuxer, stb, qrcodegen, minimp3 (unmodified) |
| `tests/unit`, `tests/host` | GoogleTest suites and host stand-ins (HTTP server, software sink, recorded API responses) |
| `tools/` | Build, FFmpeg, fixtures, screenshots, import check |

## Storage

Everything the app writes stays under `/download0/akeno/` (the title's own
download-data area, mounted only while the app runs; a PC can read it over
FTP at `/mnt/sandbox/PPSA99276_000/download0/akeno/` but not write to it):

| File | Contents |
| --- | --- |
| `settings.json` | Versioned settings (`"version": 1`); unknown or corrupt files are moved aside and defaults used |
| `history.json`, `favorites.json` | Watch history with positions, favourites |
| `secrets.json` | YouTube API key only; never logged or exported |
| `websites.json` | Saved websites, Recently Visited, your per-site test marks |
| `web-tests.json` | Browser capability test, YouTube player and Crunchyroll results |
| `akeno-diagnostics-*.txt` | Exported reports |
| `crash.txt`, `crash-previous.txt` | Crash report of the last session; shown and renamed at the next start |

Files a user provides are read from the install folder, which a PC can write
(`/data/homebrew/PPSA99276/`, `/app0` in the app; read-only to the app):
`media/`, `streams.json`, `youtube-key.txt` and `websites.txt`.

Writes go to a temporary file first and are renamed into place.

## Security and policy rules in the code

- TLS certificate and host-name verification are always on; the console's CA
  list is used. HTTPS redirects may not downgrade to HTTP.
- No credentials are built in. The YouTube key is the user's own, stored
  separately and redacted (`AIza…`, `key=`, `token=`, `signature=` …) from
  logs, the diagnostics screen and exported reports.
- Only DRM-free sources are played. Encrypted HLS is refused, not bypassed.
  YouTube and Crunchyroll video is not extracted; the app links to the
  official players with QR codes.
- Responses are size-limited (playlists 4 MB, JSON 8 MB, images 12 MB,
  segments 48 MB) and parsers are depth- and length-limited.
- Websites run in the system's browser engine, a separate component: AKENO
  never sees their content, cookies or input, adds no proxy or script, and
  makes no certificate exceptions. Only http(s) addresses are opened; no
  credentials in addresses; loopback addresses are reserved for AKENO's own
  pages, which are served only while open, with a random token, and can only
  report results and ask to close the browser.
