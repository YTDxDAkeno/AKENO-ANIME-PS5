// AKENO STREAM PS5 - Sources the user adds: playlists, feeds and addresses.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// AKENO STREAM ships no third-party sources and does not look for any. The
// user adds the addresses they are allowed to use; this module reads them in
// open, documented formats only:
//   - M3U / M3U8 lists (#EXTINF entries with tvg-logo, group-title ...)
//   - AKENO JSON feeds ({"title": ..., "items": [{"title", "url", ...}]})
//   - HLS playlists, MPEG-TS streams and media files (played directly)
// There is no site-specific code: nothing extracts links from web pages,
// signs requests or works around a service's protection, and DRM-protected
// streams stay unplayable.
#pragma once

#include "providers/provider.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace akeno
{
struct SourceEntry
{
    std::string name;
    std::string url;
    bool from_install_folder = false; // listed in sources.json/.txt next to the app (read-only)
};

// sources.json: {"sources": [{"name": ..., "url": ...}]} or a plain list.
std::vector<SourceEntry> parse_source_list(std::string_view text, std::string *error);
// sources.txt: one source per line, "Name = URL" or just the URL; # starts a comment.
std::vector<SourceEntry> parse_source_text(std::string_view text, std::string *error);
std::string dump_source_list(const std::vector<SourceEntry> &entries);

enum class SourceFormat : std::uint8_t
{
    channel_list, // M3U list of entries
    hls,          // an HLS playlist: one stream
    feed,         // AKENO JSON feed
    media,        // a media file or MPEG-TS stream: one item
    unknown,      // text that is none of the above (e.g. a web page)
};
const char *format_name(SourceFormat format) noexcept;
SourceFormat classify_source(std::string_view content_type, std::string_view first_bytes);

// The player kind for an entry address, from its extension (else automatic).
media::SourceKind guess_source_kind(const std::string &url);

struct SourceListing
{
    bool ok = false;
    std::string error;
    std::string title;
    std::string description;
    std::vector<Shelf> shelves;
    std::size_t entries = 0;
    std::string notice; // skipped entries, truncation
};

inline constexpr std::size_t kMaxSourceEntries = 5000;
inline constexpr std::size_t kMaxSourceGroups = 60;

// Items are provider "source"; nested lists become ItemKind::folder items
// whose id is the list address.
SourceListing parse_channel_list(std::string_view text, const std::string &base_url,
                                 const std::string &name);
SourceListing parse_feed(std::string_view text, const std::string &base_url,
                         const std::string &name);

// One source opened for browsing. home() downloads and reads it; search()
// filters what home() found.
class SourceProvider final : public Provider
{
  public:
    explicit SourceProvider(SourceEntry entry);

    [[nodiscard]] ProviderInfo info() const override;
    ShelvesResult home(const net::CancelFlag &cancel) override;
    [[nodiscard]] bool can_search() const override
    {
        return true;
    }
    ItemsResult search(const std::string &query, const net::CancelFlag &cancel) override;

    [[nodiscard]] const SourceEntry &entry() const noexcept
    {
        return entry_;
    }
    // Format found by the last home() call.
    [[nodiscard]] SourceFormat format() const;

  private:
    SourceEntry entry_;
    mutable std::mutex lock_;
    std::vector<MediaItem> all_;
    SourceFormat format_ = SourceFormat::unknown;
};

// The text every place that adds sources shows.
extern const char *const kSourcesNotice;
} // namespace akeno
