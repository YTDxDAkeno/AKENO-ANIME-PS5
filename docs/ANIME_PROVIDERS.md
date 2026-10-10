# Anime streaming providers

Anime mode separates three jobs, the way modular media centres such as Kodi
separate metadata scrapers, source add-ons and the player:

| Role | Answers | In AKENO STREAM 1.0 |
| --- | --- | --- |
| **Catalogue** (metadata) | Which titles exist: artwork, descriptions, seasons, episode counts, genres, ratings, search | [AniList](https://anilist.co) (public API) |
| **Episode provider** | Which series, seasons and episodes a source really offers, with its own ids, languages, subtitles and restrictions | The bundled sample series, AKENO catalogues, PeerTube channels and playlists |
| **Playback provider** | The authorized, DRM-free resource for one episode - or why there is none | The same providers; the resource is checked before it is offered |

AniList never supplies video. A title from AniList is *catalogue only* until a
provider lists its episodes, and an episode is offered as **playable** only
after AKENO has read its first bytes and found DRM-free media it can decode.
Series from different providers are linked only by an explicit `anilistId`,
never by guessing from similar titles. Nothing is invented: an episode
without a title has no title, an episode without a video has no Play button.

## What counts as playable

When a series opens (and when a provider is tested) AKENO fetches the first
64 KiB of an episode's resource:

| First bytes | Result |
| --- | --- |
| MP4 / fragmented MP4, Matroska, MPEG-TS, an HLS playlist | **Playable** (H.264/HEVC video with AAC, MP2 or AC-3 audio, as the player supports) |
| MP4 with encryption boxes (`encv`, `enca`, `pssh`, `sinf`), HLS with `SAMPLE-AES` or a DRM key system | **DRM not supported** - never played, never bypassed |
| An HTML page, JSON, an error page | Not media |
| HTTP 401/403 | **Sign-in required** |
| HTTP 404/410 / file missing | **Missing** |
| No answer | **Network error** (checked again later) |

AES-128 HLS (clear keys published by the stream) and plain HTTP(S) files
play; MPEG-DASH does not (the native player has no DASH demuxer).

## Adding a provider on the console

*Anime -> Streaming Providers -> Add Streaming Provider*:

1. **Provider name** (optional; taken from the source when empty).
2. **Address**: an AKENO catalogue URL, or a PeerTube channel or playlist
   (`https://host/c/<channel>`, `https://host/video-channels/<channel>`,
   `https://host/w/p/<playlist>`, `https://host/video-playlists/<playlist>`).
3. **AniList id** (optional): for a PeerTube channel that is one series, so
   the AniList page of that title offers the channel's episodes.
4. **Test Connection**. The compatibility report shows:
   - *What it is*: a native provider, a documented API, a website, a single
     video, an M3U list, metadata without video, a protected source, or
     unreachable;
   - *Format*: `AKENO catalogue v1`, `PeerTube channel`, `PeerTube playlist`;
   - *Catalogue*: series, seasons, episodes and how many name a video;
   - *Offers*: what the loaded data actually contains - seasons, episode
     lists and descriptions, artwork, audio and subtitle languages, AniList
     links, checked native playback;
   - *Episodes checked*: playable / DRM / sign-in / region / missing /
     unreachable / unsupported, for up to 12 episodes;
   - every validation error and note, by path
     (`series[0].seasons[1].episodes[3].media.url: ...`);
   - a *preview* of the first series that Import would add.
5. **Import Provider** is enabled only when at least one checked episode is
   playable (or when the episodes could not be reached at all, so the check
   is repeated later).

In *Streaming Providers*: Cross shows a provider's report, Square removes it
(built-in providers stay), Triangle checks every provider again. Providers
are stored in `/download0/akeno/anime-providers.json` (at most 30) and
survive updates of the app.

A website's homepage is **not** a provider: it stays in Websites, where the
browser shows it. *Websites -> (a saved site) -> Check for native streaming*
runs the same check on a saved address. *Find Public Video on a Page*
(Websites; also *Find public videos (native)* in a saved site's options) lists videos a page publishes openly in its HTML
(`<video>`/`<source>` elements, video meta tags, direct `.mp4`/`.m3u8` links)
and plays them in AKENO's player - it does not run the page's player or
scripts, never touches protected streams, and is not a universal extractor
(see [NATIVE_PUBLIC_VIDEO.md](NATIVE_PUBLIC_VIDEO.md)).

### Your own library on the console

Put a catalogue named `anime-catalog.json` into the install folder
(`/data/homebrew/PPSA99276/`, over FTP) next to your video files. It appears
as the built-in provider *My Anime Library*. Relative addresses resolve next
to the catalogue; absolute console paths are allowed; `..` is refused.
The file survives app updates as long as you do not delete it (the release
ZIP never contains it).

## The AKENO catalogue format (version 1)

A UTF-8 JSON file, at most 8 MiB, served over HTTPS (plain HTTP works with a
warning) or stored on the console. Schema:
[`schema/akeno-catalog.schema.json`](schema/akeno-catalog.schema.json).
Complete example:
[`examples/anime-catalog/catalog.json`](../examples/anime-catalog/catalog.json)
(a unit test keeps it valid). AKENO's validator
(`src/anime/catalog_format.cpp`) is the authority: it reports each problem
with its path, skips broken entries and keeps the rest.

```json
{
  "akenoCatalog": 1,
  "provider": {"id": "my-studio", "name": "My Studio", "licence": "CC BY 4.0"},
  "series": [{
    "id": "my-show", "title": "My Show", "altTitles": ["Boku no Show"],
    "poster": "art/poster.jpg", "genres": ["Drama"], "year": 2025, "status": "releasing",
    "seasons": [{"number": 1, "episodes": [
      {"number": 1, "title": "Pilot", "duration": 1440,
       "media": {"url": "video/s1e1.mp4",
                 "audio": [{"language": "ja", "label": "Japanese"}],
                 "subtitles": [{"language": "en", "label": "English", "url": "subs/s1e1.en.vtt"}]}}
    ]}]
  }]
}
```

### Fields

**Root**: `akenoCatalog` (required, `1`), `provider` (required), `series`
(required list, at most 500).

**provider**: `name` (required, 60 chars), `id` (letters, digits, `-`, `_`,
`.`; default: slug of the name), `description`, `homepage`, `licence`,
`attribution` (defaults for every series).

**series**: `title` (required), `seasons` (required, at least one, at most
60), `id` (default: slug of the title; unique), `altTitles` (romaji, English
and other names - searched like the title; at most 12), `description`,
`poster` (portrait), `banner` (wide), `genres` (at most 12), `year`,
`status` (`finished`, `releasing`, `upcoming`), `score` (0-100),
`anilistId`, `malId`, `licence`, `attribution`, `updated` (`YYYY-MM-DD`,
feeds *Recently Updated*).

**season**: `number` (default: position; `0` = Specials, listed last), `id`
(default `s<number>`), `title` (default "Season N"), `year`, `anilistId`,
`episodeCount` (announced total when fewer are listed), `episodes`. At most
3000 episodes per series.

**episode**: `number` (default: position), `id` (default: the number),
`special` (listed after the numbered episodes), `title`, `description`,
`thumbnail`, `duration` (seconds), `airDate` (`YYYY-MM-DD`), `media`.

**media**: `url` (required), `type` (`mp4`, `m4v`, `mov`, `mkv`, `hls`/`m3u8`,
`ts`/`mpegts`; default: the extension), `audio` (list of
`{language, label}`), `subtitles` (list of `{language, label, url, format}`;
WebVTT `.vtt` and SubRip `.srt`), `drm` (a declared DRM system - the
episode is shown as *DRM not supported*), `requiresAuth` (*Sign-in
required*), `regions` (ISO 3166 codes; elsewhere *Not offered in your
region*).

### Addresses

- `https://...` and `http://...` are used as given.
- Relative addresses resolve against the catalogue's address. A web
  catalogue can only reach http(s) addresses: `/path` means the catalogue's
  own host, never a file on the console.
- A catalogue on console storage may name files next to it or absolute
  console paths, without `..`.
- `javascript:`, `data:`, `file:`, protocol-relative `//host` and other
  schemes are refused.

### Hosting a catalogue

Any static web server works (a GitHub Pages site, an object store, a
self-hosted PeerTube or Jellyfin export converted to this format). There are
no cookies, no sign-in and no CORS involved; AKENO sends a plain GET with
TLS certificate verification. Only list media you are allowed to
distribute; AKENO plays only DRM-free resources.

## PeerTube

A channel becomes one series: its public playlists are seasons (in creation
order) and playlist positions are episode numbers. A channel without public
playlists is one season of its videos in publishing order. A single playlist
is a one-season series. The video's HLS playlist or MP4 file and its WebVTT
captions are looked up from the PeerTube API when an episode plays - the
same path as Discover, which is confirmed on the console.

## For developers: adding a provider type

Native code is not loaded at run time. A PS5 native title only executes the
signed executable and modules inside its own package, the app runs in the
system sandbox, and downloading code would be remote code execution. A
versioned dynamic plugin interface would therefore add risk without adding
reach. AKENO uses a **compile-time adapter registry** plus the **declarative
catalogue format** above (the equivalent of a Kodi "source add-on" that
returns listings and resolved URLs, without executing code):

1. Add the type to `ProviderType` in `src/anime/provider.hpp`, with its id
   and label (`provider_type_id`, `provider_type_label`,
   `provider_type_from_id` in `provider.cpp`).
2. Implement a `SourceProvider` (`EpisodeProvider::load` lists series,
   seasons and episodes; `PlaybackProvider::resolve` returns the resource for
   one episode or an `Availability` with the reason) and return it from
   `make_provider`.
3. Teach `check_source` (`src/anime/registry.cpp`) to recognise the address
   so *Add Streaming Provider* can test and import it.
4. Add host tests with the local HTTP server (`tests/host/test_server.hpp`)
   and recorded responses; `tests/unit/test_anime.cpp` shows the pattern.

Rules for every adapter:

- Treat every response as untrusted input: bounded sizes (`fetch_text`
  limits, `kMaxCatalogSeries`, `kMaxEpisodesPerSeries`), validated ids and
  addresses, no unbounded recursion.
- Use only documented, authorized APIs and media the provider publishes for
  playback. No scraping of video sites, no player or signature
  reverse-engineering, no cookie or session import from the browser.
- Never decrypt protected media. Declare DRM, sign-in and regional
  restrictions as availability states instead.
- Never log passwords, tokens or keys; addresses in logs and reports go
  through `url::redact`.
- Leave the player alone: a provider hands a `MediaResource` to the existing
  native player (`media::Player`), which already handles HLS, MP4, MPEG-TS,
  Matroska, subtitles, resume and Up Next.

Crunchyroll and other DRM services: see [CRUNCHYROLL.md](CRUNCHYROLL.md).
