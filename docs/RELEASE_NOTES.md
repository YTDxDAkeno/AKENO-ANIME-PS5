# AKENO STREAM for PS5 1.0.0

Native, controller-operated media app for jailbroken PS5 consoles (title ID
`PPSA99276`, ShadowMountPlus). **Release candidates (`-rc.N`) are for
hardware testing**; the stable 1.0.0 follows once the critical checks pass on
a console.

## What's new in 1.0.0

- **AKENO STREAM look**: new home-screen icon, selection and launch
  backgrounds, the AKENO mark in the app's header and the artwork on the
  startup screen - the orange prototype icon and the template's bear
  backgrounds are gone.
- **Anime with real episodes**: add a streaming provider (an AKENO catalogue
  or a PeerTube channel/playlist), browse series, seasons and episodes, and
  play them in AKENO's own player with subtitles, previous/next episode,
  *Up next* and resume. Only episodes that were checked as DRM-free video are
  offered as playable; AniList stays the catalogue. A synthetic test series
  ships with the app so the episode path works offline.
- **Anime search** with English, romaji and alternative titles, suggestions,
  history and filters.
- **Home** redesigned; **websites as their own modes**; the **Playback Lab**.
- **Find public videos on a page**: videos a page publishes openly play in
  AKENO's player (no scripts, cookies or DRM involved).
- **Data backup**: your settings, history, favorites, websites, sources and
  anime providers are saved to `/data/akeno-backup/` and restored
  automatically if the app is registered again.

Full list: [CHANGELOG.md](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/blob/v1.0.0-rc.1/CHANGELOG.md).

## Install or update

1. Download the ZIP below and `SHA256SUMS`; check it with
   `sha256sum -c SHA256SUMS` (or compare the SHA-256 shown below).
2. Close AKENO STREAM and **keep a copy** of `/data/homebrew/PPSA99276/`
   (your rollback). Your data lives in the app's data folder and is not
   touched by the update.
3. Unzip: it contains the folder `PPSA99276/`. Copy that whole folder over
   FTP to `/data/homebrew/PPSA99276/`, replacing the old files. Do not upload
   the ZIP itself.
4. Refresh ShadowMountPlus and start **AKENO STREAM**. The notifications read
   "AKENO STREAM 1.0.0 starting" and "... ready".
5. First check: Anime -> *Available to Watch* -> *Signal Lights (AKENO Test
   Series)* -> Play: a test pattern with a tone and English subtitles.

**Home screen still says "Akeno Anime Preview" or shows the old icon?**
ShadowMountPlus keeps the name and artwork of the first registration. Start
1.0.0 once (it backs up your data), close it, then in ShadowMountPlus's web
interface (`http://<PS5>:10101/`) choose **Uninstall** on AKENO STREAM (the
folder is kept - never *Delete source*) and **Scan**. Step by step:
[docs/INSTALL.md](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/blob/v1.0.0-rc.1/docs/INSTALL.md).

Rollback: copy the saved folder back to `/data/homebrew/PPSA99276/`.

## Please test

[Hardware acceptance checklist](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/blob/v1.0.0-rc.1/docs/HARDWARE_ACCEPTANCE.md),
section 12 (the 1.0.0 acceptance run: branding, YouTube, PeerTube, anime
episodes, subtitles, Up next, progress, websites, providers, exit and
restart, diagnostics). Settings -> Diagnostics -> *Export report* collects
the results; attach it with your firmware version.

## Known limitations

- Crunchyroll: website and sign-in work, episodes stop with **KAT-6005**:
  they need a licensed DRM module the PS5 does not give homebrew apps, and
  Crunchyroll offers no API for third-party apps. AKENO never bypasses DRM
  ([details](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/blob/v1.0.0-rc.1/docs/CRUNCHYROLL.md)).
- Anime plays only from providers you add; AniList is metadata only. A
  website's homepage is not a streaming provider.
- AnikotoTV: the player loads but playback does not start on firmware 12.20.
- A website mode always starts at its start page. Inside the system browser
  its own controls apply; avoid the PS button there (L3 + R3 is the
  emergency exit).
- No MPEG-DASH in the native player, no subtitle tracks embedded in video
  files, no HDR tone mapping.
- The new features in this release are tested automatically on the build
  machine and built for the PS5; their console results are pending.
