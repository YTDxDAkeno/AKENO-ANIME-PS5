// AKENO STREAM PS5 - Anime catalogue from the public AniList GraphQL API.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// AniList (https://anilist.co) offers a documented, public, read-only API that
// needs no account. AKENO uses it for browsing, search and details of anime,
// including the legal streaming services AniList lists for each title. It
// does not provide video: licensed episodes are linked (QR code), not played.
#pragma once

#include "anime/model.hpp"
#include "providers/provider.hpp"

#include <cstdint>
#include <map>
#include <mutex>

namespace akeno
{
class AniList final : public Provider
{
  public:
    explicit AniList(bool include_adult = false);

    [[nodiscard]] ProviderInfo info() const override;
    ShelvesResult home(const net::CancelFlag &cancel) override;
    [[nodiscard]] bool can_search() const override
    {
        return true;
    }
    ItemsResult search(const std::string &query, const net::CancelFlag &cancel) override;
    DetailsResult details(const MediaItem &item, const net::CancelFlag &cancel) override;

    void set_include_adult(bool include)
    {
        include_adult_ = include;
    }

    // Anime mode rows beyond the front page: new episodes (aired in the
    // last days), Action, Romance, Fantasy, Comedy and Movies.
    ShelvesResult genre_rows(const net::CancelFlag &cancel);
    struct Filters
    {
        int year = 0;
        std::string genre;
        std::string status; // AniList MediaStatus: FINISHED, RELEASING, NOT_YET_RELEASED
    };
    ItemsResult search(const std::string &query, const Filters &filters,
                       const net::CancelFlag &cancel);
    // A title as an anime series: its titles, the seasons AniList links as
    // prequel/sequel (TV, TV short, ONA), and per season the episode titles
    // AniList knows from official streaming pages. Nothing here is playable:
    // episodes are catalogue entries with a link to their official page.
    struct SeriesResult
    {
        bool ok = false;
        std::string error;
        anime::Series series;
        int focus_season = 0; // the season that is the requested title
    };
    SeriesResult series(int anilist_id, const net::CancelFlag &cancel);
    // Episode titles of one season (AniList's streamingEpisodes).
    std::vector<anime::Episode> season_episodes(int anilist_id, const net::CancelFlag &cancel);

    // Exposed for tests: parse API responses without the network.
    static ShelvesResult parse_home(const std::string &body);
    static ItemsResult parse_search(const std::string &body);
    static DetailsResult parse_details(const std::string &body);
    static MediaItem parse_media(const json::Value &media);
    static ShelvesResult parse_genre_rows(const std::string &body);
    // "Episode 3 - The Cat" -> number 3, title "The Cat".
    static std::vector<anime::Episode> parse_streaming_episodes(const json::Value &list);
    // Current anime season for a Unix time ("WINTER", "SPRING", "SUMMER", "FALL").
    static std::pair<std::string, int> season_for(std::uint64_t unix_seconds);

  private:
    struct Response
    {
        bool ok = false;
        std::string body;
        std::string error;
    };
    Response post(const std::string &query, const json::Value &variables,
                  const net::CancelFlag &cancel);

    bool include_adult_;
    std::mutex cache_lock_;
    std::uint64_t home_cached_at_ = 0;
    ShelvesResult home_cache_;
    std::map<std::string, DetailsResult> details_cache_;
    std::uint64_t rows_cached_at_ = 0;
    ShelvesResult rows_cache_;
    std::map<int, SeriesResult> series_cache_;
    std::map<int, std::vector<anime::Episode>> episodes_cache_;
};
} // namespace akeno
