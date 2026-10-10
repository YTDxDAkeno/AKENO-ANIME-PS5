// AKENO STREAM PS5 - Public-domain films from the Internet Archive.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Uses the documented archive.org APIs: advanced search (advancedsearch.php)
// for rows and search, the metadata API (/metadata/{identifier}) for the
// files of an item, and /download/{identifier}/{file} for playback. Only
// curated collections are listed - Feature Films (films believed to be in
// the public domain), the Prelinger Archives, and Animation & Cartoons up to
// 1963 - not arbitrary user uploads.
#pragma once

#include "providers/provider.hpp"

#include <mutex>

namespace akeno
{
class InternetArchive final : public Provider
{
  public:
    explicit InternetArchive(std::string base = "https://archive.org");

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
    static std::vector<MediaItem> parse_search(const std::string &body, const std::string &base,
                                               std::string *error);
    static bool parse_metadata(const std::string &body, const std::string &base, int max_height,
                               MediaItem *item, std::string *error);
    // The advanced-search query for a user's words within the curated collections.
    static std::string search_query(const std::string &words);

  private:
    ItemsResult query(const std::string &q, int rows, const net::CancelFlag &cancel);

    std::string base_;
    int max_height_ = 1080;
    std::mutex cache_lock_;
    std::uint64_t home_cached_at_ = 0;
    ShelvesResult home_cache_;
};
} // namespace akeno
