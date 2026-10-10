// AKENO STREAM PS5 - A quick look at a website before the browser opens it.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fetches the start page with AKENO STREAM's own HTTP client (TLS verified,
// no cookies, nothing sent but the request) to report network problems in
// plain words before the system browser opens, and to find the page title
// and the site's icon for its card. This is not a check of what the page can
// play: only the browser itself knows that.
#pragma once

#include "net/http.hpp"

#include <initializer_list>
#include <string>
#include <string_view>

namespace akeno::web
{
struct SiteProbe
{
    bool reached = false;  // the site answered (any HTTP status)
    bool blocking = false; // DNS, connection, TLS or certificate failure
    long status = 0;
    std::string final_url;
    std::string title;
    std::string icon_url;
    std::string site_name;   // og:site_name / application-name
    std::string theme_color; // <meta name="theme-color">, "#rrggbb"
    std::string problem;     // plain-language description, "" when fine
    std::string detail;      // technical line for diagnostics
};

// Runs on a worker thread. Reads at most the first 384 KiB of the page.
SiteProbe probe_site(const std::string &url, const net::CancelFlag &cancel);

// Exposed for tests.
std::string find_title(std::string_view html);
std::string find_icon(std::string_view html, const std::string &page_url);
// The content of the first <meta name=... / property=...> among names.
std::string find_meta(std::string_view html, std::initializer_list<std::string_view> names);
// "#rgb" / "#rrggbb" (any case) as "#rrggbb"; "" for anything else.
std::string normalise_color(std::string_view text);
std::string describe_failure(const net::Response &response, bool *blocking);
} // namespace akeno::web
