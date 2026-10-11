# Changelog

AKENO STREAM for PS5 (`PPSA99276`). Versions are `MAJOR.MINOR.PATCH`; release
candidates are tagged `vX.Y.Z-rc.N` and published as GitHub pre-releases.

## 1.1.0 (release candidate 2: `v1.1.0-rc.2`)

`contentVersion` 01.110.000 (unchanged from rc.1: same version, next
candidate). Host-tested (ASan/UBSan), real-network tested on a GitHub runner
against an authorized PeerTube instance (software decoder), and built for the
PS5; awaiting the console run of the
[hardware checklist](docs/HARDWARE_ACCEPTANCE.md) section 15 before the
stable `v1.1.0`.

### Provider capability states

- Every provider now shows one of eight states, in *Add Streaming Provider*,
  on its card, its home, its series and in the diagnostics report:
  **Native playback verified**, **Native playback available - not
  hardware-tested** (*for some episodes* when others are protected or
  missing), **Catalogue only**, **Browser only (PS5 browser)**,
  **Authentication required**, **DRM protected**, **Unsupported**,
  **Temporarily unavailable** - plus *Not checked yet* before the first
  check ([docs/PROVIDER_COMPATIBILITY.md](docs/PROVIDER_COMPATIBILITY.md)).
  Sign-in and DRM are told apart; a provider that did not answer its last
  load is *Temporarily unavailable* (its last full check is kept). Play
  buttons appear only for episodes of native states.
- **Verified means played here.** A check of a site never verifies a
  provider. The app records *Native playback verified* when one of the
  provider's episodes plays in AKENO's own player with about a second of
  pictures and half a second of sound and no decoder or audio errors, and
  keeps what the player measured (decoder, codecs, size, counts) in
  `anime-native-playback.json` (data backup included; removed with the
  provider). The first time, a message says so; the provider's *Details* and
  the exported report show the record.
- Saved ids are unchanged (`native`, `partial`, `catalogue-only`,
  `browser-only`, `protected`, `unsupported`, `unknown`); `auth-required` is
  new and rc.1 keeps it as stored. *Temporarily unavailable* and *verified*
  are never saved as a check's result (an outage keeps the last full check).

### AnimePahe investigated: browser only, never contacted

- [docs/ANIMEPAHE.md](docs/ANIMEPAHE.md): why AnimePahe plays in the PS5
  browser (a full WebKit browser runs DDoS-Guard's challenge, AnimePahe's and
  kwik's scripts and requests the stream from kwik's own page) and why that
  does not make it a native provider: catalogue and episodes behind
  DDoS-Guard's browser challenge, kwik.cx web players whose HLS address is
  built by an obfuscated script and answers only kwik's `Referer`, no API,
  terms or licence. No challenge solving, session extraction, script
  de-obfuscation or header forging; no native playback built or claimed.
- *Add Streaming Provider* recognises `animepahe.pw`, `.com`, `.org`, `.ru`
  and `.si` **without a request**, explains why, labels it *Browser only* and
  adds nothing; the button next to *Detect Provider* reads **Open in
  Websites** and opens it in the PS5 browser directly (no pre-check request
  from AKENO). Look-alike domains are not treated as AnimePahe.

### EverythingMoe: provider compatibility assessment

- [docs/PROVIDER_COMPATIBILITY.md](docs/PROVIDER_COMPATIBILITY.md): the
  directory as a discovery source (June 2026 snapshot: 8 of 86 active
  anime-streaming entries carry its `licensed` tag - all subscription, DRM or
  region-locked services, or YouTube), the criteria (documentation,
  permission, catalogue API, search, episodes, native MP4/HLS, codecs,
  sign-in, DRM, region, reliability) and the authorized open sources AKENO
  plays natively (PeerTube, Internet Archive, your own catalogues). No
  EverythingMoe entry was integrated.

### Real-network acceptance

- `tests/unit/test_real_network.cpp` and the *Real-network acceptance*
  workflow (`.github/workflows/network-acceptance.yml`): the real App,
  through the controller, from an empty data folder, over the internet:
  *Add Streaming Provider* with `video.blender.org` (the Blender
  Foundation's PeerTube instance; Creative Commons open movies - animated
  films, not anime), its catalogue, a series, its season, the first episode
  in AKENO's native player (FFmpeg's software decoder standing in for the
  hardware decoder), the provider verified, subtitles, pause and resume,
  seek both ways, the next episode (R2), progress saved, restart, resume from
  the series' *Continue*. A Markdown report and screenshots are uploaded. It
  is skipped in the normal test run and never gates a release: a third-party
  outage says nothing about the build.
