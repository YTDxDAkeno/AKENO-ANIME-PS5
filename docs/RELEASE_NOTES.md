# AKENO STREAM for PS5 1.1.0 - release candidate 2

Native, controller-operated media app for jailbroken PS5 consoles (title ID
`PPSA99276`, ShadowMountPlus). **This is a release candidate for hardware
testing, not the stable release.** The stable `v1.1.0` follows only when the
critical items of the hardware checklist, section 15, pass on a console with
firmware 12.20.

## What's new in rc.2

- **One native anime workflow, end to end**: Anime -> *Add Streaming
  Provider* -> `video.blender.org` -> a series -> its season -> an episode ->
  **AKENO STREAM's own player** -> pause, seek, subtitles where the episode
  has them, next episode (R2), progress, restart, *Continue*. The source is the
  Blender Foundation's PeerTube instance: Creative Commons open movies,
  DRM-free, published by their rights holder. They are animated films, **not
  anime**; they prove AKENO's provider-to-player path with an authorized
  source.
- **Honest provider states**: *Native playback verified*, *Native playback
  available - not hardware-tested*, *Catalogue only*, *Browser only (PS5
  browser)*, *Authentication required*, *DRM protected*, *Unsupported*,
  *Temporarily unavailable*. **Verified** is set only when one of the
  provider's episodes really played in AKENO's player on your console
  (pictures, sound, no decoder errors), never by a check of a website. Only
  native states have Play buttons.
- **AnimePahe**: plays in the PS5 browser (as you confirmed on 12.20), but it
  cannot be a native provider - its catalogue sits behind DDoS-Guard's browser
  challenge and its videos are kwik web players whose streams answer only
  that player. AKENO never contacts it; *Add Streaming Provider* explains why,
  labels it *Browser only* and offers **Open in Websites**.
- Investigations: [ANIMEPAHE.md](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/blob/v1.1.0-rc.2/docs/ANIMEPAHE.md),
  [PROVIDER_COMPATIBILITY.md](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/blob/v1.1.0-rc.2/docs/PROVIDER_COMPATIBILITY.md)
  (EverythingMoe as a discovery source: 8 of 86 active anime-streaming
  entries are licensed, all DRM, subscription or region-locked services, or
  YouTube), [MIRURO.md](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/blob/v1.1.0-rc.2/docs/MIRURO.md),
  [ANIKOTO.md](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/blob/v1.1.0-rc.2/docs/ANIKOTO.md).
- Also in rc.2 (from the Miruro build): Miruro as a browser-only catalogue,
  Playback Lab *Test a Play button*.

Full list: [CHANGELOG.md](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/blob/v1.1.0-rc.2/CHANGELOG.md).

## What was tested, and where

| Test | Where | Result |
| --- | --- | --- |
| Host unit and end-to-end tests with ASan/UBSan (controller-driven: provider import, catalogue, series, episode, native player, verified state, next episode, resume, restart, AnimePahe never contacted) | Build machine, offline fixtures | Passed (see the CI run of this tag) |
| Real-network acceptance: `video.blender.org` through the controller into AKENO's player (FFmpeg software decoder) | GitHub runner, real internet | See the *Real-network acceptance* run of this commit |
| PS5 cross-build, import check (system modules), package contents | Build machine | Passed |
| **Console** (firmware 12.20): launch, DualSense, native playback with the hardware decoder, restart | **Your PS5** | **Pending - checklist section 15** |

A passing CI build is not a console result.

## Install or update

1. Download `AKENO-STREAM-PS5-v1.1.0-rc.2.zip` and `SHA256SUMS` below; check
   it with `sha256sum -c SHA256SUMS` (or compare with the SHA-256 shown at the
   bottom of this page).
2. Close AKENO STREAM and **keep a copy** of `/data/homebrew/PPSA99276/`
   (your rollback). Your data lives in the app's data folder and is not
   touched by the update.
3. Unzip: it contains the folder `PPSA99276/`. Copy that whole folder over
   FTP to `/data/homebrew/PPSA99276/`, replacing the old files. Do not upload
   the ZIP itself.
4. Refresh ShadowMountPlus and start **AKENO STREAM**: "AKENO STREAM 1.1.0
   starting" and "... ready". Settings shows the build label bottom right.
5. Run section 15 of the
   [hardware checklist](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/blob/v1.1.0-rc.2/docs/HARDWARE_ACCEPTANCE.md)
   and attach the exported diagnostics report (Settings -> Diagnostics ->
   *Export report*).

Rollback: copy the saved folder back. rc.1 and 1.0.0 keep working with your
providers; the new file `anime-native-playback.json` is simply ignored by
them.

Step-by-step guide: [INSTALL.md](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/blob/v1.1.0-rc.2/docs/INSTALL.md).

## Known limitations

- **No anime website plays in AKENO's own player.** AnimePahe and Miruro play
  only in the PS5 browser (their own players); Anikoto is unsupported;
  Crunchyroll episodes are DRM-protected (KAT-6005 in the browser). AKENO
  never solves challenges, forges headers, extracts sessions or bypasses
  DRM. Native playback needs a source that publishes DRM-free files with
  permission: PeerTube instances, the Internet Archive (Discover), or an
  AKENO catalogue of your own files.
- The native workflow's test source is open animated films, not anime.
- The native player's hardware path on firmware 12.20 is verified for HLS
  and PeerTube in Discover (earlier builds); the anime provider path in rc.2
  still needs its console run.
- No MPEG-DASH, VP9 or AV1; no subtitle tracks embedded in video files or HLS
  subtitle playlists (WebVTT/SubRip files and PeerTube captions work); no HDR
  tone mapping.
- USB drive access from the title sandbox is unconfirmed.
- Third-party sources can change or go offline at any time.
