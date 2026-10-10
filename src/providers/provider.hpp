// AKENO STREAM PS5 - Content provider interface.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every service is an adapter behind this interface. A provider states what
// it can do, and for what it cannot, the concrete reason; the UI shows those
// statements as they are. Blocking calls run on worker threads.
#pragma once

#include "net/http.hpp"
#include "providers/model.hpp"

#include <string>
#include <vector>

namespace akeno
{
enum class Support : std::uint8_t
{
    available,
    needs_setup, // works once the user provides something (e.g. an API key)
    unavailable, // cannot work on this platform/legally; detail says why
};

struct Capability
{
    std::string name; // "Browse", "Search", "Playback", "Sign-in"
    Support support = Support::unavailable;
    std::string detail; // one or two sentences
};

struct ProviderInfo
{
    std::string id;
    std::string name;
    std::string tagline;
    std::string attribution; // required credit for the data source
    std::vector<Capability> capabilities;
};

struct ShelvesResult
{
    bool ok = false;
    std::string error;
    std::vector<Shelf> shelves;
};

struct ItemsResult
{
    bool ok = false;
    std::string error;
    std::vector<MediaItem> items;
};

struct DetailsResult
{
    bool ok = false;
    std::string error;
    MediaItem item;
    std::vector<Shelf> related; // episodes, streaming links, uploads ...
};

class Provider
{
  public:
    virtual ~Provider() = default;
    [[nodiscard]] virtual ProviderInfo info() const = 0;
    virtual ShelvesResult home(const net::CancelFlag &cancel) = 0;
    [[nodiscard]] virtual bool can_search() const
    {
        return false;
    }
    virtual ItemsResult search(const std::string &query, const net::CancelFlag &cancel)
    {
        (void)query;
        (void)cancel;
        return {false, "Search is not available for this source.", {}};
    }
    virtual DetailsResult details(const MediaItem &item, const net::CancelFlag &cancel)
    {
        (void)cancel;
        return {true, {}, item, {}};
    }
};

// A GET for providers that read public JSON APIs. service names the source in
// error messages ("Could not reach PeerTube (...)").
struct Fetched
{
    bool ok = false;
    long status = 0;
    std::string body;
    std::string error;
};
Fetched fetch_text(const std::string &url, const net::CancelFlag &cancel,
                   const std::string &service, std::size_t max_bytes = 4u * 1024u * 1024u);

const char *support_label(Support support) noexcept;
// "Available" / "Setup needed" / "Not available"

// Removes simple HTML (AniList descriptions contain <br>, <i>) and decodes
// the common entities.
std::string strip_html(const std::string &html);
// "PT1H2M3S" -> 3723 seconds; 0 on malformed input.
double parse_iso8601_duration(const std::string &text);
// 3723 -> "1:02:03", 63 -> "1:03"
std::string format_clock(double seconds);
// 1234567 -> "1.2M"
std::string format_count(long long value);
} // namespace akeno
