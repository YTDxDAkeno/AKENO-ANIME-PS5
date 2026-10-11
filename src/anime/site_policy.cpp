// AKENO STREAM PS5 - Websites investigated as providers, and the precise result.
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

// Miruro's own list of its domains (miruro.com); look-alike domains such as
// miruro.cx are not Miruro's and are not named here.
bool miruro_host(const std::string &host)
{
    static const char *const kDomains[] = {"miruro.tv", "miruro.to", "miruro.bz", "miruro.ru",
                                           "miruro.com"};
    for (const char *domain : kDomains)
    {
        const std::string d = domain;
        if (host == d || (host.size() > d.size() && host.ends_with(d) &&
                          host[host.size() - d.size() - 1] == '.'))
            return true;
    }
    return false;
}

std::optional<url::Url> host_of(std::string_view address)
{
    const SiteAddress normalised = normalize_site_address(address);
    auto u = url::parse(normalised.ok ? std::string_view{normalised.url} : address);
    if (!u || u->host.empty())
        return std::nullopt;
    return u;
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

std::optional<KnownSite> known_site(std::string_view address)
{
    const auto u = host_of(address);
    if (!u || !miruro_host(u->host))
        return std::nullopt;
    KnownSite k;
    k.site = "Miruro";
    k.playback = PlaybackClass::browser_only;
    k.document = "docs/MIRURO.md";
    k.reasons = {
        "Episodes: no public API - a private interface behind Cloudflare bot protection, with "
        "obfuscated answers. Never got around.",
        "Video: third-party hosts that answer only their own player's Referer, via Miruro's "
        "proxy. No file for other players.",
        "Licence: none published; a 2026 MPA/ACE DMCA subpoena to Cloudflare names miruro.to.",
        "Catalogue: only what its pages publish as schema.org data, as for any website - no "
        "Play buttons.",
    };
    k.browser_hint = "Watch it in Websites (PS5 browser) with Autoplay on in Miruro's player: on "
                     "firmware 12.20 episodes start only that way.";
    k.adapters = {"schema-org"};
    return k;
}
} // namespace akeno::anime
