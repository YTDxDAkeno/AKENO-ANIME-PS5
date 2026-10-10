# Hardware acceptance checklist (AKENO STREAM 0.7.x)

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
- [ ] Notifications "AKENO STREAM 0.7.0 starting" and, within ~5 s,
      "AKENO STREAM 0.7.0 ready" appear; the splash screen shows the startup
      steps in between
- [ ] **0.7.0 imports two more system modules (the browser dialog). If the
      app no longer starts at all (an error code like CE-108255-1 and no
      "starting" notification), note it: it means this firmware lacks one of
      them. Go back to 0.6.0 and report the firmware version.**
- [ ] If it crashes instead: note the "crashed: ..." notification text
      (signal, `eboot+0x…`, stage) and the last splash message
- [ ] Home shows the AKENO STREAM header, mode tabs and shelves; text is
      smooth (Inter font). Blocky pixel text means the fonts were not found.
- [ ] No "Controller disconnected" chip while the DualSense is on

## 2. Navigation

- [ ] D-pad and left stick move focus; holding repeats
- [ ] L1/R1 cycle Home -> Anime -> YouTube -> Websites -> Discover -> Library ->
      Sources -> Settings and wrap
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
- [ ] **Apple BipBop** and **Sintel** streams open and play with sound
- [ ] **Tears of Steel** (fragmented MP4 / CMAF, refused by 0.4.x) plays
      with sound; OPTIONS shows container "HLS (fragmented MP4)"
- [ ] Unplug the network during playback: the player retries and shows an error after a while instead of freezing
- [ ] Stop at ~1 min, leave, reopen: "Resume" starts at the saved position;
      the item appears under *Continue Watching*

## 6. Library and your own media

- [ ] Library lists *Bundled test clips* and *Media in the install folder*
- [ ] Copy an MP4 (H.264 + AAC) over FTP to `/data/homebrew/PPSA99276/media/`;
      it appears under *Media in the install folder* and plays with sound
