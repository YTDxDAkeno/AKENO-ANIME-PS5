// AKENO STREAM PS5 - Websites investigated as providers, and the precise result.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Some websites users try as providers were investigated and offer no
// permitted way in: no documented or authorized API, a catalogue reachable
// only by scraping pages and private endpoints the site blocks for automated
// clients, and episodes that are third-party web players whose streams are
// obtainable only by forging request headers. AKENO STREAM does none of that.
// For such a site, "Add Streaming Provider" explains why at once - without
// sending a single request to it - and the site stays in Websites, where the
// PS5's own browser opens it as before.
//
// Other websites can be read as a catalogue - they publish their series as
// structured data on their own pages, read like any website's - but their
// episodes cannot play in AKENO STREAM's player. known_site() names such a
// site, the precise reason, and how to watch it in the PS5 browser instead.
//
// The investigation behind each entry is in docs/ (docs/ANIKOTO.md,
// docs/MIRURO.md, docs/ANIMEPAHE.md).
#pragma once

#include "anime/provider.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace akeno::anime
{
struct SitePolicy
{
    std::string site;    // "Anikoto"
    std::string verdict; // one or two sentences for the report
    // One line each: catalogue, playback, licence, where it can still be used.
    std::vector<std::string> reasons;
    std::string document; // "docs/ANIKOTO.md"
    // What its episodes do in AKENO STREAM: unsupported, or browser-only for a
    // site confirmed to play in the PS5 browser (AnimePahe), with how.
    PlaybackClass playback = PlaybackClass::unsupported;
    std::string browser_hint;
};

// The policy for an address (any of the site's known domains and their
// subdomains); nullopt for every other address. Never contacts the site.
std::optional<SitePolicy> unsupported_site(std::string_view address);

struct KnownSite
{
    std::string site; // "Miruro"
    // What its episodes do in AKENO STREAM (website-only for Miruro: they play
    // in the PS5 browser, never in AKENO STREAM's player).
    PlaybackClass playback = PlaybackClass::browser_only;
    // One line each: episodes, video, licence, what AKENO STREAM reads.
    std::vector<std::string> reasons;
    // How to watch it on the website, from what was confirmed on a console.
    std::string browser_hint;
    // The website adapters that may read it (no other adapter's requests are
    // sent to it: Miruro is not a PeerTube instance, so it is never asked for
    // PeerTube's API).
    std::vector<std::string> adapters;
    std::string document; // "docs/MIRURO.md"
};

// The known website an address belongs to (its official domains and their
// subdomains only); nullopt for every other address. Never contacts the site.
std::optional<KnownSite> known_site(std::string_view address);
} // namespace akeno::anime
