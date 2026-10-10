// AKENO STREAM PS5 - Checks that an episode's resource is real, DRM-free media.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// An HTTP 200 proves nothing: a web page, a login form or an encrypted file
// all answer 200. The probe reads the first 64 KiB (or the start of a file
// on console storage) and decides from the bytes: an MP4/MKV/MPEG-TS file or
// an HLS playlist is playable; DRM markers (CENC boxes, SAMPLE-AES or a
// Widevine/PlayReady/FairPlay key in a playlist) are not; a page is not
// media at all. Runs on a worker thread.
#pragma once

#include "anime/model.hpp"
#include "net/http.hpp"

#include <string>
#include <string_view>

namespace akeno::anime
{
struct ProbeResult
{
    Availability availability = Availability::network_error;
    std::string detail;    // one sentence for the episode list
    std::string container; // "MP4", "HLS", "MPEG-TS", "Matroska"
};

ProbeResult probe_media(const MediaResource &media, const net::CancelFlag &cancel);
// Judges the first bytes of a resource (exposed for tests). content_type may
// be empty; status 0 means a file on console storage.
ProbeResult judge_media(std::string_view first_bytes, std::string_view content_type);
} // namespace akeno::anime
