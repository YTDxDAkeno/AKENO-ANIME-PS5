Synthetic test clips generated with FFmpeg's built-in sources (no third-party content):
- h264-aac-360p.ts: testsrc2 pattern + tone, 2 s, 640x360 25 fps H.264 High + AAC-LC.
- av-sync-720p.ts: testsrc pattern with a seconds counter + a steady tone with a higher
  beep at the start of every second (sine beep_factor), 12 s, 1280x720 30 fps H.264 + AAC-LC.
Used by Diagnostics (media self-test, hardware test clip) and Home > Offline Test Clips.
- h264-aac-360p.mp4: the same 2 s clip remuxed (no re-encode) into MP4 with faststart. Played by
  the browser capability test (Settings > Diagnostics > Browser) to check HTML5 video and audio.
- h264-aac-360p-frag.mp4: the same 2 s clip remuxed (no re-encode) into fragmented MP4
  (frag_keyframe+empty_moov+default_base_moof, 0.5 s fragments). The playback lab feeds it to
  MediaSource the way HLS.js and DASH players do; h264-aac-360p.ts is its HLS segment.
- h264-aac-360p-cenc.m4s: h264-aac-360p-frag.mp4 encrypted with Clear Key (ISO/IEC 23001-7 'cenc',
  AES-128-CTR; key ID 0123456789abcdef0123456789abcdef, key fedcba9876543210fedcba9876543210 - a
  published test key that protects nothing) by tools/make-clearkey-clip.py. The playback lab plays
  it through MediaSource and EME to see whether the browser can decrypt video at all. The .m4s
  extension keeps it out of the Library: AKENO's own player does not decrypt.
