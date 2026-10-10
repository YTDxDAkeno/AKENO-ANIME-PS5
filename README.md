# AKENO STREAM for PS5

[![Build](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/actions/workflows/tooling.yml/badge.svg)](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/actions/workflows/tooling.yml)
[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg)](LICENSE)

AKENO STREAM is a native, controller-operated media app for jailbroken PS5
consoles. It plays DRM-free video with audio - HLS streams (MPEG-TS or
fragmented MP4), MPEG-TS over HTTP, MP4/MKV files on web servers and your own
MP4, MKV, MOV and TS files - through the console's hardware video decoder.
**Discover** plays free video from PeerTube and public-domain films from the
Internet Archive; **Sources** takes your own M3U lists, JSON feeds and stream
addresses (also from your phone); the app adds an anime discovery mode, a
YouTube browser that hands videos to the official YouTube app, a local
library, watch history, favourites and a diagnostics screen.

| | |
| --- | --- |
| Title ID | `PPSA99276` (unchanged since v0.1) |
| Version | 0.6.0 (`contentVersion` 01.006.000) |
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
> below. 0.5.0 added the Sources mode, fragmented-MP4 HLS, separate audio
> tracks, AES-128 HLS and web files; the tester reported that 0.5.0 works on
> the console (not itemised). 0.6.0 adds Discover (PeerTube, Internet
> Archive), adding sources from a phone and hand-offs to the YouTube app and
> the web browser; these are host-tested and still need a console run.
> Please keep reporting with the
> [hardware acceptance checklist](docs/HARDWARE_ACCEPTANCE.md).

German installation notes: [AKENO_INSTALLIEREN.md](AKENO_INSTALLIEREN.md).

