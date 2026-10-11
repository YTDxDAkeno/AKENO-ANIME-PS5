// AKENO STREAM PS5 - Websites that cannot become providers, and the precise reason.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "anime/site_policy.hpp"

#include "anime/address.hpp"
#include "core/url.hpp"

#include <algorithm>

namespace akeno::anime
{
namespace
{
std::vector<std::string> labels_of(const std::string &host)
{
    std::vector<std::string> out;
    std::string current;
    for (const char c : host)
    {
        if (c == '.')
        {
            out.push_back(current);
            current.clear();
        }
        else
            current += c;
    }
    out.push_back(current);
    return out;
}

// Anikoto has changed domains repeatedly (anikototv.to, anikoto.tv/.net/.me/
// .cz/.se/.dk, anikoto.com.im ...); its name, not one domain, identifies it.
// "anikotoapi" is an unofficial scraper API for it, not the site's own.
bool anikoto_host(const std::string &host)
{
    static const char *const kNames[] = {"anikoto", "anikototv", "anikotoapi"};
    const std::vector<std::string> labels = labels_of(host);
    // Every label but the top-level domain.
    for (std::size_t i = 0; i + 1 < labels.size(); ++i)
        if (std::find(std::begin(kNames), std::end(kNames), labels[i]) != std::end(kNames))
            return true;
    return false;
}
} // namespace

std::optional<SitePolicy> unsupported_site(std::string_view address)
{
    const SiteAddress normalised = normalize_site_address(address);
    const auto u = url::parse(normalised.ok ? std::string_view{normalised.url} : address);
    if (!u || u->host.empty() || !anikoto_host(u->host))
        return std::nullopt;
    SitePolicy p;
    p.site = "Anikoto";
    p.document = "docs/ANIKOTO.md";
    p.verdict = "Anikoto cannot be added: it has no documented or authorized API, and its "
                "episodes are web players on other hosts with no video file AKENO STREAM may "
                "play. Nothing was sent to the site.";
    p.reasons = {
        "Catalogue: no documented or authorized API - only scraping of its pages and private "
        "endpoints, which it blocks for automated clients.",
        "Playback: episodes are web players on other hosts; a stream is obtainable only by "
        "forging Referer/Origin headers. Never done.",
        "Licence: none published; security and press reports describe the Anikoto domains as "
        "unlicensed.",
        "Still possible: open it in Websites (the PS5 browser), as before.",
    };
    return p;
}
} // namespace akeno::anime
