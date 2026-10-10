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
# CMAF: fragmented-MP4 segments with an initialization section (EXT-X-MAP).
hls_vod=(-hls_time 2 -hls_playlist_type vod)
fmp4=(-hls_segment_type fmp4 -hls_fmp4_init_filename init.mp4)
mkdir -p "$out/hls/fmp4" "$out/hls/split/video" "$out/hls/split/audio" "$out/hls/aes" "$out/hls/single" \
    "$out/hls/single-fmp4"
"${ff[@]}" "${video[@]}" "${tone[@]}" -t 6 "${x264[@]}" -c:a aac -ac 2 -b:a 128k "${hls_vod[@]}" "${fmp4[@]}" \
    -hls_segment_filename "$out/hls/fmp4/seg%03d.m4s" "$out/hls/fmp4/index.m3u8"
# Video and audio in separate renditions (EXT-X-MEDIA TYPE=AUDIO).
"${ff[@]}" "${video[@]}" -t 6 "${x264[@]}" -an "${hls_vod[@]}" "${fmp4[@]}" \
    -hls_segment_filename "$out/hls/split/video/seg%03d.m4s" "$out/hls/split/video/index.m3u8"
"${ff[@]}" "${tone[@]}" -t 6 -c:a aac -ac 2 -b:a 128k "${hls_vod[@]}" "${fmp4[@]}" \
    -hls_segment_filename "$out/hls/split/audio/seg%03d.m4s" "$out/hls/split/audio/index.m3u8"
cat > "$out/hls/split/master.m3u8" <<'SPLIT'
#EXTM3U
#EXT-X-INDEPENDENT-SEGMENTS
#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="aac",NAME="English",LANGUAGE="en",DEFAULT=YES,AUTOSELECT=YES,URI="audio/index.m3u8"
#EXT-X-STREAM-INF:BANDWIDTH=2500000,CODECS="avc1.64001f,mp4a.40.2",RESOLUTION=1280x720,AUDIO="aac"
video/index.m3u8
SPLIT
# Standard AES-128 segment encryption (not DRM); the IV is the sequence number.
printf '\x00\x11\x22\x33\x44\x55\x66\x77\x88\x99\xaa\xbb\xcc\xdd\xee\xff' > "$out/hls/aes/key.bin"
printf 'key.bin\n%s\n' "$out/hls/aes/key.bin" > "$out/aes.keyinfo"
"${ff[@]}" "${video[@]}" "${tone[@]}" -t 6 "${x264[@]}" -c:a aac -ac 2 -b:a 128k "${hls_vod[@]}" \
    -hls_key_info_file "$out/aes.keyinfo" -hls_segment_filename "$out/hls/aes/seg%03d.ts" "$out/hls/aes/index.m3u8"
# The same with the IV taken from the media sequence number (no IV attribute).
if command -v openssl >/dev/null; then
    mkdir -p "$out/hls/aes-seq"
    cp "$out/hls/aes/key.bin" "$out/hls/aes-seq/key.bin"
    {
        printf '#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:5\n'
        printf '#EXT-X-KEY:METHOD=AES-128,URI="key.bin"\n'
        for i in 0 1 2 3; do
            openssl enc -aes-128-cbc -K 00112233445566778899aabbccddeeff -iv "$(printf '%032x' $((i + 5)))" \
                -in "$out/hls/v720/seg00$i.ts" -out "$out/hls/aes-seq/seg00$i.bin"
            printf '#EXTINF:2.0,\nseg00%d.bin\n' "$i"
        done
        printf '#EXT-X-ENDLIST\n'
    } > "$out/hls/aes-seq/index.m3u8"
fi
# One file per rendition, segments addressed with EXT-X-BYTERANGE.
"${ff[@]}" "${video[@]}" "${tone[@]}" -t 6 "${x264[@]}" -c:a aac -ac 2 -b:a 128k "${hls_vod[@]}" \
    -hls_flags single_file "$out/hls/single/index.m3u8"
"${ff[@]}" "${video[@]}" "${tone[@]}" -t 6 "${x264[@]}" -c:a aac -ac 2 -b:a 128k "${hls_vod[@]}" \
    -hls_segment_type fmp4 -hls_flags single_file "$out/hls/single-fmp4/index.m3u8"
printf '%s\n' "$stamp" > "$out/.complete"
