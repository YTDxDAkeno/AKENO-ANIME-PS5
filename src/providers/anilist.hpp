// AKENO STREAM PS5 - Anime catalogue from the public AniList GraphQL API.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// AniList (https://anilist.co) offers a documented, public, read-only API that
// needs no account. AKENO uses it for browsing, search and details of anime,
// including the legal streaming services AniList lists for each title. It
// does not provide video: licensed episodes are linked (QR code), not played.
#pragma once

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

    // Exposed for tests: parse API responses without the network.
    static ShelvesResult parse_home(const std::string &body);
    static ItemsResult parse_search(const std::string &body);
    static DetailsResult parse_details(const std::string &body);
    static MediaItem parse_media(const json::Value &media);
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
};
} // namespace akeno
