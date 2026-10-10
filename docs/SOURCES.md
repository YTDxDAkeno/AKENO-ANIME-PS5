# Sources: your own lists, feeds and addresses

AKENO STREAM plays what you point it at. It ships **no** third-party sources,
it does not search for any, and it contains no code for particular websites.
The Sources mode gives you the means to add lists and streams yourself; you
decide what to add and you are responsible for having the right to watch it.

What the app will not do, whatever the source:

- read video links out of web pages (no scraping, no site-specific extractors)
- sign requests, solve challenges, fake referrers or otherwise work around a
  service's access rules
- decrypt DRM (FairPlay, Widevine, PlayReady, SAMPLE-AES); such entries are
  skipped or refused with a message

## Adding a source

**From your phone (easiest):** Sources -> *Add from Phone*. The app shows a
QR code and an address such as `http://192.168.1.20:8090/abcd2345`. Open it on
a phone or computer in the same network, paste the address of a list, feed
or stream, optionally a name, and tap *Add to the PS5*. The page:

- runs only while that screen is open on the PS5, and stops when you close it;
- answers only at the one-time code in the address (a new code every time);
- stops after 30 wrong requests or 20 additions;
- accepts only `http://` and `https://` addresses up to 2,048 characters.

**On the console:** Sources -> *Add a Source*. The first time, a note
explains the above. Type the address (R1 or the `#+=` key opens `:` `/` `?`
`=` `&` `%` and friends), then a name. Square on a source removes it (after a
question). *Play an Address* plays one link without saving it.

**From a PC** (easier than typing addresses): copy one of these files to the
install folder `/data/homebrew/PPSA99276/`. They are read every time you open
Sources; their entries carry a **PC** badge and are edited in the file.

`sources.txt` - one source per line, `#` starts a comment (the package
contains `sources-example.txt` as a template; updates never overwrite your
`sources.txt`):

```text
# Name = address
My TV list = https://example.com/lists/tv.m3u
Club videos = https://example.com/feeds/club.json
https://example.com/live/stream.m3u8
```

`Name | address` and a bare address (named after its host) work too.

`sources.json`:

```json
{
  "sources": [
    { "name": "My TV list", "url": "https://example.com/lists/tv.m3u" },
    { "name": "Club videos", "url": "https://example.com/feeds/club.json" }
  ]
}
```

Sources added on the console are stored in the app's data folder
(`sources.json` there). Addresses can contain personal tokens, so this file is
**not** included in diagnostics reports, and addresses shown in diagnostics
are redacted.

Only `http://` and `https://` addresses are accepted. HTTPS certificates are
always verified.

## What a source can be

When you open a source, the app downloads it and decides from its content:

| Content | Shown as |
| --- | --- |
| M3U / M3U8 list (`#EXTM3U` with `#EXTINF` entries, or a plain list of addresses) | Rows by `group-title` (or `#EXTGRP`), logos from `tvg-logo`, durations as badges |
| AKENO JSON feed (below) | Rows by `group`, artwork, descriptions |
| HLS playlist (`#EXT-X-…` tags) | One playable entry |
| MPEG-TS stream, MP4/MKV/MOV file | One playable entry |
| Anything else (for example an HTML page) | A message; nothing is extracted |

Limits: lists up to 16 MB, the first 5,000 entries, 60 rows (the rest goes to
a *More* row). An entry that points to another `.m3u` list, or a feed entry
with `"type": "feed"`, opens as a nested source.

Skipped with a note: entries with a DRM licence (`#KODIPROP` licence lines,
feed entries with `"drm": true` or a `license`), MPEG-DASH (`.mpd`), and
other protocols (`rtmp://`, `udp://`, `rtsp://` …). `#EXTVLCOPT` and other
player options in lists are ignored.

Inside a source, Triangle searches entry names, Square adds an entry to
Favorites and Cross plays it (or opens a nested list).

## AKENO JSON feed

```json
{
  "title": "Club Recordings",
  "description": "Optional",
  "items": [
    {
      "title": "Final",
      "url": "videos/final.mp4",
      "group": "2026",
      "image": "art/final.jpg",
      "description": "Optional",
      "duration": 754
    },
    { "title": "Stage", "url": "https://example.com/stage/index.m3u8", "live": true },
    { "title": "Archive", "url": "https://example.com/archive.json", "type": "feed" }
  ]
}
```

- `title` and `url` are required per item; relative addresses are resolved
  against the feed's address.
- `group` (or `category`) makes rows; `image`/`logo`/`thumbnail`/`poster`
  give artwork; `subtitle`, `description`, `duration` (seconds) and
  `live: true` are optional.
- `type`: `hls`, `ts`, `file`, `auto` (detect) or `feed` (a nested source).
  Without `type` the kind is guessed from the extension (`.m3u8` HLS, `.ts`
  MPEG-TS, `.mp4`/`.mkv`/`.mov` file) and otherwise detected when played.
- `"streams"` or `"entries"` are accepted instead of `"items"`, and a plain
  list of items works too, so the older `streams.json` format is a valid feed.

## What plays

| | |
| --- | --- |
| HLS | MPEG-TS or fragmented-MP4 (CMAF) segments, `EXT-X-BYTERANGE`, a separate audio rendition (`EXT-X-MEDIA`), AES-128 segment encryption (the standard HLS method; the key is fetched like a segment), VOD and live |
| MPEG-TS | over HTTP, live or file |
| Files | MP4, M4V, MKV, MOV on a web server; seeking uses HTTP range requests (servers without range support work, seeking is slower) |
| Video | H.264, HEVC (8/10-bit 4:2:0), decoded by the PS5 hardware decoder |
| Audio | AAC, AC-3, E-AC-3, MP2; other codecs play the video with a notice |

Not supported: MPEG-DASH, VP9/AV1, DRM of any kind, subtitles. A web page
address gives the message "This address is a web page, not a stream".

## Status

The Sources mode and the streaming additions are covered by host tests
(parsers, a local HTTP server with range support, fragmented-MP4, separate
audio, AES-128 and byte-range fixtures, the phone page, random controller
input) and are in the PS5 build. The tester reported that 0.5.0 works on the
console (not itemised); the phone page (0.6.0) has not been tried on a
console yet. Section 6b of the
[hardware acceptance checklist](HARDWARE_ACCEPTANCE.md) lists what to check.
