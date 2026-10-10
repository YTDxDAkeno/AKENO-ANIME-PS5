// AKENO STREAM PS5 - Crunchyroll: capability report and legal alternatives.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "providers/crunchyroll.hpp"

namespace akeno
{
ProviderInfo Crunchyroll::info() const
{
    ProviderInfo info;
    info.id = "crunchyroll";
    info.name = "Crunchyroll";
    info.tagline = "Crunchyroll's own website in the PS5 browser inside AKENO STREAM.";
    info.capabilities = {
        {"Website", Support::available,
         "crunchyroll.com opens in the console's browser inside AKENO STREAM (Home or Anime > "
         "Crunchyroll). Whether every page renders is recorded per console."},
        {"Sign-in", Support::available,
         "Only on Crunchyroll's own page inside the browser. AKENO STREAM never shows a login "
         "form and never sees your password or session."},
        {"Playback", Support::needs_setup,
         "Episodes are DRM-protected. They can play only if the console's browser offers a "
         "licensed DRM system (Widevine, PlayReady or FairPlay) to web pages - the browser test "
         "measures this. DRM is never bypassed."},
        {"Catalogue API", Support::unavailable,
         "Crunchyroll publishes no developer API; AKENO STREAM does not use its private "
         "endpoints. Anime mode lists which titles Crunchyroll streams (via AniList)."},
    };
    return info;
}

ShelvesResult Crunchyroll::home(const net::CancelFlag &)
{
    ShelvesResult result;
    result.ok = true;
    Shelf options;
    options.title = "Watch Crunchyroll Legitimately";
    MediaItem app;
    app.provider = "crunchyroll";
    app.id = "official-app";
    app.kind = ItemKind::info;
    app.title = "Official Crunchyroll app for PS5";
    app.subtitle = "PlayStation Store";
    app.description =
        "Crunchyroll's own PS5 app supports your account, subscriptions and DRM-protected "
        "playback. Switch to it from the PS5 home screen.";
    app.accent = 0xf47521;
    options.items.push_back(app);
    MediaItem web;
    web.provider = "crunchyroll";
    web.id = "website";
    web.kind = ItemKind::info;
    web.title = "crunchyroll.com on your phone";
    web.subtitle = "Scan to open";
    web.description = "Open Crunchyroll on another device with the QR code.";
    web.external_url = "https://www.crunchyroll.com/";
    web.external_label = "Open crunchyroll.com";
    web.accent = 0xf47521;
    options.items.push_back(web);
    MediaItem discover;
    discover.provider = "crunchyroll";
    discover.id = "discover";
    discover.kind = ItemKind::info;
    discover.title = "Find titles in Anime mode";
    discover.subtitle = "AniList catalogue";
    discover.description =
        "Press R1 to switch to Anime. Each title's details list the official services "
        "(including Crunchyroll) that stream it.";
    discover.accent = 0x02a9ff;
    options.items.push_back(discover);
    result.shelves.push_back(std::move(options));
    return result;
}
} // namespace akeno
