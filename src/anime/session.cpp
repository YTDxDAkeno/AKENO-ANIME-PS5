// AKENO STREAM PS5 - One episode playing in AKENO's native player.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "anime/session.hpp"

#include "providers/provider.hpp"

namespace akeno::anime
{
std::string episode_heading(const Season &season, const Episode &episode)
{
    const std::string label = episode_label(season, episode);
    return episode.title.empty() ? label : label + " - " + episode.title;
}

PlaybackSession make_session(const Series &series, EpisodeRef ref, const MediaResource &media,
                             double position)
{
    PlaybackSession s;
    const Season &season = series.seasons.at(ref.season);
    const Episode &episode = season.episodes.at(ref.episode);
    s.provider_id = series.provider_id;
    s.series_id = series.id;
    s.season_id = season.id;
    s.episode_id = episode.id;
    s.series_title = series.title;
    s.episode_title = episode.title;
    s.label = episode_label(season, episode);
    s.media = media;
    s.subtitles = media.subtitles;
    s.audio = media.audio;
    s.position = position;
    s.ref = ref;
    s.next = next_episode(series, ref);
    s.previous = previous_episode(series, ref);
    if (s.next)
        s.next_label = episode_heading(series.seasons[s.next->season],
                                       series.seasons[s.next->season].episodes[s.next->episode]);
    if (s.previous)
        s.previous_label =
            episode_heading(series.seasons[s.previous->season],
                            series.seasons[s.previous->season].episodes[s.previous->episode]);
    return s;
}

MediaItem session_item(const PlaybackSession &session, const Series &series)
{
    const Season &season = series.seasons.at(session.ref.season);
    const Episode &episode = season.episodes.at(session.ref.episode);
    MediaItem m;
    m.provider = kEpisodeItemProvider;
    m.id = session.key();
    m.kind = ItemKind::video;
    m.title = series.title;
    m.subtitle = episode_heading(season, episode);
    m.description = episode.description.empty() ? series.description : episode.description;
    m.image_url = !episode.thumbnail.empty() ? episode.thumbnail
                  : !series.banner.empty()   ? series.banner
                                             : series.poster;
    m.banner_url = series.banner;
    m.duration = episode.duration;
    if (episode.duration > 0.0)
        m.badge = format_clock(episode.duration);
    m.meta = series.provider_name;
    m.attribution = series.attribution.empty() ? series.provider_name : series.attribution;
    m.genres = series.genres;
    m.playable = Playable{session.media.kind, session.media.url};
    return m;
}

bool split_episode_key(const std::string &key, std::string *provider, std::string *series,
                       std::string *season, std::string *episode)
{
    std::string parts[4];
    std::size_t start = 0;
    for (int i = 0; i < 4; ++i)
    {
        const std::size_t slash = i < 3 ? key.find('/', start) : std::string::npos;
        if (i < 3 && slash == std::string::npos)
            return false;
        parts[i] = key.substr(start, i < 3 ? slash - start : std::string::npos);
        if (parts[i].empty() || parts[i].find('/') != std::string::npos)
            return false;
        start = slash + 1;
    }
    *provider = parts[0];
    *series = parts[1];
    *season = parts[2];
    *episode = parts[3];
    return true;
}
} // namespace akeno::anime
