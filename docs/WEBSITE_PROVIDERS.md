# Website providers (new in 1.1.0)

*Anime -> Add Streaming Provider* takes a website's address. When one of AKENO
STREAM's **website adapters** recognises the site, it becomes a provider: its
catalogue, search, series, seasons and episodes appear inside AKENO STREAM, and
episodes with a DRM-free video file play in AKENO's own player. No hand-made
JSON catalogue is needed.

A website becomes a provider only when an adapter recognises **what the site
publishes**. A page that answers HTTP 200, has a title or contains a video
player is not enough. Sites no adapter recognises stay in *Websites*, where
the PS5's browser shows them.

| Adapter | Recognises | Catalogue | Search | Seasons and episodes | Playback |
| --- | --- | --- | --- | --- | --- |
| **PeerTube instance** (`peertube-instance`) | `GET /api/v1/config` answers with the instance's `instance.name` and `serverVersion` (PeerTube's documented REST API) | The instance's own channels (`/api/v1/video-channels`, `isLocal`), 50 per page | `/api/v1/search/video-channels` with `searchTarget=local` and `host` | A channel's public playlists are seasons, their videos episodes (as a PeerTube channel provider) | The video's HLS playlist or MP4 file from `/api/v1/videos/<uuid>`, captions as subtitles |
| **Structured data** (`schema-org`) | schema.org JSON-LD on the page that describes series, films or videos (`ItemList`, `TVSeries`, `TVSeason`, `TVEpisode`, `Movie`, `VideoObject`) | The page's `ItemList` entries and series; `<link rel="next">` for more pages | `WebSite` -> `potentialAction` `SearchAction` (`target` / `urlTemplate`, `query-input`) | `containsSeason` / `episode` on the series' own page; a season that names only its `url` is read from that page | Only a `contentUrl` (of the episode's `video`, `associatedMedia` or `encoding`) |

The adapters live in `src/anime/website_peertube.cpp` and
`src/anime/website_schema_org.cpp`; they are compiled into the app, nothing is
downloaded or run at run time.

## On the console

1. *Anime -> Your Anime Sources -> Add Streaming Provider* (or *Anime ->
   Streaming Providers -> Add Streaming Provider*).
2. **Website address**: the homepage, for example `video.blender.org` (the
   keyboard starts with `https://`). An empty field shows *e.g.
   https://video.blender.org* with an **EXAMPLE** tag: that is a hint, not an
   entered address.
3. Detection runs as soon as the address is entered (or with **Detect
   Provider**). The panel shows:
   - **Recognised: \<site name\>** and the adapter (*Recognised by*), or why
     the site is not a provider;
   - the first series with their posters - what *Add* brings in;
   - *Offers*: catalogue browsing, search on the site, more pages, series
     details, seasons, episode lists, and **Native playback (checked)** only
     when a checked episode played;
   - *Catalogue* (entries on the first page; the seasons and episodes of up
     to three opened series) and *Episodes checked* (playable, DRM, sign-in,
     region, missing, unreachable, unsupported - up to 12 episodes);
   - *Found*: the evidence (`/api/v1/config: PeerTube 7.2.1`, `JSON-LD: 1
     ItemList, 1 WebSite`, `Search: SearchAction ...`).
4. **Add Provider** (enabled when the site is a provider with playable
   episodes) opens the provider's home.

An address that is already one of your providers - typed differently
(`http://`, `www.`, a trailing `/`, the default port) or one that redirects to
it - opens **that provider's home** instead; nothing is added twice. The same
holds for AKENO catalogues and PeerTube channels.

### Provider home

Opened after adding, from *Streaming Providers* (Cross) or from the provider's
card at the end of *Anime -> Your Anime Sources*:

- name, adapter, homepage, status (*N series so far*, how many episodes were
  checked playable) and the capabilities as chips;
- **Search** (Triangle): on the site when the adapter offers search, else
  among the titles listed so far;
- **Load More**: the next catalogue page (also the last card of the
  catalogue);
- **Check Again**, **Open Website** (the site in the browser), **Turn Off /
  Turn On** (kept but not loaded), **Details** (Options: adapter, addresses,
  status, the storage file and what was checked) and **Remove** (Square);
- *Available to Watch* and the **Catalogue** in rows of twelve.

A catalogue entry shows *NOT OPENED* until it is opened: its seasons and
episodes load from the site then (and a few episodes are checked). If the
site cannot be read, the series says why and offers **Try Again**. Episodes
play in AKENO's native player with subtitles, previous/next episode, *Up
next* and resume, like every other provider; progress and the watchlist are
kept per provider.

*Websites -> (a saved site) -> Check for native streaming* runs the same
detection on a saved website and offers *Add to Anime* when it is a provider.

## What is never done

- No address is invented. An episode plays only from a `contentUrl` the site
  publishes (or, for PeerTube, the file the API names). `embedUrl` is a web
  player and is never played; a `Movie`'s `trailer` is never played as the
  film. An episode without a video file is listed as *Episodes indexed* with
  the reason ("offered in a web player only (Websites)", "listed without a
  video file").
- No hidden or obfuscated players are read, no scripts run, no cookies or
  browser sign-ins are used, no access control is bypassed.
- DRM is never bypassed. Before an episode is offered as playable, its first
  bytes are checked (MP4, MPEG-TS, Matroska or an HLS playlist without DRM
  markers - [ANIME_PROVIDERS.md](ANIME_PROVIDERS.md#what-counts-as-playable)).
  Episodes marked `isAccessibleForFree: false` or `requiresSubscription` are
  *Sign-in required* and are not requested at all; `regionsAllowed` is
  honoured.
- Pages of the catalogue (catalogue pages, series, seasons, episodes, search)
  are read from the provider's own site only - its host, with or without
  `www.`, or a subdomain. Video files and artwork may come from other hosts
  (content delivery networks).
- Bounds: pages up to 4 MiB, at most 5 redirects and never from https to
  http, JSON-LD blocks up to 1 MiB (32 per page, 32 levels deep), 200 entries
  per page, 500 series per provider, 60 seasons per series and 3,000
  episodes per season, 24 page loads to open one series.

## Not supported

Any website without a matching adapter. In particular, AKENO STREAM has **no
adapter for Aniworld, AnikotoTV, Crunchyroll** or similar sites, and none is
planned: they publish no catalogue AKENO may read and play their episodes in
their own web players, often with DRM. They stay in *Websites*. Crunchyroll's
protected playback is a separate question ([CRUNCHYROLL.md](CRUNCHYROLL.md)).

The structured-data adapter depends on what a site publishes. Many sites
describe their series with schema.org but name no video file (only a web
player): AKENO then lists the catalogue in the detection panel and explains
that the site stays in *Websites*; it is not added.

## Storage and rolling back to 1.0.0

Website providers are kept in `/download0/akeno/anime-website-providers.json`
(at most 30), **not** in `anime-providers.json`.

1.0.0 reads `anime-providers.json`, treats every provider type it does not
know as an AKENO catalogue and writes that back at its next change. A website
provider stored there would turn into a broken catalogue after a rollback and
stay broken after upgrading again. So:

- 1.1.0 never writes a website provider into `anime-providers.json`; adding,
  turning off or removing one leaves that file byte for byte unchanged.
- 1.0.0 never opens, backs up, restores or deletes
  `anime-website-providers.json`. Roll back, use 1.0.0, upgrade again: your
  website providers are exactly as they were (and catalogues or PeerTube
  channels you added or removed in 1.0.0 are kept too).
- Website provider ids start with `web_`. 1.0.0 makes ids from letters,
  digits and `-` only, so the two files can never hand out the same id.

Both files are read defensively. An entry 1.1.0 cannot use - an unknown
provider type or adapter, a newer adapter format, a missing or malformed
field, a duplicate id - is **kept in the file with all its fields**, reported
under *Settings -> Diagnostics* and on *Streaming Providers*, and never
reinterpreted. Fields 1.1.0 does not know survive its saves. A damaged file
is kept as `<file>.corrupt` (never over a different damaged copy) and the
previous good copy, `<file>.bak`, written at every save, is used instead. A
file written by a newer format is loaded as far as it allows and never
written by 1.1.0. Writes go to `<file>.tmp` and are renamed over the file, so
an interrupted write leaves the previous file in place. The data backup
(`/data/akeno-backup/`) includes the file.

### The file format (version 1)

```json
{
  "format": "akeno-website-providers",
  "version": 1,
  "minReaderVersion": 1,
  "providers": [
    {
      "id": "web_blender-video",
      "name": "Blender Video",
      "url": "https://video.blender.org/",
      "homepage": "https://video.blender.org/",
      "adapter": "peertube-instance",
      "adapterVersion": 1,
      "enabled": true,
      "added": 1760100000,
      "settings": {"host": "video.blender.org"},
      "catalogue": {"series": 12, "episodes": 0, "checked": 1760100000}
    }
  ]
}
```

| Field | Meaning |
| --- | --- |
| `format`, `version` | `akeno-website-providers`, 1. |
| `minReaderVersion` | The oldest format a reader must understand to use the file. A reader older than `version` loads the providers only when it is at least `minReaderVersion`, and never writes the file. |
| `id` | `web_` + letters, digits, `-`, `_`; unique across both provider files. |
| `name` | Shown in AKENO; the site's own name unless you gave one. |
| `url` | The address you entered (normalised). |
| `homepage` | Where it led (after redirects). |
| `adapter`, `adapterVersion` | The adapter that reads the site and its settings format. |
| `enabled` | `false`: kept, not loaded (*Turn Off*). |
| `added` | Unix seconds. |
| `settings` | The adapter's own configuration: `host` (PeerTube); `catalogue` (the catalogue page) and `search` (the search address, `AKENOQUERY` marks the query) for structured data. |
| `catalogue` | Optional: what the last check found (series, opened episodes, Unix seconds). |

## For site owners: being readable by the structured-data adapter

Describe your catalogue with schema.org JSON-LD, as you would for search
engines, and name the video files you publish for playback:

```html
<script type="application/ld+json">
{"@context": "https://schema.org", "@graph": [
  {"@type": "WebSite", "name": "My Open Anime", "url": "https://anime.example/",
   "potentialAction": {"@type": "SearchAction",
     "target": {"@type": "EntryPoint", "urlTemplate": "https://anime.example/search?q={search_term_string}"},
     "query-input": "required name=search_term_string"}},
  {"@type": "ItemList", "itemListElement": [
    {"@type": "ListItem", "position": 1,
     "item": {"@type": "TVSeries", "name": "The Signal", "url": "https://anime.example/the-signal/",
              "image": "https://anime.example/art/signal.jpg"}}]}
]}
</script>
```

On the series page, a `TVSeries` with `containsSeason` (`TVSeason` with
`seasonNumber` and `episode`, or a `url` to the season's page) and, per
`TVEpisode`, `episodeNumber`, `name` and a `video` `VideoObject` with
`contentUrl` (an MP4, MPEG-TS, Matroska file or HLS playlist without DRM),
`encodingFormat`, `duration`, `thumbnailUrl` and optional `caption`
(`MediaObject` with a WebVTT/SubRip `contentUrl` and `inLanguage`). Only
publish media you are allowed to distribute.
[tests/fixtures/website-schema](../tests/fixtures/website-schema) is a
complete example site.

## For developers: adding a website adapter

1. Implement `WebsiteAdapter` (`src/anime/website.hpp`): `detect` must find
   positive evidence on the fetched page (or through a documented API of the
   same site); `catalogue` and `search` return pages of `Series` entries
   (`details_pending` for entries whose seasons load later; `next_page` for
   more); `series` loads one entry's seasons and episodes; `resolve` returns
   an episode's resource or an `Availability` with the reason. Use
   `fetch_site_page` and `same_site` for every page of the catalogue.
2. Give it an id and a `version()` (its settings format). Register it in
   `website_adapters()` (`src/anime/website.cpp`). Older AKENO versions keep
   providers of an adapter they do not have, unchanged.
3. Test it offline: an in-process fixture (`tests/host/fixture_site.hpp`) or
   canned answers through `net::set_test_transport`;
   `tests/unit/test_website_providers.cpp` and
   `tests/unit/test_provider_e2e.cpp` show the pattern.

The rules in [ANIME_PROVIDERS.md](ANIME_PROVIDERS.md#for-developers-adding-a-provider-type)
apply: documented, authorized sources only, bounded parsing, no DRM, no
cookies, no secrets in logs.
