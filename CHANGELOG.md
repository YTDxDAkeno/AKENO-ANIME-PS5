# Changelog

AKENO STREAM for PS5 (`PPSA99276`). Versions are `MAJOR.MINOR.PATCH`; release
candidates are tagged `vX.Y.Z-rc.N` and published as GitHub pre-releases.

## 1.1.0 (release candidate 1: `v1.1.0-rc.1`)

`contentVersion` 01.110.000. Host-tested (fixture website, controller-driven
end to end, rollback against 1.0.0's own storage code) and built for the PS5;
awaiting the console run of the [hardware checklist](docs/HARDWARE_ACCEPTANCE.md)
(section 13) before the stable `v1.1.0`.

### New

- **Website providers** ([docs/WEBSITE_PROVIDERS.md](docs/WEBSITE_PROVIDERS.md)):
  *Add Streaming Provider* takes a website's address and, when an adapter
  recognises the site, lists its catalogue, series, seasons and episodes in
  AKENO STREAM and plays DRM-free episodes in the native player - no JSON
  catalogue needed. Two adapters:
  - **PeerTube instance**: recognised by `/api/v1/config` (instance name and
    server version); its own channels are the catalogue, channel search on
    the instance, playlists as seasons, HLS/MP4 playback with captions.
  - **Structured data (schema.org)**: JSON-LD `ItemList`, `TVSeries`,
    `TVSeason`, `TVEpisode`, `Movie` and `VideoObject`; `SearchAction` for
    search, `rel="next"` for more pages. Only a `contentUrl` is played -
    never an `embedUrl` (web player) or a film's trailer; subscriber-only
    episodes (`isAccessibleForFree: false`, `requiresSubscription`) are never
    requested; `regionsAllowed` is honoured.
  A site that only answers HTTP 200, or that no adapter recognises, is not a
  provider and stays in Websites. Catalogue pages are read from the
  provider's own site only; every request is bounded.
- **New Add Streaming Provider flow**: address -> automatic detection ->
  *Recognised: <site>* with the adapter, capabilities, checked episodes,
  evidence and the first series' posters -> *Add Provider* -> the provider's
  home. Steps 1-4 show where the flow is. An empty address field shows *e.g.
  https://video.blender.org* with an EXAMPLE tag instead of looking filled
  in.
- **Provider home** for every provider: catalogue in rows, *Available to
  Watch*, search (on the site, or among the listed titles), *Load More*,
  *Check Again*, *Open Website*, *Turn Off/On*, *Details*, *Remove*. Reached
  from *Streaming Providers* (Cross), from the provider's card in *Anime ->
  Your Anime Sources*, and right after adding.
- A website series loads its seasons and episodes when it is opened (and
  checks a few); *Try Again* when the site cannot be read.

### Changed

- An address that is already a provider (typed differently, or redirecting
  to it) opens that provider's home instead of the notice "already a
  provider"; nothing is added twice.
- *Streaming Providers*: Cross opens the provider's home, OPTIONS shows its
  report; website providers show their adapter; entries kept but not loaded
  are mentioned.
- *Websites -> Check for native streaming* also recognises website providers
  and offers *Add to Anime*.

### Storage and rollback

- Website providers are stored in `anime-website-providers.json`, never in
  `anime-providers.json`. 1.0.0 maps unknown provider types in
  `anime-providers.json` to "catalog" and writes them back; keeping website
  providers apart means rolling back to 1.0.0 and upgrading again leaves
  them unchanged, and adding one leaves `anime-providers.json` byte for byte
  as it was. Website ids start with `web_`, which 1.0.0 never creates.
- Both provider files are read defensively: entries this version cannot use
  (unknown type or adapter, newer adapter format, missing fields, duplicate
  ids) are kept with all their fields and reported, never reinterpreted;
  unknown fields survive saves; a damaged file is kept as `.corrupt` and the
  previous good copy (`.bak`, kept at every save) is restored; a file in a
  newer format is never written.
- The data backup includes `anime-website-providers.json`.

### Fixed

- The screenshot tour (and every new test) starts from an empty data folder:
  a provider kept from an earlier run made the tour stop with "already a
  provider".
- CI without a dependency cache: the pinned zlib archive is fetched from
  zlib's GitHub release when zlib.net serves different bytes (same SHA-256).

### Not included

- No adapter for Aniworld, AnikotoTV, Crunchyroll or other sites without a
  readable catalogue; they stay in Websites. Crunchyroll's DRM-protected
  playback remains unsupported ([docs/CRUNCHYROLL.md](docs/CRUNCHYROLL.md)).

## 1.0.0 (release candidate 1: `v1.0.0-rc.1`)

`contentVersion` 01.100.000. Host-tested and built for the PS5; awaiting the
console run of the [hardware checklist](docs/HARDWARE_ACCEPTANCE.md)
(sections 11 and 12) before the stable `v1.0.0`.

### New

- **AKENO STREAM branding**: a new home-screen icon (`icon0.png`), selection
  background (`pic0.dds`) and launch background (`pic1.dds`, both 3840x2160
  BC7) from the AKENO STREAM artwork, replacing the orange prototype icon
  and the template's bear backgrounds. The app's header shows the AKENO "A"
  mark and the startup screen the AKENO STREAM artwork. CI fails if the
  packaged name is not "AKENO STREAM" or the prototype icon comes back.
- **Anime episode system**: series, seasons and episodes from streaming
  providers, separate from AniList's metadata. Availability per episode
  (playable, not checked, DRM, sign-in, region, missing, network, format,
  catalogue only) with the reason; an episode is offered as playable only
  after its first bytes were checked. Anime home: Continue Watching,
  Available to Watch, Recently Updated, My Watchlist, Recently Watched,
  Trending, New Episodes, This Season, Most Popular, genre rows and Movies.
  Series page with season selector, episode list (thumbnail, progress,
  runtime, audio and subtitle languages), Play/Continue and watchlist.
- **Streaming providers**: AKENO catalogues (JSON, documented with a schema
  and an example in [docs/ANIME_PROVIDERS.md](docs/ANIME_PROVIDERS.md)) and
  PeerTube channels or playlists. *Add Streaming Provider* tests an address
  and shows what it is, its format, counts, what it offers (seasons,
  subtitles, audio languages, artwork, AniList links, checked playback), the
  checked episodes, every problem by path and a preview of its series, then
  imports it. *Streaming Providers* lists, explains, removes and re-checks
  them. Your own library: `anime-catalog.json` in the install folder.
- **Anime search**: English, romaji and alternative titles, partial matches,
  suggestions, search history, year/genre/status/availability filters.
- **Player**: WebVTT and SubRip subtitles (Triangle), previous/next episode
  (L2/R2), *Up next* with a countdown across seasons, progress per
  provider-qualified episode.
- A bundled **synthetic test series** (generated test patterns and tones,
  1.5 MB, labelled as test material) plays the whole episode path offline.
- **Find public videos on a page (native)** (Websites): plays videos a page
  publishes openly in its HTML (`<video>`, `<source>`, video meta tags,
  direct links) in AKENO's player. No scripts, cookies, players or DRM; not
  a universal extractor ([docs/NATIVE_PUBLIC_VIDEO.md](docs/NATIVE_PUBLIC_VIDEO.md)).
- **Data backup**: settings, history, favorites, sources, websites, anime
  providers and watchlist are copied to `/data/akeno-backup/` at every start;
  an empty data folder (the title registered again) is restored from it
  automatically; *Settings -> Back up my data / Restore my data*. The YouTube
  key is not backed up.
- [docs/INSTALL.md](docs/INSTALL.md): install, update, rollback, duplicate
  tiles, and why the home screen can keep the old name and icon
  (ShadowMountPlus keeps the first registration) with the steps to refresh
  it. [docs/CRUNCHYROLL.md](docs/CRUNCHYROLL.md): what a native Crunchyroll
  integration would need and why protected playback is not possible.

- **Home**, redesigned: *Continue Watching*, *YouTube*, *Anime*, *My
  Websites*, *Discover*, *Local Library*, *Recently Added Websites* and
  *Favorites*. Empty rows are hidden. The YouTube (trending, with your API
  key), Anime (AniList trending) and Discover (PeerTube and Internet Archive)
  rows fill in after Home appears; without a network Home opens at once and
  tries again later. *Local Library* lists files played before, video files in
  the install folder's `media` folder and the bundled clips.
- **Websites as app modes**: *Pin as Mode* gives a saved website its own tab
  after Websites (at most four) with a tab name, the site's icon or a letter
  tile, a colour and a start page (the saved address or the homepage). The
  last mode, a website mode included, is restored at the next start. Site
  names and colours are taken from the site (`og:site_name`,
  `application-name`, `theme-color`) when you left them at their defaults.
- **Playback Lab** (Websites): measures why a website's video plays or not -
  MP4, fragmented MP4 through MediaSource, **a Clear Key-encrypted clip
  through EME** (tells "no commercial DRM system" apart from "no decryption at
  all"), native HLS, video in a frame from another origin, cookies in frames,
  DRM key systems with key creation, full screen, audio and codec support;
  the secure DRM check on Shaka Player's HTTPS support page; public MP4, HLS,
  HLS.js and dash.js tests; *Play a Video Link* in AKENO's own player.
- **Per-site playback records**: what you saw and the error code (for example
  Crunchyroll's KAT-6005) are classified with the lab's measurements into
  works / page failed / player failed to initialize / no compatible resource /
  codec unsupported / media API unsupported / DRM unavailable / embedding
  denied, or *not determined* when the evidence does not decide.
- *Clear Browser Data* asks the system browser to forget its cookies (looked
  up at run time; firmwares without the function only lose this option).
- Release workflow: tags `vX.Y.Z` / `vX.Y.Z-rc.N` matching `version.hpp`
  publish `AKENO-STREAM-PS5-<tag>.zip` with `SHA256SUMS` and these notes.

### Fixed

- YouTube channel uploads: the parsed list was used after it was freed
  (found by the AddressSanitizer run of the host tests).

### Changed

- Mode order: Home, YouTube, Anime, Websites, your website modes, Discover,
  Library, Sources, Settings.
- YouTube videos opened from their details page are kept in watch history
  (without a resume point - the official player keeps its own).
- The browser capability test became the Playback Lab's first step; its
  results are still in Settings -> Diagnostics -> Browser.

### Unchanged on purpose

- Title ID `PPSA99276`, install path `/data/homebrew/PPSA99276/`, the data
  folder and its files: websites, history, favourites and settings carry
  over. YouTube and PeerTube playback paths are unchanged.
- No DRM circumvention, no account bypass, no site-specific stream
  extractors.

### Known limitations

- Crunchyroll episodes stop with **KAT-6005** on firmware 12.20 while the
  website and sign-in work. Run the Playback Lab and record the episode: the
  Crunchyroll section names the measured cause. If the console's browser
  offers no DRM system Crunchyroll licenses, protected episodes cannot play
  in it - AKENO cannot add one.
- AnikotoTV's player loads but never starts on firmware 12.20. Record it
  with *Record video playback*; the lab separates the usual causes (player
  refuses the browser, frame embedding refused, streaming feature missing).
- A website mode always starts at its start page: the system browser does not
  tell apps which page was open.
- Anime plays only from providers you add; AniList is metadata only. A
  website's homepage is not a provider.
- The home screen keeps the name and icon of the first registration until
  the title is registered again ([docs/INSTALL.md](docs/INSTALL.md)).
- See the README's *Limitations* for the rest (DASH in the native player,
  embedded subtitle tracks, HDR).

## 0.7.0

Websites mode and the PS5 browser inside AKENO STREAM
(`libSceWebBrowserDialog`), YouTube's official embedded player, the
Crunchyroll website with its own sign-in, and the browser capability test.

## 0.6.0

Discover (PeerTube, Internet Archive), adding sources from a phone, YouTube
app hand-off (experimental).

## 0.5.0

Sources mode (M3U lists, JSON feeds, stream addresses), fragmented-MP4 HLS,
separate audio renditions, AES-128 HLS, byte ranges, web files.

## 0.4.x

Native media app with hardware H.264/HEVC decoding and AAC audio (0.4.0);
own heap, crash reporting and startup progress (0.4.1); user files in the
install folder, recorded console results (0.4.2); keyboard crash fix and
reliable key files (0.4.3).
