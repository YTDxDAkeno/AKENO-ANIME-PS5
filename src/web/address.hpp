// AKENO STREAM PS5 - What the user typed, turned into an address the browser may open.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Only http and https leave the app: javascript:, data:, file:, about: and
// every other scheme are refused, as are user:password@ addresses and the
// console's own loopback addresses (reserved for AKENO STREAM's own pages).
// Text that is not an address becomes a search with the engine the user
// chose. There is no list of allowed or blocked sites.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace akeno::web
{
enum class SearchEngine : std::uint8_t
{
    duckduckgo,
    google,
    bing,
    startpage,
};
inline constexpr int kSearchEngineCount = 4;
const char *engine_id(SearchEngine engine) noexcept;
const char *engine_name(SearchEngine engine) noexcept;
SearchEngine engine_from_id(std::string_view id) noexcept;
std::string search_url(SearchEngine engine, std::string_view query);

struct Destination
{
    bool ok = false;
    bool is_search = false; // the text was not an address
    bool insecure = false;  // plain http: not encrypted
    std::string url;        // normalised http(s) address
    std::string error;      // why it cannot be opened
};

// Address bar behaviour: "youtube.com" -> https://youtube.com/, "cat videos"
// -> a search, "javascript:..." -> refused.
Destination interpret(std::string_view typed, SearchEngine engine);
// For stored and imported addresses: an address or nothing, never a search.
Destination check_address(std::string_view text);

// "www.youtube.com" -> "youtube.com" for labels.
std::string display_host(std::string_view url);
// "https://example.com:8443" for an address ("" when invalid).
std::string origin_of(std::string_view url);
bool is_loopback_host(std::string_view host) noexcept;

// YouTube links and IDs, for the embedded official player.
struct YouTubeTarget
{
    std::string video_id; // 11 characters, or empty for a playlist only
    std::string list_id;  // playlist, or empty
    int start_seconds = 0;
};
bool valid_video_id(std::string_view id) noexcept;
bool valid_list_id(std::string_view id) noexcept;
// Accepts watch, youtu.be, shorts, embed, live and playlist links (with or
// without https://) and bare video IDs.
std::optional<YouTubeTarget> parse_youtube(std::string_view text);
// "90", "90s", "1m30s", "1h2m3s" -> seconds; 0 when malformed.
int parse_start_time(std::string_view text) noexcept;
} // namespace akeno::web
