#!/usr/bin/env bash
# AKENO STREAM PS5 - Synthetic media fixtures for host tests.
# Copyright (C) 2026 AKENO STREAM contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Generates test clips from FFmpeg's built-in test pattern and tone sources
# into build/fixtures/ (never committed). The 720p H.264 High + AAC-LC
# MPEG-TS mirrors the public HLS segment v0.3 failed to decode on the PS5.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/build/fixtures"
stamp=$(sha256sum "${BASH_SOURCE[0]}" | cut -d' ' -f1)
if [[ -f $out/.complete && $(<"$out/.complete") == "$stamp" ]]; then
    exit 0
fi
command -v ffmpeg >/dev/null || { echo 'Install the FFmpeg command-line tools to generate fixtures.' >&2; exit 2; }
rm -rf -- "$out"
mkdir -p "$out/hls/v720" "$out/hls/v360"
ff=(ffmpeg -hide_banner -loglevel error -y)
video=(-f lavfi -i testsrc2=size=1280x720:rate=25)
tone=(-f lavfi -i sine=frequency=440:sample_rate=48000)
x264=(-c:v libx264 -profile:v high -level 3.1 -pix_fmt yuv420p -g 50 -bf 2 -threads 2)

# The v0.3 failure profile: 720p25 H.264 High, AAC-LC stereo 48 kHz, MPEG-TS.
"${ff[@]}" "${video[@]}" "${tone[@]}" -t 6 "${x264[@]}" -c:a aac -ac 2 -b:a 128k -f mpegts "$out/h264-aac-720p.ts"
"${ff[@]}" "${video[@]}" "${tone[@]}" -t 3 "${x264[@]}" -c:a aac -ac 2 -b:a 128k "$out/h264-aac.mp4"
"${ff[@]}" -i "$out/h264-aac.mp4" -c copy "$out/h264-aac.mkv"
"${ff[@]}" "${video[@]}" "${tone[@]}" -t 3 "${x264[@]}" -c:a libmp3lame -ac 2 -b:a 128k -f mpegts "$out/h264-mp3.ts"
"${ff[@]}" "${video[@]}" "${tone[@]}" -t 3 "${x264[@]}" -c:a ac3 -ac 2 -b:a 192k -f mpegts "$out/h264-ac3.ts"
encoders=$(ffmpeg -hide_banner -encoders 2>/dev/null || true)
if [[ $encoders == *libx265* ]]; then
    "${ff[@]}" "${video[@]}" "${tone[@]}" -t 3 -c:v libx265 -x265-params log-level=error -pix_fmt yuv420p \
        -c:a aac -ac 2 -f mpegts "$out/hevc-aac.ts"
fi
"${ff[@]}" -f lavfi -i testsrc2=size=320x180:rate=25 -t 1 -c:v libx264 -profile:v baseline -pix_fmt yuv420p \
    -an -f mpegts "$out/video-only.ts"

# A small VOD HLS ladder with muxed audio (2 s segments).
"${ff[@]}" "${video[@]}" "${tone[@]}" -t 8 "${x264[@]}" -c:a aac -ac 2 -b:a 128k \
    -hls_time 2 -hls_playlist_type vod -hls_segment_filename "$out/hls/v720/seg%03d.ts" "$out/hls/v720/index.m3u8"
"${ff[@]}" -f lavfi -i testsrc2=size=640x360:rate=25 "${tone[@]}" -t 8 -c:v libx264 -profile:v main \
    -pix_fmt yuv420p -g 50 -c:a aac -ac 2 -b:a 96k \
    -hls_time 2 -hls_playlist_type vod -hls_segment_filename "$out/hls/v360/seg%03d.ts" "$out/hls/v360/index.m3u8"
cat > "$out/hls/master.m3u8" <<'MASTER'
#EXTM3U
#EXT-X-STREAM-INF:BANDWIDTH=900000,CODECS="avc1.4d401e,mp4a.40.2",RESOLUTION=640x360
v360/index.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=2500000,CODECS="avc1.64001f,mp4a.40.2",RESOLUTION=1280x720
v720/index.m3u8
MASTER
printf '%s\n' "$stamp" > "$out/.complete"
