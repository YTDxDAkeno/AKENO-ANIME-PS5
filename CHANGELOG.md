# Changelog

AKENO STREAM for PS5 (`PPSA99276`). Versions are `MAJOR.MINOR.PATCH`; release
candidates are tagged `vX.Y.Z-rc.N` and published as GitHub pre-releases.

## 1.0.0 (release candidate 1: `v1.0.0-rc.1`)

`contentVersion` 01.100.000. Host-tested and built for the PS5; awaiting the
console run of the [hardware checklist](docs/HARDWARE_ACCEPTANCE.md)
(section 11) before the stable `v1.0.0`.

### New

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
- See the README's *Limitations* for the rest (DASH in the native player,
  subtitles, HDR).

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
