// AKENO STREAM PS5 - Websites that cannot become providers, and the precise reason.
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
// The investigation behind each entry is in docs/ (docs/ANIKOTO.md).
#pragma once

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
};

// The policy for an address (any of the site's known domains and their
// subdomains); nullopt for every other address. Never contacts the site.
std::optional<SitePolicy> unsupported_site(std::string_view address);
} // namespace akeno::anime
