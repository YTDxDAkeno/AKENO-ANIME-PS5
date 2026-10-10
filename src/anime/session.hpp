// AKENO STREAM PS5 - One episode playing in AKENO's native player.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A PlaybackSession is what the player needs to know about an episode beyond
// the media address: which series, season and episode it is (provider-
// qualified), its subtitle and audio tracks, where to resume and what comes
// next. It holds no credentials: providers that need a sign-in are not
// played at all.
#pragma once

#include "anime/model.hpp"
#include "providers/model.hpp"

#include <optional>
#include <string>
#include <vector>

namespace akeno::anime
{
// MediaItem::provider of an anime episode in history and the player.
inline constexpr char kEpisodeItemProvider[] = "anime";

struct PlaybackSession
{
    std::string provider_id;
    std::string series_id;
    std::string season_id;
    std::string episode_id;
    std::string series_title;
    std::string episode_title; // may be empty (not invented)
    std::string label;         // "S1 E3"
    MediaResource media;
    std::vector<SubtitleTrack> subtitles;
    std::vector<AudioTrack> audio;
    double position = -1.0; // -1: resume from history
    EpisodeRef ref;
    std::optional<EpisodeRef> next;
    std::optional<EpisodeRef> previous;
    std::string next_label; // "S1 E4 - Title"
    std::string previous_label;

    [[nodiscard]] std::string key() const
    {
        return episode_key(provider_id, series_id, season_id, episode_id);
    }
};

// A session for an episode with its resolved resource.
PlaybackSession make_session(const Series &series, EpisodeRef ref, const MediaResource &media,
                             double position = -1.0);
// The episode as an item for the player, history and Continue Watching.
MediaItem session_item(const PlaybackSession &session, const Series &series);
// "S1 E4 - Title" (or just "S1 E4").
std::string episode_heading(const Season &season, const Episode &episode);
// Splits "provider/series/season/episode"; false when malformed.
bool split_episode_key(const std::string &key, std::string *provider, std::string *series,
                       std::string *season, std::string *episode);
} // namespace akeno::anime
