# Provider compatibility assessment (EverythingMoe as a discovery source)

Which anime and video sources can AKENO STREAM play **natively** - in its own
player, not the PS5 browser - with permission? This assessment uses
[EverythingMoe](https://everythingmoe.com/), a community-curated directory of
anime websites, as the list of candidates, and adds the open sources AKENO
STREAM already supports. Written 2026-10-11 for 1.1.0-rc.2.

**Result in short.** EverythingMoe is a good map of *what exists*, not a list
of what may be integrated. Of its 86 active anime-streaming entries, **8** carry
its `licensed` tag; every one of those is a subscription, DRM-protected or
region-locked service, or YouTube (official embedded player only). The other
78 are unlicensed sites; the two investigated in depth (Miruro, AnimePahe) and
Anikoto have no permitted way to their video files. **No EverythingMoe entry is
an authorized native provider.** The native workflow AKENO STREAM can ship
and verify uses documented, open sources: PeerTube instances, the Internet
Archive and catalogues of the user's own files.

## 1. What EverythingMoe is

- A community-curated directory: ranked lists per section (Anime Streaming,
  Manga, Donghua, Apps, Download, Database/Tracker, ...), community reviews,
  mirror domains, uptime pings and a "graveyard" of dead sites.
- **Not a streaming API and not an authority on legality.** It has no public
  API; it states no permission to embed or redistribute anything it lists.
- Each entry has filter labels (`Self-host`, `Scraper`, `Soft-sub`,
  `Hard-sub`, `Dub friendly`, `Easy download`, `Third party`, `Old reliable`,
  ...) and tags. The `licensed` tag marks official services (unofficial
  front ends render it as "Licensed, official service").

### Source of the figures

`everythingmoe.com` could not be reached from the build environment (its
network policy blocks it, and the web fetcher could not resolve it). The
figures below come from a snapshot of the directory published by an
unofficial scraper project, read but never run:
[Varomine/everythingmoe-api](https://github.com/Varomine/everythingmoe-api)
`data.json` at `91a30bc`, scraped **2026-06-22** (1,058 entries: 759 active,
299 dead, 24 sections). The site's structure was cross-checked with two
other unofficial clients,
[itznan/EverythingMoeAPI](https://github.com/itznan/EverythingMoeAPI)
(`d7aeb61`) and [quantavil/everythingmoe](https://github.com/quantavil/everythingmoe)
(`d536b72`). The directory changes daily; treat the numbers as a June 2026
snapshot.

### Anime Streaming in the snapshot

| | |
| --- | --- |
| Active entries | 86 (138-139 more in the graveyard: Gogoanime, AniWave/9Anime, AnimeKai, ...) |
| `licensed` | 8: Crunchyroll, Bilibili, YouTube, HIDIVE, Netflix, OceanVeil, RetroCrush, Hulu |
| `Self-host` (hosts its own encodes) | 34 |
| `Scraper` (shows other sites' video) | 37 |
| Top five | Anikoto, AnimePahe, MKissa, AniDB (`anidb.app`, not the AniDB database), Miruro |
| PeerTube, Internet Archive, Creative Commons, public domain | **none listed** |

## 2. Criteria

| Criterion | What counts |
| --- | --- |
| Public documentation | An API or data format the operator documents for third parties |
| Integration permission | Terms or a licence that allow a third-party client to list and play the media |
| Catalogue API | Series lists obtainable without scraping protected pages |
| Search | Documented search |
| Episode metadata | Seasons and episodes with stable ids |
| Native MP4/HLS | A direct, DRM-free MP4, MPEG-TS, Matroska or HLS address in the documented data |
| Codecs | H.264 or HEVC video, AAC/MP3/AC-3 audio (what AKENO's hardware pipeline decodes) |
| Authentication | A sign-in or subscription needed for the media |
| DRM | Widevine, PlayReady, FairPlay or SAMPLE-AES (never bypassed; AKENO plays none) |
| Region restrictions | Media limited by country |
| Reliability | Domain stability, uptime, blocking of automated clients |

## 3. Assessment

`Yes`/`No` are what was established; *expected* marks what a service's own
description implies but was not measured from here.

### Licensed services (EverythingMoe `licensed`)

| Service | Docs / permission for third parties | Media | Auth | DRM | Region | AKENO state | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Crunchyroll | No public API | Protected streams | Yes ("requires a paid subscription") | **Yes - measured**: KAT-6005 in the PS5 browser ([CRUNCHYROLL.md](CRUNCHYROLL.md)) | Yes | DRM protected | Website and sign-in work in Websites; episodes do not |
| HIDIVE | No public API | Protected streams | Yes | Expected | Yes | DRM protected | Not measured |
| Netflix | No public API | Protected streams | Yes | Expected | Yes | DRM protected | Not measured |
| Hulu | No public API | Protected streams | Yes | Expected | US only | DRM protected | Not measured |
| OceanVeil | No public API | Protected streams | Yes | Expected | Yes | DRM protected | Not measured |
| RetroCrush | No public API | Streams; partly free with ads | For most | Expected | Yes | Authentication required / DRM | Not measured |
| Bilibili (bilibili.tv) | No public playback API | Streams; free in some regions | For HD | Expected for licensed anime | Yes ("region locked") | Authentication required | Not measured |
| YouTube | IFrame Player API only; terms forbid other access to streams | Official embedded player | No for free videos | Some videos | Yes | Browser only (official embedded player) | Official channels (Muse Asia, Ani-One, ...) post episodes; AKENO's YouTube mode plays them in the official player, not natively |

None of them offers a DRM-free file for third-party players. AKENO STREAM
never signs in to them, never reuses a browser's session and never bypasses
DRM.

### Unlicensed sites (no `licensed` tag)

| Site (rank) | Investigated | Catalogue | Native media | Blockers | AKENO state |
| --- | --- | --- | --- | --- | --- |
| Anikoto (1) | [ANIKOTO.md](ANIKOTO.md) | Scraping only; blocks automated clients | Third-party web players | No API; header forging; challenge solvers | Unsupported (never contacted) |
| AnimePahe (2) | [ANIMEPAHE.md](ANIMEPAHE.md) | Behind DDoS-Guard's challenge | kwik web players; Referer-locked HLS built by obfuscated script | Challenge solving, session cookies, script de-obfuscation, Referer forging | **Browser only** (never contacted; Open in Websites) |
| Miruro (5) | [MIRURO.md](MIRURO.md) | Its public pages' schema.org data | Private Cloudflare-protected obfuscated interface; third-party Referer-locked hosts | Cloudflare bypass, XOR de-obfuscation, Referer forging; DMCA subpoena names the domain | **Browser only** (catalogue without Play buttons) |
| The other 75 | Not individually | - | - | No `licensed` tag; `Self-host` sites host unlicensed encodes, `Scraper` sites show other sites' video | Not integrated; each opens in Websites |

A website that plays in the PS5 browser is not thereby a valid native
provider, and an EverythingMoe listing is not a permission.

### Authorized open sources (not in EverythingMoe)

| Source | Docs / permission | Catalogue, search, episodes | Native media | Auth / DRM / region | Reliability | AKENO state |
| --- | --- | --- | --- | --- | --- | --- |
| **PeerTube** instances, e.g. **video.blender.org** (Blender Foundation) | Documented REST API ([docs.joinpeertube.org](https://docs.joinpeertube.org/api-rest-reference.html)); each video carries its licence - Blender's open movies are Creative Commons (CC BY), published by their rights holder | Channels = series, playlists = seasons, videos = episodes; channel search; captions | HLS (H.264 + AAC) and MP4 named in the API | None for public videos | High (Blender's own instance; HLS console-verified on 12.20 in Discover) | **Native playback available - not hardware-tested** until an episode plays on the console, then **Native playback verified** |
| **Internet Archive** | Documented APIs; public-domain and CC items | Discover rows (curated collections) | MP4 derivatives | None | High | Native in Discover (not an Anime provider) |
| **AKENO catalogue (JSON)** of your own files | [ANIME_PROVIDERS.md](ANIME_PROVIDERS.md) | Series, seasons, episodes, subtitles | MP4/HLS/MKV/TS you have the right to play | None | Your own server or USB | Native |
| **Bundled sample series** | Generated test patterns (not anime) | Two seasons | Local MP4 | None | Offline | Native (proves the player, nothing more) |

## 4. Priority for 1.1.0

1. **Ship one verified native workflow on an authorized source**: Anime ->
   *Add Streaming Provider* -> `video.blender.org` -> a series (channel) ->
   season (playlist) -> episode -> AKENO's player -> resume / next episode.
   Exercised over the real network by the *Real-network acceptance* workflow
   (host, software decoder) and by hardware checklist section 15 on the
   console.
2. Keep the honest labels for the rest: Browser only (AnimePahe, Miruro),
   DRM protected (Crunchyroll), Unsupported (Anikoto).
3. Do not integrate further EverythingMoe entries: none offers documented,
   permitted, DRM-free media.

## 5. Capability states in AKENO STREAM

Every provider shows exactly one of these (Add Streaming Provider, its card,
its home, its series, the diagnostics report). Play buttons belong to
episodes: only an episode that names a DRM-free video, checked as playable or
checked when it is played, has one - so only the native states show them.

| State | Meaning | Play buttons |
| --- | --- | --- |
| **Native playback verified** | An episode played in AKENO's own player on this console: about a second of pictures and half a second of sound, no decoder or audio errors. Recorded by the app from the player's own measurements (`anime-native-playback.json`); a check of a site never sets it. | Yes |
| **Native playback available - not hardware-tested** | Its checked episodes are DRM-free files AKENO's player supports; none has played on this console yet. (*Native playback for some episodes* when others are protected or missing.) | Yes |
| **Catalogue only** | Series and details; no episode names a video AKENO may play. | No |
| **Browser only (PS5 browser)** | Its episodes play only on its own website, in the PS5 browser (AnimePahe, Miruro). | No |
| **Authentication required** | Its checked episodes need a sign-in. | No |
| **DRM protected** | Its checked episodes are DRM-protected. | No |
| **Unsupported** | No adapter reads it, no permitted way in, or another region only. | No |
| **Temporarily unavailable** | The site or its videos did not answer the last check (network, HTTP 5xx or 429). | No |
| *Not checked yet* | Before the first check; each episode's video is checked when it is played. | For episodes that name a video |
