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
    info.tagline = "Not integrated: no authorized interface exists for independent apps.";
    info.capabilities = {
        {"Catalogue API", Support::unavailable,
         "Crunchyroll publishes no developer API. Its apps use private endpoints that require "
         "Crunchyroll-issued "
         "client credentials; using them without permission would breach Crunchyroll's terms."},
        {"Sign-in", Support::unavailable,
         "No OAuth or device-code sign-in is offered to third parties. AKENO will not ask for your "
         "Crunchyroll "
         "password."},
        {"Playback", Support::unavailable,
         "Episodes are protected with DRM (Widevine/PlayReady). Licences are issued only to "
         "certified players; a "
         "homebrew app cannot obtain them, and bypassing DRM is illegal."},
        {"Discovery", Support::available,
         "Anime mode shows which titles are on Crunchyroll (via AniList) with QR links to the "
         "official pages."},
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
