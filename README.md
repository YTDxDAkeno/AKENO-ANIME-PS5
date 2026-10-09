# AKENO STREAM for PS5

[![Build](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/actions/workflows/tooling.yml/badge.svg)](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/actions/workflows/tooling.yml)
[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg)](LICENSE)

AKENO STREAM is a native, controller-operated media app for jailbroken PS5
consoles. It plays DRM-free video with audio - HLS streams, MPEG-TS over HTTP
and your own MP4, MKV, MOV and TS files - through the console's hardware video
decoder, and adds an anime discovery mode, a YouTube browser, a local library,
watch history, favourites and a diagnostics screen.

| | |
| --- | --- |
| Title ID | `PPSA99276` (unchanged since v0.1) |
| Version | 0.4.2 (`contentVersion` 01.004.002) |
| Tested on | PS5 firmware 13.09 with ShadowMountPlus (0.4.1); no PSN account, no PC needed after install |
| Install path | `/data/homebrew/PPSA99276/` |
| Licence | GPL-3.0-or-later |

> **Status.** 0.4.1 runs on a PS5 (firmware 13.09, ShadowMountPlus): the
> interface, controller, HTTPS, the FFmpeg self-test, hardware H.264 decoding
> with AAC audio (360 of 360 frames presented, nothing dropped, no audio
> errors), an HLS stream (Big Buck Bunny), the AniList catalogue and the
> diagnostics export all worked on the console. Earlier, 0.4.0 crashed at
> launch because the system heap returns null for real allocations; 0.4.1
> brought its own heap. Features not yet tried on the console are marked
> below. Please keep reporting with the
> [hardware acceptance checklist](docs/HARDWARE_ACCEPTANCE.md).

German installation notes: [AKENO_INSTALLIEREN.md](AKENO_INSTALLIEREN.md).

| | |
| --- | --- |
| ![Home](docs/screenshots/01-home.jpg) | ![Anime](docs/screenshots/03-anime.jpg) |
| ![Player with stream information](docs/screenshots/19-player-info.jpg) | ![Diagnostics](docs/screenshots/15-diagnostics.jpg) |

*Rendered by the real interface code on the build machine (`make screenshots`)
with recorded API responses and generated placeholder artwork; more in
[docs/screenshots](docs/screenshots).*

## Feature status

