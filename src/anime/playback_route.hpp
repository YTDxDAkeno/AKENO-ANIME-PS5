// AKENO STREAM PS5 - How an episode is played: native, official embed, website or not at all.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// In order of preference:
//   native       AKENO STREAM's own player: a DRM-free file or stream the
//                provider names (or looks up when it plays);
//   embed        the service's officially supported embedded player on
//                AKENO's video-only page (web/embed.hpp: YouTube, PeerTube);
//   website      the provider's own page in the PS5 browser, labelled as the
//                website's player, not AKENO's;
//   unavailable  nothing AKENO STREAM may play, and why.
// DRM, a sign-in, a region restriction or a missing video never lead to an
// embed or a website: they are reported, not worked around.
#pragma once

#include "anime/model.hpp"
#include "web/embed.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace akeno::anime
{
enum class Route : std::uint8_t
{
    native,
    embed,
    website,
    unavailable,
};
const char *route_id(Route route) noexcept;    // "native", "embed", "website", "unavailable"
const char *route_label(Route route) noexcept; // "AKENO player", "Video-only player", ...

struct PlaybackRoute
{
    Route route = Route::unavailable;
    web::EmbedTarget embed; // Route::embed
    std::string website;    // Route::website: the page
    std::string reason;     // one sentence
};

// The episode's official embedded player, if it has one: an embed address
// the provider gave (Episode::embed) or a PeerTube video ("uuid@host").
std::optional<web::EmbedTarget> episode_embed(const Episode &episode);

// The route for an episode as its provider lists it.
PlaybackRoute route_episode(const Episode &episode);

// After AKENO's own player could not play an episode: whether its official
// embed may be tried instead. Only for a format AKENO cannot play - never for
// DRM, a sign-in, a region, a missing video or a network failure.
bool embed_may_follow(Availability failure) noexcept;
} // namespace akeno::anime
