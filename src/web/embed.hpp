// AKENO STREAM PS5 - Officially supported embedded players for the video-only page.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// AKENO STREAM's video-only player (the "watch" page, src/web/pages.cpp)
// shows one video from a service that publishes an embedding interface for
// exactly this: YouTube's IFrame Player API and PeerTube's Embed API
// (https://docs.joinpeertube.org/api/embed-player). The video plays in the
// service's own player inside a frame; AKENO STREAM only sends it play,
// pause and seek and reads its state. Nothing is extracted from it, and no
// sign-in, DRM, region or frame restriction is worked around: a service that
// does not allow its player in other pages gets a clear error instead.
//
// These helpers decide what counts as such an embed. They accept only the
// documented address forms and strictly validated hosts and IDs, because a
// PeerTube origin ends up in the page's Content-Security-Policy header.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace akeno::web
{
enum class EmbedKind : std::uint8_t
{
    none,
    youtube,  // YouTube IFrame Player API
    peertube, // PeerTube Embed API (any instance)
};
const char *embed_kind_id(EmbedKind kind) noexcept;    // "youtube", "peertube"
const char *embed_kind_label(EmbedKind kind) noexcept; // "YouTube", "PeerTube"

struct EmbedTarget
{
    EmbedKind kind = EmbedKind::none;
    std::string host;     // PeerTube: "video.blender.org" (lower case, optional ":port")
    std::string video_id; // YouTube: 11 characters; PeerTube: UUID or short UUID
    int start_seconds = 0;

    [[nodiscard]] bool valid() const noexcept;
    // The origin the player's frame comes from: "https://video.blender.org",
    // "https://www.youtube.com".
    [[nodiscard]] std::string origin() const;
    // The service's own page for the video (a website fallback, a QR code).
    [[nodiscard]] std::string page_url() const;
};

// True for a DNS name (or IPv4 address) with an optional port, nothing else:
// letters, digits, '-', '.', at most 253 characters, no leading or trailing dot.
bool valid_embed_host(std::string_view host) noexcept;
// PeerTube video IDs: a UUID (8-4-4-4-12 hex) or a short UUID (base58).
bool valid_peertube_id(std::string_view id) noexcept;

// Recognises the documented embed and watch addresses:
//   YouTube   watch, youtu.be, embed, shorts and youtube-nocookie links with a
//             video ID (playlists only are not a single video);
//   PeerTube  https://<host>/videos/embed/<id>, /videos/watch/<id>, /w/<id>.
// ?start= / ?t= become start_seconds. Anything else: nullopt.
std::optional<EmbedTarget> parse_embed(std::string_view address);
// AKENO's PeerTube provider refers to a video as "<uuid>@<host>".
std::optional<EmbedTarget> peertube_embed_from_ref(std::string_view source_ref);

// The query of AKENO's watch page for a target ("p=peertube&h=...&v=...&t=...").
std::string watch_query(const EmbedTarget &target);

// Whether a page with these response headers may be shown inside a frame of
// embedder_origin ("http://127.0.0.1:8095"), from X-Frame-Options and the
// frame-ancestors directive of Content-Security-Policy. "unknown" when the
// headers do not say (browsers then allow it).
enum class FramePolicy : std::uint8_t
{
    allowed,
    blocked,
    unknown,
};
struct FrameCheck
{
    FramePolicy policy = FramePolicy::unknown;
    std::string detail; // one line: what decided it
};
FrameCheck frame_policy(std::string_view x_frame_options, std::string_view content_security_policy,
                        std::string_view embed_origin, std::string_view embedder_origin);
} // namespace akeno::web
