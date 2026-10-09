# Hardware acceptance checklist (AKENO STREAM 0.4.0)

Run this on the console before calling a build "working". Note the result of
each step (pass / fail / not tested) and, when anything fails, export a
diagnostics report (step 7) and attach it together with the firmware version,
loader version and the build label shown in Settings -> About.

Console: firmware ______ · ShadowMountPlus ______ · build label ______ · date ______

## 1. Install and launch

- [ ] `/data/homebrew/PPSA99276/` contains `eboot.bin`, `sce_sys/`,
      `sce_module/libc.prx` and `assets/` (fonts and `selftest/`)
- [ ] No old `/data/homebrew/PPSA99999/` remains
- [ ] The tile is named **AKENO STREAM** and shows the icon
- [ ] The app starts within ~5 s; a system notification "AKENO STREAM 0.4.0 ready" appears
- [ ] Home shows the AKENO STREAM header, mode tabs and shelves; text is
      smooth (Inter font). Blocky pixel text means the fonts were not found.
- [ ] No "Controller disconnected" chip while the DualSense is on

## 2. Navigation

- [ ] D-pad and left stick move focus; holding repeats
- [ ] L1/R1 cycle Home -> Anime -> YouTube -> Library -> Settings and wrap
- [ ] Cross opens, Circle goes back; the last mode is restored after restart
- [ ] PS button / home menu and returning to the app do not freeze it

## 3. Offline playback (no network)

Disconnect the network (or skip this if not possible) and open
Home -> *Offline Test Clips*.

- [ ] **H.264 + AAC 360p clip**: picture appears within 2 s, sound plays
- [ ] **A/V Sync Test Clip** (12 s): a steady tone with a short, higher
      beep once a second; each beep starts when the seconds counter in the
      picture changes (note any noticeable offset, early or late: ______)
- [ ] Cross pauses and resumes picture and sound together
- [ ] Left/right seek ±10 s; the position bar updates
- [ ] At the end the player shows "Finished"; Cross replays from 0:00
- [ ] Up/down change the volume
- [ ] Circle stops and returns to the details page
- [ ] OPTIONS shows stream information: decoder "PS5 hardware decoder
      (Videodec2)", H.264, frames decoded/presented increasing, 0 audio errors

## 4. Diagnostics

Settings -> *Diagnostics*:

- [ ] System panel shows firmware, data folder `/download0/akeno`, "FreeType (Inter)"
- [ ] *Run media self-test* -> **Passed** (FFmpeg 8.0.1, mpegts h264, frames decoded)
- [ ] *Play hardware test clip* plays the A/V clip
- [ ] *Run network test* (with network) -> **Passed**

## 5. Network playback

- [ ] Home -> *Open Movies & Test Streams* -> **Big Buck Bunny**: plays with
      sound, quality chosen per Settings -> Maximum video quality
- [ ] Square changes the maximum quality; playback continues near the same position
- [ ] L1/R1 seek ±60 s
- [ ] **Akamai live test stream**: plays, shows LIVE, keeps playing > 2 min
- [ ] **Apple BipBop** and **Sintel** streams open (they may play without
      sound if the stream only has separate audio renditions - a notice says so)
- [ ] Unplug the network during playback: the player retries and shows an error after a while instead of freezing
- [ ] Stop at ~1 min, leave, reopen: "Resume" starts at the saved position;
      the item appears under *Continue Watching*

## 6. Library and your own media

- [ ] Library lists *Bundled test clips* and *AKENO media folder*
- [ ] Copy an MP4 (H.264 + AAC) to `/download0/akeno/media/` over FTP; it
      appears after reopening Library and plays with sound
- [ ] An MKV and a TS file play
- [ ] A USB drive: note what Library shows for USB drive 1 (files / "Not
      connected" / "No access from the title sandbox"): ______
- [ ] `streams.json` (README example with a real stream) shows *Your Streams* in Open Streams

## 7. Export a report

- [ ] Diagnostics -> *Export report* names a file
      `/download0/akeno/akeno-diagnostics-….txt`
- [ ] Download it over FTP; it contains no API key (search for `AIza`)

## 8. Catalogues (network)

- [ ] Anime mode loads trending/seasonal/popular/top rows with artwork
- [ ] Triangle search finds "Frieren"; details show description and a
      *Where to Watch* row with QR codes that open on a phone
- [ ] YouTube mode without a key shows the setup guide
- [ ] With your key: trending row loads, search works, a video shows a QR
      code that opens the video on a phone
- [ ] The Crunchyroll card explains why there is no Crunchyroll playback

## 9. Stability

- [ ] Play, stop and reopen videos 10 times in a row without a crash
- [ ] 30 minutes of continuous playback without dropped audio or growing delay
- [ ] Switching modes rapidly while artwork loads does not crash

Result: ______ passed, ______ failed, ______ not tested.
