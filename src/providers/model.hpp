// AKENO STREAM PS5 - Content model shared by providers and screens.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/json.hpp"
#include "media/player.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace akeno
{
enum class ItemKind : std::uint8_t
{
    video,   // directly playable
    series,  // anime series with details
    channel, // YouTube channel
    folder,  // local directory
    file,    // local media file
    info,    // informational card (e.g. setup instructions)
};

struct Playable
{
    media::SourceKind kind = media::SourceKind::hls;
    std::string url;
};

struct MediaItem
{
    std::string provider; // open, anilist, youtube, local, crunchyroll
    std::string id;       // unique within the provider
    ItemKind kind = ItemKind::video;
    std::string title;
    std::string subtitle; // channel, studio, folder ...
    std::string description;
    std::string image_url;  // http(s) artwork, or empty
    std::string banner_url; // wide artwork for details
    bool portrait = false;  // 2:3 poster rather than 16:9 thumbnail
    std::string badge;      // short overlay text: "LIVE", "12:04", "24 EP"
    std::string meta;       // details line: "2024 · TV · 24 episodes"
    double duration = 0.0;
    std::optional<Playable> playable;
    std::string external_url;   // where the content can be watched legitimately
    std::string external_label; // "Watch on YouTube"
    std::string attribution;    // licence / source credit
    std::vector<std::string> genres;
    std::uint32_t accent = 0; // optional 0xRRGGBB colour from the source
    std::string playlist;     // YouTube: a channel's uploads playlist
    bool icon_art = false;    // image_url is a small site icon, drawn centred

    [[nodiscard]] std::string key() const
    {
        return provider + ":" + id;
    }
    [[nodiscard]] json::Value to_json() const;
    static MediaItem from_json(const json::Value &value);
};

struct Shelf
{
    std::string title;
    std::vector<MediaItem> items;
    bool portrait = false;
};
} // namespace akeno
