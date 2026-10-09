# v0.3 playback failure: `ERROR -51 / FRAMES 0`

## What v0.3 did

v0.3 (`bf56a58`) downloaded the master playlist of the public Mux test
stream `x36xhzz`, took the first variant (1280x720 H.264 High, AAC-LC,
MPEG-TS segments), downloaded its first segment into memory and handed that
buffer to FFmpeg through a custom AVIO context: `avformat_open_input` with
the `mpegts` demuxer, `avformat_find_stream_info`, `avcodec_open2`, then
decode and `sws_scale` to RGBA. FFmpeg came from PacBrew's prebuilt
`libavformat`/`libavcodec`/`libavutil`/`libswscale` archives, plus
`src/ffmpeg_ps5_compat.cpp` supplying `localtime_r`, `__assert`,
`nl_langinfo` and `___mb_cur_max` that those archives (and the libiconv they
pull in) needed but the console's libc does not export.

On the console the HTTPS requests succeeded (the v0.2 path, verified on
hardware) and the app reported **error -51 with 0 frames**. In v0.3, -51 was
returned when `avformat_open_input` **or** `avformat_find_stream_info` failed,
so the failure happened before any decoding: FFmpeg could not open or probe a
complete, valid MPEG-TS segment that was already in memory. v0.3.1 split the
code into -511 (open) and -512 (probe) and kept the FFmpeg error code, but
was never run on hardware.

## What was checked

| Hypothesis | Result |
| --- | --- |
| The segment is not plain MPEG-TS (fMP4, encrypted, truncated) | Ruled out. The variant playlist has no `EXT-X-MAP` or `EXT-X-KEY`; the code checks the size cap; the same profile (720p25 H.264 High + AAC-LC in TS) opens, probes and decodes with FFmpeg on the build machine through the same custom-AVIO path (`tests/unit/test_ffmpeg_probe.cpp`, `V03FailurePathDecodes720pH264High`). |
| A missing import aborted loading | Ruled out: the title started and ran the network stages. |
| A call through a null import on the open/probe path | One import of the v0.3 executable bound to `libScePosixForWebKit` (`arc4random_buf`), whose functions are not available to native titles. FFmpeg uses it for random seeds (encoders, RTP/RTSP, HTTP auth), not in the TS demuxer's open/probe path, so it is unlikely to be the cause - but it is a latent crash. |
| Text relocations or a broken PacBrew archive layout | No text relocations in the linked executable; the archives link cleanly. |
| The system heap ran out | **Most likely.** The clean-room `libc.prx` provides the loader's heap contract, not a general-purpose heap: ProsperoTV recorded on hardware (fw 6.02) that system `malloc`/`calloc` return null once an app asks for real amounts of memory, and fixed it by giving the executable its own page-backed allocator. v0.3 had already allocated an 8 MB segment buffer and playlists when FFmpeg's probing allocations ran; an `ENOMEM` from `avformat_open_input`/`avformat_find_stream_info` gives exactly -51. 0.4.0, which allocates more than 30 MB at launch through the same heap, crashed immediately on the console (`CE-108255-1`). |
| A libc difference inside FFmpeg (locale, `iconv`, time, file APIs) | **Not excluded.** PacBrew's FFmpeg is built for a FreeBSD-like userland with libiconv and many components enabled. Its behaviour against the console's `libSceLibcInternal` (where `localtime_r`, `isatty`, `mkstemp`, `nl_langinfo` are missing and had to be faked) was never tested. |

Without a console the exact failing call cannot be identified: v0.3 did not
record FFmpeg's error code or log.

## Confirmed on the console

0.4.1 - same FFmpeg-based open/probe/decode path as v0.3, but with the
executable's own heap - ran its media self-test on a PS5 (firmware 13.09):
`mpegts h264 640x360`, stream info found, 21 packets read, 8 video and 11 audio
frames decoded. The hardware decoder presented 360 of 360 frames of the 720p
clip. This is consistent with the heap explanation above (the FFmpeg build
also changed, so the two cannot be separated with certainty).

## What 0.4.0 and 0.4.1 change

0. **0.4.1: the executable owns its heap.** `malloc`, `calloc`, `realloc`,
   `free`, `posix_memalign`, `aligned_alloc`, `strdup` and every C++
   `new`/`delete` use an allocator backed by anonymous pages
   (`tooling/native/page_allocator.hpp`, host-tested), 32-byte aligned as the
   PS5 compiler target expects. The import check fails the build if any of
   them is imported from the system. 0.4.1 also runs no code before `main`,
   reports crashes with their stage and code address as a system
   notification (and in `/download0/akeno/crash.txt`), and shows its startup
   progress on screen.

1. **Video no longer depends on FFmpeg decoding.** HLS and TS playback use
   ProsperoTV's native MPEG-TS demuxer and the console's hardware decoder
   (`sceVideodec2`, `sceAudiodec`) - the path ProsperoTV ships on PS5.
   FFmpeg is only used to remux local MP4/MKV/MOV files to TS, for AC-3/E-AC-3
   audio, and for the self-test.
2. **FFmpeg 8.0.1 is built from verified source** with the configuration
   ProsperoTV uses for native titles (`tools/setup-ffmpeg.sh`): no network,
   no devices, no filters, no assembly, only the demuxers, parsers and
   decoders needed, and the libc features the console lacks
   (`localtime_r`, `gmtime_r`, `isatty`, `mkstemp`, `arc4random_buf`,
   `pthread_setname_np`, `getauxval`, `sysctl`) switched off at configure
   time instead of faked at link time. The PacBrew FFmpeg and the
   compatibility shim are gone.
3. **The build checks every import binding** (`tools/check-imports.py`, run by
   `make app` and CI). It fails if anything binds to
   `libScePosixForWebKit` or cannot be resolved. 0.4.1 has 245 imports from
   11 modules, none of them in `libScePosixForWebKit`.
4. **The failure can now be diagnosed on the console.** Settings ->
   Diagnostics -> *Run media self-test* runs FFmpeg's open/probe/decode on the
   bundled clip and shows the stage reached, the FFmpeg error text and
   FFmpeg's own log lines; *Play hardware test clip* exercises the hardware
   decoder without network; *Export report* writes both to a file.
5. **Regression tests reproduce the v0.3 path** on the build machine:
   `V03FailurePathDecodes720pH264High` and the player tests that stream the
   same profile from a local HTTP server.

If the console still fails, the exported diagnostics report says whether the
problem is in networking, the HLS playlist, the TS demuxer, the hardware
decoder, audio output or FFmpeg, and with which error.
