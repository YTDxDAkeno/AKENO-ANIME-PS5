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

// AnimePahe's domains: animepahe.pw (current), .com and .org (EverythingMoe's
// listing, June 2026), .ru and .si (used earlier). Look-alike sites under other
// domains (animepahe.ch publishes a different, slug-based site) are not
// AnimePahe and are not named here.
bool animepahe_host(const std::string &host)
{
    static const char *const kDomains[] = {"animepahe.pw", "animepahe.com", "animepahe.org",
                                           "animepahe.ru", "animepahe.si"};
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
    if (!u || u->host.empty())
        return std::nullopt;
    if (animepahe_host(u->host))
    {
        SitePolicy p;
        p.site = "AnimePahe";
        p.document = "docs/ANIMEPAHE.md";
        p.playback = PlaybackClass::browser_only;
        p.verdict = "AnimePahe cannot be a native provider: its catalogue and episodes sit "
                    "behind a DDoS-Guard browser check, and its videos are kwik web players "
                    "whose streams answer only that player. Nothing was sent to the site.";
        p.reasons = {
            "Catalogue: no documented or authorized API - its pages and a private /api "
            "endpoint behind DDoS-Guard's browser challenge, which AKENO STREAM does not solve.",
            "Playback: episodes are kwik.cx web players (packed script, hls.js); their HLS "
            "stream answers only kwik's Referer. Forging it would bypass hotlink protection. "
            "Never done.",
            "Licence: none published; it is not a licensed service (EverythingMoe lists it "
            "without its \"licensed\" tag).",
            "Still possible: Websites (the PS5 browser). On firmware 12.20 its episodes start "
            "there without Autoplay - AnimePahe's player, not AKENO STREAM's.",
        };
        p.browser_hint = "AnimePahe plays in the PS5 browser on firmware 12.20: its own "
                         "player, not AKENO STREAM's.";
        return p;
    }
    if (!anikoto_host(u->host))
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
