// AKENO STREAM PS5 - HLS playlist parsing and variant selection.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Supports RFC 8216 master and media playlists: MPEG-TS and fragmented-MP4
// (CMAF, EXT-X-MAP) segments, byte ranges, AES-128 full-segment encryption
// (the standard HLS key method, keys fetched like segments), audio renditions,
// VOD and live. Refused and reported, never worked around: SAMPLE-AES and
// every DRM key system (FairPlay, Widevine, PlayReady).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace akeno::hls
{
enum class Kind : std::uint8_t
{
    none,
    master,
    media,
};

struct Variant
{
    std::string uri; // absolute
    std::uint64_t bandwidth = 0;
    std::uint64_t average_bandwidth = 0;
    int width = 0, height = 0;
    double frame_rate = 0.0;
    std::string codecs;
    std::string audio_group;
    bool video_codec_supported = true; // false when CODECS names an unsupported video codec
    bool audio_codec_supported = true;
};

struct Rendition
{
    std::string type; // AUDIO, SUBTITLES, CLOSED-CAPTIONS, VIDEO
    std::string group;
    std::string name;
    std::string language;
    std::string uri; // absolute, empty when carried in the variant stream
    bool is_default = false;
    bool autoselect = false;
};

// Initialization section of fragmented-MP4 segments (EXT-X-MAP).
struct Map
{
    std::string uri;
    std::int64_t offset = -1; // byte range, -1 for the whole resource
    std::int64_t length = 0;
};

// AES-128 key (EXT-X-KEY METHOD=AES-128).
struct Key
{
    std::string uri;
    bool has_iv = false;
    std::uint8_t iv[16] = {};
};

struct Segment
{
    std::string uri; // absolute
    double duration = 0.0;
    double start = 0.0; // seconds from the first segment in this playlist
    std::uint64_t sequence = 0;
    bool discontinuity = false;
    int map = -1;             // index into Playlist::maps
    int key = -1;             // index into Playlist::keys
    std::int64_t offset = -1; // byte range, -1 for the whole resource
    std::int64_t length = 0;
};

struct Playlist
{
    Kind kind = Kind::none;
    std::vector<Variant> variants;
    std::vector<Rendition> renditions;

    double target_duration = 0.0;
    std::uint64_t media_sequence = 0;
    bool endlist = false;
    std::string playlist_type; // VOD, EVENT or empty
    std::vector<Segment> segments;
    std::vector<Map> maps;
    std::vector<Key> keys;
    double total_duration = 0.0;

    // Set when the playlist uses a feature this player cannot handle honestly.
    std::string unsupported;

    [[nodiscard]] bool is_live() const noexcept
    {
        return kind == Kind::media && !endlist;
    }
    // Fragmented MP4: segments need their initialization section.
    [[nodiscard]] bool fragmented_mp4() const noexcept
    {
        return !maps.empty();
    }
};

struct ParseResult
{
    Playlist playlist;
    bool ok = false;
    std::string error;
    std::size_t line = 0;
};

struct Limits
{
    std::size_t max_bytes = 4u * 1024u * 1024u;
    std::size_t max_line = 8192;
    std::size_t max_segments = 20000;
    std::size_t max_variants = 64;
};

ParseResult parse(std::string_view text, std::string_view base_url, const Limits &limits = {});

struct Selection
{
    int max_width = 1920;
    int max_height = 1080;
    std::uint64_t max_bandwidth = 0; // 0 = unlimited
};

// Chooses the best variant within limits whose video codec is playable,
// preferring ones whose audio codec is playable too. Returns -1 when none
// qualifies and writes the reason into *why.
int select_variant(const Playlist &playlist, const Selection &selection,
                   std::string *why = nullptr);

// The audio rendition to play with a variant: the default (else autoselect,
// else first) member of its AUDIO group that has its own URI. nullptr when the
// variant carries its audio itself.
const Rendition *audio_rendition(const Playlist &playlist, const Variant &variant);

// Index of the segment containing time t (seconds from playlist start).
std::size_t segment_at(const Playlist &playlist, double t) noexcept;

// Parses an attribute list such as BANDWIDTH=1,CODECS="a,b".
std::vector<std::pair<std::string, std::string>> parse_attributes(std::string_view list);
} // namespace akeno::hls
