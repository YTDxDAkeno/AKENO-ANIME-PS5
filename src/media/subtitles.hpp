// AKENO STREAM PS5 - Sidecar subtitles (WebVTT, SubRip) for the native player.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Subtitles come as separate files named by a catalogue episode; the player
// screen draws the cue text over the video. Styling, positioning and ruby
// are dropped: the text is shown plainly at the bottom of the picture.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace akeno::media
{
struct Cue
{
    double start = 0.0; // seconds
    double end = 0.0;
    std::string text; // lines separated by '\n', tags removed
};

struct SubtitleDocument
{
    bool ok = false;
    std::vector<Cue> cues; // sorted by start
    std::string error;
};

inline constexpr std::size_t kMaxCues = 20000;

// format: "vtt" or "srt"; empty: decided by the text ("WEBVTT" header).
SubtitleDocument parse_subtitles(std::string_view text, std::string_view format = {});
// The text of the cues showing at a time ("" when none).
std::string cue_text_at(const std::vector<Cue> &cues, double seconds);
// "00:01:02.500" / "01:02,500" / "62.5" -> seconds; negative when malformed.
double parse_cue_time(std::string_view text);
} // namespace akeno::media
