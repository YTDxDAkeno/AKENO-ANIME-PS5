// AKENO STREAM PS5 - Canned API responses for host tests and screenshots.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shaped like the documented AniList GraphQL, YouTube Data API v3, PeerTube
// and Internet Archive responses. Artwork requests are answered with generated PNG gradients.
#pragma once

#include "net/http.hpp"

#include <string>

namespace akeno::test
{
std::string anilist_home_response();
std::string anilist_details_response();
std::string anilist_search_response();
std::string youtube_videos_response();
std::string youtube_search_response();
std::string youtube_error_response(const char *reason, int code);
std::string peertube_videos_response();
std::string peertube_video_response(const std::string &uuid);
std::string archive_search_response();
std::string archive_metadata_response(const std::string &id);
// A PNG of the given size with a two-colour gradient derived from seed.
std::string generated_png(int width, int height, unsigned seed);
// Answers AniList, YouTube and artwork URLs from the data above.
net::Response canned_transport(const net::Request &request);
} // namespace akeno::test
