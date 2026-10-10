# AKENO STREAM for PS5 1.0.0

Native, controller-operated media app for jailbroken PS5 consoles (title ID
`PPSA99276`). **Release candidates (`-rc.N`) are for hardware testing**; the
stable 1.0.0 follows once the critical checks pass on a console.

## What's new in 1.0.0

- **Home**: Continue Watching, YouTube, Anime, My Websites, Discover, Local
  Library, Recently Added Websites and Favorites; empty rows are hidden.
- **Websites as app modes**: pin a saved website as its own tab with its own
  name, icon or letter tile, colour and start page; restored after a restart.
- **Playback Lab**: measures why a website's video plays or not - MP4,
  MediaSource, a Clear Key-encrypted clip through EME, native HLS, video in
  other sites' frames, DRM key systems, full screen, audio, codecs - and
  classifies what you recorded on a site (for example Crunchyroll's KAT-6005)
  into one of eight states, or "not determined".
- *Clear Browser Data* (experimental), a release workflow with checksums.

Full list: [CHANGELOG.md](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/blob/release/akeno-stream-1.0.0/CHANGELOG.md).

## Install on the PS5

1. Download the ZIP below and `SHA256SUMS`; check it with
   `sha256sum -c SHA256SUMS` (or compare the SHA-256 shown below).
2. **Keep a copy** of your current `/data/homebrew/PPSA99276/` folder (your
   rollback). Your websites, history and settings are in the app's data
   folder and are not touched by the update.
3. Unzip: it contains the folder `PPSA99276/`. Copy that whole folder over
   FTP to `/data/homebrew/PPSA99276/`, replacing the old files. Do not upload
   the ZIP itself.
4. Refresh your homebrew loader (ShadowMountPlus or equivalent) and start
   **AKENO STREAM**. The notifications read "AKENO STREAM 1.0.0 starting" and
   "... ready".
5. First check: Home -> *Local Library* -> *A/V Sync Test Clip* plays with
   picture and beeps.

Rollback: copy the saved folder back to `/data/homebrew/PPSA99276/`.

## Please test

The [hardware acceptance checklist](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/blob/release/akeno-stream-1.0.0/docs/HARDWARE_ACCEPTANCE.md)
section 11 (Home, website modes, Playback Lab), plus sections 1-3, 10 and 10b
to confirm nothing that worked before broke. Settings -> Diagnostics ->
*Export report* holds every lab result; attach it with your firmware version.

## Known limitations

- Crunchyroll: website and sign-in work on firmware 12.20, episodes stop with
  **KAT-6005**. Protected episodes play only if the console's browser offers a
  DRM system Crunchyroll licenses; the Playback Lab measures this. AKENO never
  bypasses DRM.
- AnikotoTV: the player loads but playback does not start on firmware 12.20;
  record it with *Record video playback* and compare with the lab.
- A website mode always starts at its start page (the browser does not report
  the last page). The system browser's own controls apply inside it; avoid
  the PS button there (L3 + R3 is the emergency exit).
- No MPEG-DASH in the native player, no subtitles, no HDR tone mapping.