| | |
| --- | --- |
| ![Home](docs/screenshots/01-home.jpg) | ![Anime](docs/screenshots/03-anime.jpg) |
| ![Discover: PeerTube and Internet Archive](docs/screenshots/13-discover.jpg) | ![Add sources from a phone](docs/screenshots/21-sources-phone.jpg) |
| ![Sources](docs/screenshots/17-sources.jpg) | ![A source's entries](docs/screenshots/18-source-list.jpg) |
| ![Player with stream information](docs/screenshots/27-player-info.jpg) | ![Diagnostics](docs/screenshots/23-diagnostics.jpg) |

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
| Mode switching with L1/R1 (Home, Anime, YouTube, Discover, Library, Sources, Settings) | **Console** (Discover tab new in 0.6.0) | The last mode is restored at the next start |
| HTTPS with certificate verification | **Console** | Console CA list; example.com and mux.dev answered |
| HLS playback (MPEG-TS segments) | **Console** | Big Buck Bunny played; resume position saved |
| HLS with fMP4/CMAF segments (`EXT-X-MAP`) | Host-tested, PS5 build | New in 0.5.0; remuxed by FFmpeg to MPEG-TS for the hardware pipeline |
| HLS with a separate audio rendition (`EXT-X-MEDIA`) | Host-tested, PS5 build | New in 0.5.0; video and audio playlists are merged by time |
| HLS with AES-128 segment encryption, byte ranges | Host-tested, PS5 build | New in 0.5.0; the standard HLS method, not DRM. SAMPLE-AES/FairPlay/Widevine/PlayReady are refused |
| MP4, MKV, MOV and TS files on web servers | Host-tested, PS5 build | New in 0.5.0; seeking with HTTP range requests (also works, slower, without them) |
| Sources: M3U/M3U8 lists, AKENO JSON feeds, stream addresses you add | Host-tested, PS5 build; tester reports 0.5.0 works | See [docs/SOURCES.md](docs/SOURCES.md). The app ships no sources |
| Add sources from a phone (QR code, page on the local network) | Host-tested, PS5 build | New in 0.6.0; works only while the screen is open, with a one-time code |
| Discover: PeerTube (Sepia Search, instance API) with in-app playback | Host-tested (recorded responses), PS5 build | New in 0.6.0; the live services were not reachable from the build machine, so their real answers are untested |
| Discover: Internet Archive public-domain films and cartoons with in-app playback | Host-tested (recorded responses, local playback test), PS5 build | New in 0.6.0; curated collections only |
| Hardware H.264 decoding (Videodec2) | **Console** | 720p30 clip: 360/360 frames presented, 0 dropped, 0 decoder errors |
| Hardware HEVC decoding | PS5 build | |
| AAC audio (Audiodec + AudioOut) | **Console** | 0 underruns, 0 output errors; A/V sync by eye not yet reported |
| MP2, AC-3/E-AC-3 audio | PS5 build | |
| Local files: MP4, M4V, MKV, MOV, TS, M2TS | Host-tested, PS5 build | Remuxed on the fly to MPEG-TS by FFmpeg 8.0.1 |
| Stop, pause, seek ±10 s / ±60 s, replay, quality change | Stop: **Console**; rest host-tested | |
| Resume where you left off, history, favourites, settings | **Console** | Files written on the console |
| Offline test clips (no network needed) | **Console** | 2 s 360p clip and a 12 s 720p A/V sync clip |
| Public DRM-free test streams | Big Buck Bunny **Console** | Third-party streams may go offline |
| Your own streams (`streams.json`) | Host-tested, PS5 build | Still read; Sources is the more flexible way |
| Anime mode: AniList catalogue, search, details, official links (QR) | **Console** (catalogue and artwork loaded) | Discovery only - AniList has no video |
| YouTube: trending, search, channels (Data API v3, your key) | Host-tested, PS5 build | **No playback inside AKENO STREAM** - see below |
| YouTube: open the official YouTube app, open a video in the web browser | PS5 build (experimental) | New in 0.6.0; uses system functions looked up at run time; untested on the console |
| Crunchyroll | **Unsupported** | Status page explains why and lists legitimate options |
| Diagnostics: network test, FFmpeg self-test, report export | **Console** | Reports exclude keys and tokens |
| Crash reporter, previous-crash notice | PS5 build | No crash since 0.4.1 to test it with |
| On-screen keyboard with symbols, secret masking | Host-tested, PS5 build | 0.4.3 fixed the crash while typing a key (seen on the console with 0.4.2); long addresses scroll to the cursor |
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
`sce_module/libc.prx`, `assets/` (fonts and the offline test clips) and
`sources-example.txt` (a template, see *Sources* below).

## Using the app

| Button | Everywhere | In the player |
| --- | --- | --- |
| L1 / R1 | Previous / next mode | Seek -60 s / +60 s |
| D-pad, left stick | Move | Left/right: seek -10 s / +10 s; up/down: volume |
| Cross | Select | Pause / play (replay at the end, retry after an error) |
| Circle | Back | Stop and close the player |
| Triangle | Search (Anime, YouTube, Discover, inside a source) | Subtitles (shows "not available") |
| Square | Add / remove favourite (Sources: remove a source) | Change maximum quality (HLS) |
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
| Your sources | `/data/homebrew/PPSA99276/sources.txt` (or `sources.json`) |
| Your stream list (older format) | `/data/homebrew/PPSA99276/streams.json` |
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

### Sources (your own lists, feeds and addresses)

The **Sources** mode lets you add what you want to watch; the app itself ships
no sources and does not look for any. You decide what to add and you are
responsible for having the right to watch it.

- **From your phone:** Sources -> *Add from Phone* shows a QR code. Scan it
  with a phone on the same network, paste the address, tap *Add to the PS5*.
  The page works only while that screen is open and only with its one-time
  code.
- **On the console:** Sources -> *Add a Source*, type the address (the keyboard
  has a symbols page on R1 for `:/?=&`), then a name. *Play an Address* plays a
  link once without saving it.
- **From a PC (easier):** put `sources.txt` into
  `/data/homebrew/PPSA99276/` (start from the packaged `sources-example.txt`),
  one source per line:

  ```text
  # Name = address
  My TV list = https://example.com/lists/tv.m3u
  Club videos = https://example.com/feeds/club.json
  https://example.com/live/stream.m3u8
  ```

A source can be an **M3U/M3U8 list** (entries grouped by `group-title`, logos
from `tvg-logo`), an **AKENO JSON feed**, an **HLS playlist**, an **MPEG-TS
stream** or an **MP4/MKV file**. Open a source to browse it, Triangle searches
inside it. DRM-protected entries, MPEG-DASH (`.mpd`) and non-HTTP protocols
(rtmp, udp) are skipped with a note. Nothing is read out of web pages and no
site protection is bypassed. Details and the feed format:
[docs/SOURCES.md](docs/SOURCES.md).

### Discover: PeerTube and the Internet Archive

**Discover** shows free video that plays right in the app:

- **PeerTube**, the open, federated video platform: latest videos, films,
  art & animation, science and kids' rows from
  [Sepia Search](https://sepiasearch.org) (the PeerTube search index, with
  sensitive content excluded) and channels of a few well-known instances
  (Blender Studio's open movies, Framatube, TILvids). Videos stream from
  their instance as HLS or MP4 without DRM.
- **Internet Archive**: Feature Films (films the archive believes to be in
  the public domain), classic cartoons up to 1963, silent films and the
  Prelinger Archives, through the archive's documented search and metadata
  APIs. Only curated collections are listed, not arbitrary uploads.

Triangle searches both at once. Open an entry for details, then *Play*.

### YouTube

YouTube mode uses the official YouTube Data API v3 with **your own free API
key** (Google Cloud Console -> enable "YouTube Data API v3" -> create an API
key). Enter it in YouTube mode or Settings with the on-screen keyboard, or put
it in `/data/homebrew/PPSA99276/youtube-key.txt`: the app imports it at the
next start (or when you press Square in YouTube mode); delete the file
afterwards (the app cannot delete files in its install folder). The key is stored in the app's data folder (`secrets.json`), never
shown in logs or diagnostics reports. A search costs 100 of the default 10,000
daily quota units, a trending page 1 unit.

YouTube's terms allow playback only in YouTube's own players. AKENO STREAM
does not extract stream URLs. Instead, a video's details offer **YouTube app**
(starts the official PS5 YouTube app), **Browser** (opens the video page in
the PS5 web browser) and a QR code for your phone; YouTube mode itself starts
with an *Open the YouTube App* card. The two hand-offs are experimental:
whether the firmware lets a homebrew title start other apps, and whether the
YouTube app opens the exact video, has not been tested on a console yet.
Sign-in (Google OAuth for TV devices) would need a registered client ID and is
not part of this build.

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
a notification reads for example *"AKENO STREAM 0.6.0 crashed: SIGSEGV (invalid
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

- HLS: MPEG-TS and fragmented-MP4 segments, byte ranges, separate audio
  renditions and AES-128. SAMPLE-AES and every DRM system (FairPlay,
  Widevine, PlayReady) are refused with a message - DRM is not circumvented.
  Packed-audio renditions (raw `.aac` segments) carry no timestamps the app
  reads, so their sync is not guaranteed. The first audio rendition chosen
  (default > autoselect > first) is used; there is no audio-track menu yet.
- MPEG-DASH (`.mpd`) is not supported.
- Web pages are never searched for videos: a source must be a list, feed,
  playlist or media address. Sites that only show videos inside their own web
  player (including unlicensed anime sites) are not supported, and no
  site-specific extractors will be added.
- No anime streaming service is integrated: the legal services use DRM or
  closed APIs. Anime mode is for discovery with links to official services.
- MP3 audio inside MPEG-TS is not supported (the stream plays without audio
  with a notice); MP2 is.
- Video: H.264 and HEVC (8/10-bit 4:2:0). VP9/AV1 are not supported.
- No subtitles, no HDR tone mapping, no picture-in-picture, no download
  manager.
- Third-party test streams can disappear at any time.
- USB access from inside the title sandbox has not been confirmed on the
  console (firmware 13.09).

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
