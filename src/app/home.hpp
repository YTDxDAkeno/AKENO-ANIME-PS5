// AKENO STREAM PS5 - The rows of Home.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Home is built from what the Home screen gathers: local data (history,
// favourites, websites, video files in the install folder) at once, the
// online rows (YouTube, AniList, PeerTube and the Internet Archive) when
// they arrive. The composition is a pure function so it can be tested.
#pragma once

#include "app/store.hpp"
#include "core/fs.hpp"
#include "providers/provider.hpp"
#include "web/websites.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace akeno
{
struct HomeContent
{
    std::vector<HistoryEntry> continue_watching; // unfinished, newest first
    std::vector<HistoryEntry> history;           // everything played, newest first
    std::vector<MediaItem> youtube;              // YouTube's trending videos (with an API key)
    bool youtube_configured = false;             // browsing works (an API key is saved)
    std::vector<MediaItem> anime;                // AniList's trending anime
    std::vector<MediaItem> discover;             // PeerTube and the Internet Archive
    std::vector<MediaItem> open_streams;         // open movies and public test streams
    std::vector<MediaItem> local_files;          // video files in the install folder's media
    std::vector<MediaItem> bundled;              // the offline test clips
    std::vector<web::Website> websites;
    std::vector<MediaItem> favorites;
};

inline constexpr std::size_t kHomeRowItems = 20;
inline constexpr std::size_t kHomeRecentWebsites = 10;

// The rows in order: Continue Watching, YouTube, Anime, My Websites,
// Discover, Local Library, Recently Added Websites, Favorites. A row with
// nothing in it is left out.
std::vector<Shelf> home_shelves(const HomeContent &content);

// Where a card on Home leads that is not a video or a website.
inline constexpr char kHomeYouTubeLink[] = "yt-link";
inline constexpr char kHomeYouTubeSite[] = "yt-site";

// The first non-empty row of a provider's front page, at most limit items.
std::vector<MediaItem> first_row(const ShelvesResult &result, std::size_t limit);
// Discover's front page: its first PeerTube and Internet Archive rows,
// alternating, at most limit items.
std::vector<MediaItem> discover_row(const ShelvesResult &result, std::size_t limit);

// A playable video file as a card (Library, Home).
MediaItem local_file_item(const std::string &directory, const fs::Entry &entry);
// The playable video files directly in a folder (not its subfolders), by
// name; empty when the folder is missing or unreadable.
std::vector<MediaItem> local_media_items(const std::string &directory, std::size_t limit);
} // namespace akeno
