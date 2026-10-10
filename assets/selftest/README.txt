Synthetic test clips generated with FFmpeg's built-in sources (no third-party content):
- h264-aac-360p.ts: testsrc2 pattern + tone, 2 s, 640x360 25 fps H.264 High + AAC-LC.
- av-sync-720p.ts: testsrc pattern with a seconds counter + a steady tone with a higher
  beep at the start of every second (sine beep_factor), 12 s, 1280x720 30 fps H.264 + AAC-LC.
Used by Diagnostics (media self-test, hardware test clip) and Home > Offline Test Clips.
- h264-aac-360p.mp4: the same 2 s clip remuxed (no re-encode) into MP4 with faststart. Played by
  the browser capability test (Settings > Diagnostics > Browser) to check HTML5 video and audio.
