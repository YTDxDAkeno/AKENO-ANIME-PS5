# Akeno Anime v0.3.1 — PS5 video decoder diagnostic

The app UI is English. This is a **video-only MPEG-TS test**, not a Crunchyroll client yet.

## Diagnosing playback on the PS5

Open **START**, then press **X**. The app downloads the public sample master playlist, one variant playlist, and one segment. If playback fails, photograph the information below:

- `VIDEO STATUS` / `FRAMES` / `ERROR`
- `HTTP` / `BYTES` / `TS SYNC` / `DEMUXER`
- `FFMPEG ERROR` / `STREAM INFO`

Key codes:

- `-43`: downloaded bytes did not have an MPEG-TS sync byte at offsets 0, 188, 376.
- `-50`: no MPEG-TS FFmpeg demuxer registered, or failed to allocate the in-memory reader.
- `-511`: `avformat_open_input` returned a negative FFmpeg error. Inspect `FFMPEG ERROR` value.
- `-512`: FFmpeg failed to identify any streams. Inspect `STREAM INFO` value.
- `-52`: no video track in the detected transport streams.
- `-53`: FFmpeg could not create/open a video decoder (possible codec issue).
- `-55`: failed to create the software scale/convert pipeline.

**Do not assume that HTTP 200 means video playback works.**
No audio, authenticated Crunchyroll requests or DRM are supported.

The HLS sample is hosted at `https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8`.
This is a diagnostic client for a small public, unencrypted MPEG-TS segment, not a
general-purpose streaming app.
