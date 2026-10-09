// AKENO STREAM PS5 - URL parsing, reference resolution and encoding.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace akeno::url
{
struct Url
{
    std::string scheme;    // lower-case, e.g. "https"
    std::string authority; // host[:port], userinfo is rejected
    std::string host;      // lower-case
    int port = -1;
    std::string path;  // starts with '/' for absolute URLs (may be empty)
    std::string query; // without '?'
    bool has_query = false;

    [[nodiscard]] std::string str() const;
    [[nodiscard]] bool is_http() const noexcept
    {
        return scheme == "http" || scheme == "https";
    }
    [[nodiscard]] bool is_https() const noexcept
    {
        return scheme == "https";
    }
};

// Parses an absolute http(s) URL. Fragments are dropped. Rejects whitespace,
// control characters, userinfo ("user:pass@") and malformed ports.
std::optional<Url> parse(std::string_view text);

// Resolves reference against an absolute base (RFC 3986 section 5.2), including
// "../" segments. Returns nullopt if the base is not a valid URL or the
// reference contains forbidden characters.
std::optional<std::string> resolve(std::string_view base, std::string_view reference);

// Percent-encodes everything except RFC 3986 unreserved characters.
std::string encode_component(std::string_view text);
std::string decode_component(std::string_view text);
std::string build_query(const std::vector<std::pair<std::string, std::string>> &params);

// Removes credential-bearing query parameters (key, token, signature ...) so a
// URL can be shown or logged in diagnostics without leaking secrets.
std::string redact(std::string_view url);
} // namespace akeno::url
