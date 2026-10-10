# AKENO STREAM for PS5 1.1.0

Native, controller-operated media app for jailbroken PS5 consoles (title ID
`PPSA99276`, ShadowMountPlus). **Release candidates (`-rc.N`) are for
hardware testing**; the stable 1.1.0 follows once the critical checks of
section 13 pass on a console.

## What's new in 1.1.0

- **Website providers**: *Anime -> Add Streaming Provider* now takes a
  website's address. When an adapter recognises the site, its catalogue,
  search, series, seasons and episodes appear inside AKENO STREAM and
  DRM-free episodes play in AKENO's own player - no JSON catalogue needed.
  Supported: **PeerTube instances** (for example `video.blender.org`, through
  PeerTube's documented API) and websites that describe their catalogue with
  **schema.org structured data** and name their video files. Sites no adapter
  recognises stay in Websites.
- **A clearer Add Streaming Provider**: enter the address, detection runs by
  itself and shows what was recognised, what the provider offers, which
  checked episodes play, and posters of its first series; *Add Provider*
  opens the provider's home. An address you already added opens its home
  instead of a notice.
- **Provider home**: catalogue, search, *Load More*, *Check Again*, *Open
  Website*, *Turn Off*, *Details* and *Remove* for every provider.
- **Safe to roll back**: website providers are kept in their own file,
  `anime-website-providers.json`, which 1.0.0 never touches. Going back to
  1.0.0 and upgrading again keeps every provider exactly as it was.

Full list: [CHANGELOG.md](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/blob/v1.1.0-rc.1/CHANGELOG.md).
How website providers work, and what they never do:
[docs/WEBSITE_PROVIDERS.md](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/blob/v1.1.0-rc.1/docs/WEBSITE_PROVIDERS.md).

## Install or update

1. Download the ZIP below and `SHA256SUMS`; check it with
   `sha256sum -c SHA256SUMS` (or compare the SHA-256 shown below).
2. Close AKENO STREAM and **keep a copy** of `/data/homebrew/PPSA99276/`
   (your rollback, and needed for the rollback test). Your data lives in the
   app's data folder and is not touched by the update.
3. Unzip: it contains the folder `PPSA99276/`. Copy that whole folder over
   FTP to `/data/homebrew/PPSA99276/`, replacing the old files. Do not upload
   the ZIP itself.
4. Refresh ShadowMountPlus and start **AKENO STREAM**. The notifications read
   "AKENO STREAM 1.1.0 starting" and "... ready".
5. First check: Anime -> *Add Streaming Provider* -> type
   `video.blender.org` -> *Recognised* -> *Add Provider* -> open a channel ->
   play an episode.

Rollback: copy the saved folder back to `/data/homebrew/PPSA99276/`. 1.0.0
keeps working with your catalogues and PeerTube channels; your website
providers wait in their own file until you upgrade again.

## Please test

[Hardware acceptance checklist](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/blob/v1.1.0-rc.1/docs/HARDWARE_ACCEPTANCE.md),
section 13 (launch, add a website provider, browse, search, episodes, native
playback, restart, rollback to 1.0.0 and upgrade again). Settings ->
Diagnostics -> *Export report* collects the results; attach it with your
firmware version.

## Known limitations

- Only websites an adapter recognises become providers: PeerTube instances
  and sites with schema.org structured data that name their video files.
  **Aniworld, AnikotoTV, Crunchyroll** and similar sites have no adapter and
  stay in Websites. Structured data alone does not make a site playable: a
  site that only offers a web player is not added.
- Crunchyroll: website and sign-in work, episodes stop with **KAT-6005**
  (DRM); AKENO never bypasses DRM
  ([details](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/blob/v1.1.0-rc.1/docs/CRUNCHYROLL.md)).
- AnikotoTV: the player loads but playback does not start on firmware 12.20.
- No MPEG-DASH in the native player, no subtitle tracks embedded in video
  files, no HDR tone mapping.
- The new features in this release are tested automatically on the build
  machine (an offline fixture website and recorded PeerTube answers) and
  built for the PS5; their console results are pending.
