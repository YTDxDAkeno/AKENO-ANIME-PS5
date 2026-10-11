// AKENO STREAM PS5 - How an episode is played: native, official embed, website or not at all.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "anime/playback_route.hpp"

namespace akeno::anime
{
namespace
{
bool blocking(Availability a) noexcept
{
    switch (a)
    {
    case Availability::auth_required:
    case Availability::drm_unsupported:
    case Availability::region_unavailable:
    case Availability::missing:
    case Availability::catalogue_only:
        return true;
    default:
        return false;
    }
}
} // namespace

const char *route_id(Route route) noexcept
{
    switch (route)
    {
    case Route::native:
        return "native";
    case Route::embed:
        return "embed";
    case Route::website:
        return "website";
    default:
        return "unavailable";
    }
}

const char *route_label(Route route) noexcept
{
    switch (route)
    {
    case Route::native:
        return "AKENO player";
    case Route::embed:
        return "Video-only player";
    case Route::website:
        return "Website";
    default:
        return "Unavailable";
    }
}

std::optional<web::EmbedTarget> episode_embed(const Episode &episode)
{
    if (!episode.embed.empty())
        if (auto t = web::parse_embed(episode.embed))
            return t;
    if (!episode.source_ref.empty())
        return web::peertube_embed_from_ref(episode.source_ref);
    return std::nullopt;
}

PlaybackRoute route_episode(const Episode &episode)
{
    PlaybackRoute r;
    if (blocking(episode.availability))
    {
        r.reason = std::string{availability_label(episode.availability)} +
                   (episode.availability_detail.empty() ? std::string{}
                                                        : ": " + episode.availability_detail);
        return r;
    }
    if (episode.media || !episode.source_ref.empty())
    {
        r.route = Route::native;
        r.reason = "Plays in AKENO STREAM's own player.";
        return r;
    }
    if (const auto embed = episode_embed(episode))
    {
        r.route = Route::embed;
        r.embed = *embed;
        r.reason = std::string{"Plays in "} + web::embed_kind_label(embed->kind) +
                   "'s official player on AKENO's video-only page.";
        return r;
    }
    if (!episode.link.empty())
    {
        r.route = Route::website;
        r.website = episode.link;
        r.reason = "Only on the website's own pages (" +
                   (episode.link_label.empty() ? std::string{"its player"} : episode.link_label) +
                   "), not in AKENO's player.";
        return r;
    }
    r.reason = episode.availability_detail.empty() ? "The provider names no video for this episode."
                                                   : episode.availability_detail;
    return r;
}

bool embed_may_follow(Availability failure) noexcept
{
    return failure == Availability::provider_unsupported;
}
} // namespace akeno::anime
