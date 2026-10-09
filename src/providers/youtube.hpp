// AKENO STREAM PS5 - YouTube browsing through the official YouTube Data API v3.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Uses only documented API endpoints with the user's own API key (Google Cloud
// Console, "YouTube Data API v3"). YouTube's terms allow video playback only
// through its official players; this native app has none and does not extract
// stream URLs, so videos open on the user's phone through a QR link or in the
// official YouTube app.
#pragma once

#include "providers/provider.hpp"

#include <functional>
#include <map>
#include <mutex>

namespace akeno
{
class YouTube final : public Provider
{
  public:
    using KeySource = std::function<std::string()>;
    YouTube(KeySource key, std::string region = "US", std::string safe_search = "moderate");

    [[nodiscard]] ProviderInfo info() const override;
    ShelvesResult home(const net::CancelFlag &cancel) override;
    [[nodiscard]] bool can_search() const override
    {
        return true;
    }
    ItemsResult search(const std::string &query, const net::CancelFlag &cancel) override;
    DetailsResult details(const MediaItem &item, const net::CancelFlag &cancel) override;

    void set_region(std::string region)
    {
        region_ = std::move(region);
    }
    void set_safe_search(std::string mode)
    {
        safe_search_ = std::move(mode);
    }
    [[nodiscard]] bool configured() const
    {
        return !key_().empty();
    }

    // Exposed for tests.
    static std::vector<MediaItem> parse_videos(const std::string &body, std::uint64_t now);
    static std::vector<MediaItem> parse_search(const std::string &body, std::uint64_t now);
    static std::string parse_error(const std::string &body, long status);
    static std::string relative_age(const std::string &rfc3339, std::uint64_t now);

  private:
    struct Fetch
    {
        bool ok = false;
        std::string body;
        std::string error;
    };
    Fetch get(const std::string &path, std::vector<std::pair<std::string, std::string>> params,
              const net::CancelFlag &cancel);

    KeySource key_;
    std::string region_;
    std::string safe_search_;
    std::mutex cache_lock_;
    std::uint64_t home_cached_at_ = 0;
    ShelvesResult home_cache_;
};
} // namespace akeno