- [ ] An MKV and a TS file play
- [ ] A USB drive: note what Library shows for USB drive 1 (files / "Not
      connected" / "No access from the title sandbox"): ______
- [ ] `/data/homebrew/PPSA99276/streams.json` (README example with a real
      stream) shows *Your Streams* in Open Streams

## 6b. Sources (new in 0.5.0)

Use only lists and streams you are allowed to watch; public test streams
(for example the Big Buck Bunny address from Open Streams) are enough.

- [ ] Sources -> *Add a Source*: the responsibility note appears once;
      typing an address with `:` `/` `?` `=` works (R1 = symbols); the end of
      a long address stays visible while typing
- [ ] The new source appears under *Your Sources*; Cross opens it; an M3U
      list shows its groups as rows and its logos
- [ ] Triangle inside a source finds an entry by name
- [ ] An entry plays with sound; Circle returns to the source
- [ ] `sources.txt` copied to `/data/homebrew/PPSA99276/` (lines like
      `Name = https://…`) shows its sources with the badge **PC**
- [ ] Square on a source added on the console asks, then removes it; it stays
      removed after a restart
- [ ] *Play an Address* with an MP4 file on a web server plays; L1/R1 and
      left/right seek within it
- [ ] A web page address (e.g. `https://example.com/`) gives a clear message
      instead of a crash
- [ ] *Add from Phone* (new in 0.6.0): a QR code and an address like
      `http://192.168.x.x:8090/abcd2345` appear; the phone opens the page,
      an added address appears in Sources; a wrong code in the address gives
      "Not found"; after closing the screen the page no longer loads

## 6c. Discover (new in 0.6.0)

- [ ] Discover loads PeerTube rows (Latest, Films, Art & Animation, ...,
      Blender Studio) and Internet Archive rows (Feature Films, Classic
      Cartoons, ...) with artwork; note rows that stay missing: ______
- [ ] A PeerTube video (e.g. a Blender Studio open movie): details show
      *Play*; it plays with sound; seeking works
- [ ] An Internet Archive feature film plays with sound; seeking works
- [ ] Triangle in Discover finds results from both

## 8b. YouTube app hand-off (experimental, secondary since 0.7.0)

- [ ] YouTube mode -> *YouTube App* card: does the official YouTube app start
      (or note the message shown: ______)? 0.6.0's lookup of this system
      function is expected to fail in a homebrew title; a failure is fine.

## 7. Export a report

- [ ] Diagnostics -> *Export report* names a file
      `/download0/akeno/akeno-diagnostics-….txt`
- [ ] With the app still running, download it over FTP from
      `/mnt/sandbox/PPSA99276_000/download0/akeno/`; it contains no API key
      (search for `AIza`)

## 8. Catalogues (network)

- [ ] Anime mode loads trending/seasonal/popular/top rows with artwork
- [ ] Triangle search finds "Frieren"; details show description and a
      *Where to Watch* row with QR codes that open on a phone
- [ ] YouTube mode without a key shows the setup guide
- [ ] With your key: trending row loads, search works, a video shows a QR
      code that opens the video on a phone
- [ ] The Crunchyroll card explains why there is no Crunchyroll playback

## 10. Websites and the embedded browser (new in 0.7.0)

Record each item separately (pass / fail / not tested). Rendering a page is
not the same as playing a video, and signing in is not the same as playing.
After this section, export a report (step 7): it contains every result
AKENO recorded (Settings -> Diagnostics -> Browser shows them too).

**Main acceptance test**

1. [ ] Open AKENO STREAM, press R1 until **Websites**
2. [ ] *Add Website*: type an address with the on-screen keyboard (R1 =
       symbols for `:/.`), then a name. It appears under *Your Websites* with
       its icon (or a coloured letter tile): ______
3. [ ] Cross on it: the screen says "Opening the browser...", then the
       **PS5 browser appears over AKENO STREAM** with the page
       - if instead "The browser could not open" appears: note the code shown
         and the lines under Settings -> Diagnostics -> Browser -> Engine: ______
4. [ ] Navigate with the DualSense: note what moves the cursor, scrolls,
       selects, goes back, and what closes the browser: ______
5. [ ] Select a video on the site
6. [ ] Press Play: picture ______ sound ______ (the site's own player)
7. [ ] Close the browser **with its own controls** (not the PS button):
       AKENO STREAM is back, no crash; a toast says "Back from <site>"
8. [ ] Square on the website -> *Record what works*: mark page / sign-in /
       video / sound as you saw them

**Browser details**

- [ ] Triangle in Websites: typing `example.com` opens the page; typing
      words opens a search (DuckDuckGo)
- [ ] A text field on a page opens the system keyboard; typing works
- [ ] The browser's back / forward / reload work: ______
- [ ] A video's full-screen button works: ______
- [ ] Do L3 + R3 together close the browser? (emergency exit; fine if not): ______
- [ ] Website check: `https://no-such-site.invalid/` shows "The site's name
      could not be found (DNS)" before opening; Cross opens it anyway
- [ ] Square -> Show on Home: the site appears on Home under *Your Websites*;
      Private: on -> it leaves Home and Recently Visited
- [ ] Rename, Change address, Remove work and survive an app restart
- [ ] `websites.txt` in `/data/homebrew/PPSA99276/` (lines `Name = https://…`):
      the sites appear after a restart, once
- [ ] Copy the new build over the old one: your websites are still there

**Browser test** (Websites -> *Browser Test*). Wait for "Done", press
*Back to AKENO*, then note from Settings -> Diagnostics -> Browser:

- [ ] Secure context: ______ ; user agent: ______
- [ ] H.264 ___ HEVC ___ VP9 ___ AV1 ___ AAC ___ Opus ___ native HLS ___
- [ ] MediaSource ___ ManagedMediaSource ___
- [ ] EME ___ Widevine ___ PlayReady ___ FairPlay ___ Clear Key ___
- [ ] HTML5 playback: video plays ___ audio decoded ___ (you should hear a
      2-second tone) starts with sound without a click ___
- [ ] Controller keys pages receive: ______
- [ ] Run the test again after closing the browser, and again after
      restarting AKENO: "Local storage kept" ___ "Cookies kept" ___

## 10b. YouTube official embedded player (new in 0.7.0)

Without an API key: YouTube mode -> *Play a link*, type `aqz-KE-bpKQ` (Big
Buck Bunny on the Blender channel). With a key: a video's details -> *Play*.

- [ ] The player page loads (AKENO's page with large buttons)
- [ ] The YouTube player appears (note any "error 153": the console's origin
      was refused) ______
- [ ] The video starts by itself, or after pressing Play ______
- [ ] Sound ______
- [ ] Pause and Play work
- [ ] *Full screen* works ______
- [ ] Controller: the buttons can be reached with the D-pad or the cursor ______
- [ ] Playlist: a channel's *Play uploads* (or a playlist link) - Next and
      Previous change the video ______
- [ ] *Back to AKENO* returns to AKENO STREAM without a crash; the toast says
      "YouTube: played for ..." or names the error
- [ ] Optional: Settings -> *Player pages without browser controls* On, play
      again: the player fills the screen; Back to AKENO still works
- [ ] A video whose owner forbids embedding shows error 101/150 with that
      explanation (not a crash)
- [ ] YouTube mode -> *youtube.com*: the full site in the browser; a video
      plays? ______

## 10c. Crunchyroll website (new in 0.7.0)

Home -> *Crunchyroll* -> *Open crunchyroll.com*. Record each separately in
*Record results*:

- [ ] Website renders ______
- [ ] Sign-in on Crunchyroll's own page works ______ (AKENO never asks for it)
- [ ] Close the browser, open Crunchyroll again: still signed in? ______
- [ ] An episode page shows the player ______
- [ ] The episode plays with picture and sound ______ (expected to fail if
      the browser test found no Widevine/PlayReady/FairPlay; note any error
      message the player shows: ______)
- [ ] The verdict line in the Crunchyroll section matches what you saw

## 9. Stability

- [ ] Play, stop and reopen videos 10 times in a row without a crash
- [ ] 30 minutes of continuous playback without dropped audio or growing delay
- [ ] Switching modes rapidly while artwork loads does not crash
- [ ] Open and close the browser 10 times (websites, the YouTube player)
      without a crash; play a local video afterwards (the hardware player
      still works after the browser was used)

Result: ______ passed, ______ failed, ______ not tested.

## Results so far

| Date | Console | Version | Result |
| --- | --- | --- | --- |
| 2026-10-09 | fw 13.09, ShadowMountPlus | 0.4.0 | Crashed at launch (`CE-108255-1`): system heap returned null |
| 2026-10-09 | fw 13.09, ShadowMountPlus | 0.4.1 | Starts; UI, controller, fonts; network test passed (HTTPS 200, HLS master with 5 variants); FFmpeg self-test passed; A/V sync clip 360/360 frames presented, 0 dropped, 0 decoder errors, 0 audio underruns/errors; Big Buck Bunny HLS played and resumed; AniList artwork loaded; report export works; a fMP4/CMAF stream was refused as designed |
| 2026-10-09 | fw 13.09, ShadowMountPlus | 0.4.1/0.4.2 | Crash while typing a YouTube key on the on-screen keyboard (fixed in 0.4.3); `youtube-key.txt` was not read from the install folder (fixed in 0.4.2/0.4.3) |
| 2026-10-10 | fw 13.09, ShadowMountPlus | 0.5.0 | Tester: "everything works" (not itemised); anime and YouTube videos cannot be watched in the app, as designed |