**Console** = seen working on a PS5 (firmware 13.09, version 0.4.1).
**Host-tested** = automated tests on the build machine exercise the real code
path (local HTTP server, generated media, FFmpeg's software decoder standing
in for the console's hardware decoder). **PS5 build** = compiled and linked
into `eboot.bin`, not yet tried on the console.

| Feature | Status | Notes |
| --- | --- | --- |
| Launch, 1080p interface, Inter fonts, DualSense navigation | **Console** | |
| Mode switching with L1/R1 (Home, Anime, YouTube, Library, Settings) | **Console** | The last mode is restored at the next start |
| HTTPS with certificate verification | **Console** | Console CA list; example.com and mux.dev answered |
| HLS playback (MPEG-TS segments) | **Console** | Big Buck Bunny played; resume position saved |
| HLS with fMP4/CMAF segments | Not supported | Refused with a clear message (seen on the console) |
| Hardware H.264 decoding (Videodec2) | **Console** | 720p30 clip: 360/360 frames presented, 0 dropped, 0 decoder errors |
| Hardware HEVC decoding | PS5 build | |
| AAC audio (Audiodec + AudioOut) | **Console** | 0 underruns, 0 output errors; A/V sync by eye not yet reported |
| MP2, AC-3/E-AC-3 audio | PS5 build | |
| Local files: MP4, M4V, MKV, MOV, TS, M2TS | Host-tested, PS5 build | Remuxed on the fly to MPEG-TS by FFmpeg 8.0.1 |
| Stop, pause, seek ±10 s / ±60 s, replay, quality change | Stop: **Console**; rest host-tested | |
| Resume where you left off, history, favourites, settings | **Console** | Files written on the console |
| Offline test clips (no network needed) | **Console** | 2 s 360p clip and a 12 s 720p A/V sync clip |
| Public DRM-free test streams | Big Buck Bunny **Console** | Third-party streams may go offline or use fMP4 |
| Your own streams (`streams.json`) | Host-tested, PS5 build | |
| Anime mode: AniList catalogue, search, details, official links (QR) | **Console** (catalogue and artwork loaded) | Discovery only - AniList has no video |
| YouTube: trending, search, channels (Data API v3, your key) | Host-tested, PS5 build | **No YouTube playback** - see below |
| Crunchyroll | **Unsupported** | Status page explains why and lists legitimate options |
| Diagnostics: network test, FFmpeg self-test, report export | **Console** | Reports exclude keys and tokens |
| Crash reporter, previous-crash notice | PS5 build | No crash since 0.4.1 to test it with |
| USB drives in the library | PS5 build | Title sandbox access is unverified; the app reports what it can reach |
| Subtitles | Not implemented | |
| HDR | Not implemented | HDR streams play without tone mapping |

## Install

1. Download `PPSA99276.zip` from the latest successful
   [Build workflow run](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/actions/workflows/tooling.yml)
   (Artifacts section) and unzip it twice: the GitHub artifact ZIP contains
   `PPSA99276.zip`, which contains the `PPSA99276/` folder.
2. Copy the whole `PPSA99276/` folder over FTP to `/data/homebrew/PPSA99276/`
   (replace the files of an older version). Do not upload the ZIP itself.
3. If an old `/data/homebrew/PPSA99999/` from the v0.1 test is still on the
   console, delete it - it is an obsolete Hello World build.
4. Refresh ShadowMountPlus (or restart it) and start **AKENO STREAM**.

The folder contains `eboot.bin`, `sce_sys/` (param.json, icon, backgrounds),
`sce_module/libc.prx` and `assets/` (fonts and the offline test clips).

## Using the app

| Button | Everywhere | In the player |
| --- | --- | --- |
| L1 / R1 | Previous / next mode | Seek -60 s / +60 s |
| D-pad, left stick | Move | Left/right: seek -10 s / +10 s; up/down: volume |
| Cross | Select | Pause / play (replay at the end, retry after an error) |
| Circle | Back | Stop and close the player |
| Triangle | Search (Anime, YouTube) | Subtitles (shows "not available") |
| Square | Add / remove favourite | Change maximum quality (HLS) |
| OPTIONS | Service information | Stream information panel |

**First test:** Home -> *Offline Test Clips* -> *A/V Sync Test Clip*. It plays
from the app folder without network access: a test pattern with a seconds
counter and a beep once a second. If you see the picture and hear the beeps in
sync, hardware video and audio work.

### Where files go

Over FTP a PC can **write** only to the install folder
`/data/homebrew/PPSA99276/` (the app sees it as `/app0`). The app's own data
folder (`/download0/akeno` inside the app) can only be **read** from a PC, and
only while AKENO STREAM is running, at
`/mnt/sandbox/PPSA99276_000/download0/akeno/`. When the app is closed, that
data lives in the image `/user/download/PPSA99276/download0.dat` (readable
with [UFS2Tool](https://github.com/SvenGDK/UFS2Tool)).

| What | Copy to (from a PC) |
| --- | --- |
| Your video files | `/data/homebrew/PPSA99276/media/` |
| Your stream list | `/data/homebrew/PPSA99276/streams.json` |
| YouTube API key (optional) | `/data/homebrew/PPSA99276/youtube-key.txt` |

| What | Read from (while the app runs) |
| --- | --- |
| Diagnostics reports, `crash-previous.txt` | `/mnt/sandbox/PPSA99276_000/download0/akeno/` |

Files in the install folder are kept when you update the app by copying the
new `PPSA99276/` folder over the old one.

### Your own media

- **Files:** copy MP4, MKV, MOV or TS files to
  `/data/homebrew/PPSA99276/media/` and open them in *Library* -> *Media in
  the install folder*. USB drives are listed too; the Library shows "No access
  from the title sandbox" if the console does not let the app read them.
- **Streams:** create `/data/homebrew/PPSA99276/streams.json`:

  ```json
  {
    "streams": [
      { "title": "My HLS stream", "url": "https://example.com/live/master.m3u8", "live": true },
      { "title": "A TS file", "url": "https://example.com/video.ts", "type": "ts",
        "subtitle": "Optional", "description": "Optional", "image": "https://example.com/poster.jpg" }
    ]
  }
  ```

  `title` and an `http(s)` `url` are required; `type` is `hls` (default) or
  `ts`. Only add streams you are allowed to watch. The list appears in Home ->
  *Open Streams*.

### YouTube

YouTube mode uses the official YouTube Data API v3 with **your own free API
key** (Google Cloud Console -> enable "YouTube Data API v3" -> create an API
key). Enter it in YouTube mode or Settings with the on-screen keyboard, or put
it in `/data/homebrew/PPSA99276/youtube-key.txt` and press Square in YouTube
mode; delete the file afterwards (the app cannot delete files in its install
folder). The key is stored in the app's data folder (`secrets.json`), never
shown in logs or diagnostics reports. A search costs 100 of the default 10,000
daily quota units, a trending page 1 unit.

YouTube's terms allow playback only in YouTube's own players. AKENO STREAM
does not extract stream URLs; each video shows a QR code to open it on your
phone. Sign-in (Google OAuth for TV devices) would need a registered client ID
and is not part of this build.

### Anime mode and Crunchyroll

Anime mode shows trending, seasonal, popular and top-rated anime from the
public [AniList](https://anilist.co) API with descriptions, episode counts,
trailers and the official streaming sites for each title (as QR codes).
AniList provides no video, and AKENO STREAM does not scrape video sites.

Crunchyroll has no public API, its sign-in is for its own apps, and its
streams are DRM-protected (Widevine/PlayReady). Integrating it would require
circumventing that protection, which this project will not do. Use the
official Crunchyroll app on PS5. The *Crunchyroll* card on Home explains this
in the app. Open animated films (Blender Foundation, CC BY) are playable in
Anime mode.

### If the app crashes

The app shows its startup steps on screen ("Loading fonts...", "Starting
network...") and catches crashes: before the system's error dialog appears,
a notification reads for example *"AKENO STREAM 0.4.2 crashed: SIGSEGV (invalid
memory access) at eboot+0x1a2b3c, address 0x0, during startup: fonts"*. A
photo of it is the most useful bug report. At the next start the app says that
the last session crashed, lists the first line under Settings -> Diagnostics
and keeps the full text with a backtrace as `crash-previous.txt` in its data
folder. The `eboot+0x…` offsets map to functions with the `build/llvm-pie.elf`
of the same build (CI uploads it with the screenshots).

### Diagnostics

Settings -> *Diagnostics* shows system, network, media and last-playback
information and can run a network test, an FFmpeg media self-test and the
hardware test clip. *Export report* writes
`akeno-diagnostics-YYYYMMDD-HHMMSS.txt` to the app's data folder; while the
app is still running, fetch it over FTP from
`/mnt/sandbox/PPSA99276_000/download0/akeno/` and attach it to bug reports. API keys, tokens and signed URL parameters are
redacted.

## Limitations

- HLS: clear (unencrypted) MPEG-TS segments only. Encrypted (AES-128,
  SAMPLE-AES, DRM), fMP4/CMAF (`EXT-X-MAP`) and byte-range playlists are
  refused with a message. Variants whose audio is only in a separate
  rendition play without sound when no muxed variant exists.
- MP3 audio inside MPEG-TS is not supported (the stream plays without audio
  with a notice); MP2 is.
- Video: H.264 and HEVC (8/10-bit 4:2:0). VP9/AV1 are not supported.
- No subtitles, no HDR tone mapping, no picture-in-picture, no download
  manager.
- Third-party test streams can disappear at any time.
- USB access from inside the title sandbox has not been confirmed on 12.20.

## Building from source

Linux or WSL with Clang 18, lld 18, Ninja, Python 3, pkg-config and (for the
tests) FFmpeg's command-line tools, FreeType and libcurl development files:

```sh
sudo apt-get install clang-18 lld-18 clang-format-18 libclang-rt-18-dev ninja-build ccache \
  pkg-config python3 ffmpeg libfreetype-dev libcurl4-openssl-dev
make            # PS5 build: dist/PPSA99276/ and dist/PPSA99276.zip
make test-unit  # 64 host tests (ASan/UBSan), local HTTP server, generated media
make screenshots  # renders every screen to build/screenshots/*.png
make lint
```

The first build downloads and verifies the public PS5 payload SDK, PacBrew's
prebuilt libcurl/OpenSSL/FreeType and FFmpeg 8.0.1's source (built twice: for
the PS5 and for the host tests) into the ignored `.deps/` folder. `make app`
also runs `tools/check-imports.py`, which fails the build if any import would
bind to a system module that does not work in native titles. The GitHub
workflow runs all of this and uploads the ZIP, screenshots and import table.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how the app is put
together, [docs/V03_ROOT_CAUSE.md](docs/V03_ROOT_CAUSE.md) for the analysis of
the v0.3 playback failure and [docs/BOILERPLATE.md](docs/BOILERPLATE.md) for
the underlying build tooling.

## Credits and licences

AKENO STREAM is GPL-3.0-or-later. It builds on
[ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
(GPL-3.0-or-later) and vendors the native TS demuxer and Videodec2/Audiodec
backend of [ProsperoTV](https://github.com/blackbearreloaded/ProsperoTV)
(GPL-3.0-or-later). It uses FFmpeg (LGPL-2.1-or-later), libcurl, OpenSSL,
FreeType, the Inter typeface (SIL OFL 1.1), stb_image, qrcodegen and minimp3.
Catalogue data comes from AniList and the YouTube Data API under their terms.
Full notices: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

This project is not affiliated with Sony, Crunchyroll, Google/YouTube or
AniList.
