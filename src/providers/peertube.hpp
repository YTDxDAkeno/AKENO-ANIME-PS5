// AKENO STREAM PS5 - PeerTube, the open federated video platform.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Browsing and search go through Sepia Search (the PeerTube search index run
// by Framasoft, https://sepiasearch.org) with sensitive content excluded;
// playback details come from the video's own PeerTube instance through the
// documented REST API (GET /api/v1/videos/{id}). PeerTube publishes plain
// HLS (fragmented MP4) and MP4 files without DRM, so videos play in the app.
#pragma once

#include "providers/provider.hpp"

#include <mutex>

namespace akeno
{
class PeerTube final : public Provider
{
  public:
    // with_instances adds rows from a few well-known instances (off in tests).
    explicit PeerTube(std::string index = "https://sepiasearch.org", bool with_instances = true);

    [[nodiscard]] ProviderInfo info() const override;
    ShelvesResult home(const net::CancelFlag &cancel) override;
    [[nodiscard]] bool can_search() const override
    {
        return true;
    }
    ItemsResult search(const std::string &query, const net::CancelFlag &cancel) override;
    DetailsResult details(const MediaItem &item, const net::CancelFlag &cancel) override;

    void set_max_height(int height)
    {
        max_height_ = height;
    }

    // Exposed for tests.
    static std::vector<MediaItem> parse_search(const std::string &body, std::string *error);
    // Fills playable, description and meta from a VideoDetails document.
    static bool parse_video(const std::string &body, int max_height, MediaItem *item,
                            std::string *error);
    // "uuid@host" ids.
    static bool split_id(const std::string &id, std::string *uuid, std::string *host);

  private:
    ItemsResult query(std::vector<std::pair<std::string, std::string>> params,
                      const net::CancelFlag &cancel);

    std::string index_;
    bool with_instances_;
    int max_height_ = 1080;
    std::mutex cache_lock_;
    std::uint64_t home_cached_at_ = 0;
    ShelvesResult home_cache_;
};
} // namespace akeno