- `Screen::focus_id()` and `App::latest_toast()` let the tests read the
  focused card and the newest message.

### Hardware checklist

- Section 15: the rc.2 acceptance run on firmware 12.20 (launch, DualSense,
  provider import, catalogue, episode list, native playback over the real
  network with the hardware decoder, picture and audio, pause/resume, seek,
  subtitles, next episode, restart and resume, no crashes, existing PeerTube
  and JSON providers, AnimePahe in the browser, report). The tester's Miruro
  and AnimePahe browser results are recorded under *Results so far*.

### Miruro investigated: a website-only catalogue; the Play button diagnosis

- [docs/MIRURO.md](docs/MIRURO.md): what the console results with Miruro's
  Autoplay setting show (the browser plays Miruro's video once started:
  decoding, sound, seeking and the streaming player work; only the start
  differs), four hypotheses for why Autoplay off does not start, Miruro's
  architecture from two unofficial API projects (AniList for metadata; a
  private, Cloudflare-protected, obfuscated interface for episodes and
  streams; third-party hosts that check the Referer, through an encoded
  proxy), and why native playback is not possible or permitted. No Miruro
  episode plays in AKENO STREAM's player, and none is claimed.
- **Playback labels** for every provider: *Plays in AKENO*, *Partly plays in
  AKENO*, *Catalogue only*, *Website only (PS5 browser)*, *Protected (DRM or
  sign-in)*, *Not supported*, *Not checked yet* - in *Add Streaming
  Provider*, on provider cards, provider homes and series. Only playable
  providers have Play buttons; series of the others have no Play action or
  "Play" hint.
- **Add as Catalogue**: a site whose series can be read but whose episodes
  name no video file AKENO STREAM may play can now be added as a catalogue
  (series and details, no Play buttons) instead of being refused. The label
  is saved as `catalogue.playback` in `anime-website-providers.json`
  (rc.1 keeps it; an unknown value is kept as it is).
- **Miruro** (`miruro.to`, `.tv`, `.bz`, `.ru`, `miruro.com`; not look-alike
  domains) is recognised: its published schema.org catalogue is added as
  *Website only*, with the precise reasons, and its series offer **Open on
  Website** (the series' own page in the PS5 browser). Opening a Miruro page
  shows "Watch it ... with Autoplay on in Miruro's player". Only its public
  pages are ever requested.
- *Check Again* on a website provider runs the whole check again and updates
  its label.
- **Playback Lab: Test a Play button** - the events one controller press
  sends to a page button, and whether the browser starts a video with sound
  from that press at once, after loading it first, 1.5 s later and inside
  another origin's frame; plus `<video autoplay>` with sound. A new answer
  when recording a site's video, "It starts only with the site's own
  Autoplay on", gives the state *Starts only by itself (Autoplay)* with the
  measured or likely cause. Settings -> Diagnostics -> Browser and the
  exported report add a "Starting playback" summary and each site's record
  (host names only).

### Anikoto investigated: not a permitted provider

- [docs/ANIKOTO.md](docs/ANIKOTO.md): what three Anikoto projects
  (zainaqdas/anikoto-api, Shineii86/AniKotoAPI, hexxt-git/anime-sdk) do for
  catalogue, search, details, seasons, episodes and playback, and why AKENO
  STREAM cannot use any of it: no documented or authorized API, scraping of
  pages and private endpoints the site blocks for automated clients (one
  project recommends a Cloudflare challenge solver), and streams obtainable
  only by forging `Referer`/`Origin` headers against third-party players. No
  authorized DRM-free media exists, so no playback proof of concept was built
  and no Play button exists.
- *Add Streaming Provider* and *Websites -> Check for native streaming*
  recognise Anikoto's domains (`anikototv.to`, `anikoto.*`, mirrors) and give
  that reason at once, **without a request to the site**; adding it is
  refused; a provider entry written for it by hand is kept but never
  contacted. Anikoto still opens in *Websites* as before. All other providers
  are unchanged.

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
