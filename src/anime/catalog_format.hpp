// AKENO STREAM PS5 - The AKENO streaming catalogue format (JSON), parsed and validated.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A catalogue describes a provider's series, seasons and episodes and, per
// episode, the resource to play: a DRM-free MP4/HLS/TS/MKV address with its
// subtitles and audio languages, or the restriction that prevents playback
// (DRM, sign-in, regions). Documented in docs/ANIME_PROVIDERS.md, schema in
// docs/schema/akeno-catalog.schema.json, example in examples/anime-catalog/.
//
// Relative addresses resolve against the catalogue's own address. A catalogue
// on console storage may name files next to it (or absolute console paths);
// a catalogue from the web may only name http(s) addresses - never files on
// the console.
#pragma once

#include "anime/model.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace akeno::anime
{
inline constexpr int kCatalogVersion = 1;
inline constexpr std::size_t kMaxCatalogSeries = 500;
inline constexpr std::size_t kMaxSeasonsPerSeries = 60;
inline constexpr std::size_t kMaxEpisodesPerSeries = 3000;

struct CatalogIssue
{
    std::string path; // "series[0].seasons[1].episodes[3].media.url"
    std::string message;
};

struct ProviderMeta
{
    std::string id;
    std::string name;
    std::string description;
    std::string homepage;
    std::string licence;
    std::string attribution;
};

struct ParsedCatalog
{
    bool ok = false; // no errors; series may still be empty
    ProviderMeta provider;
    std::vector<Series> series;
    std::vector<CatalogIssue> errors;   // the catalogue (or an entry) is unusable
    std::vector<CatalogIssue> warnings; // usable, with a caveat
};

struct CatalogOptions
{
    std::string provider_id; // overrides the catalogue's own id when set
    std::string base;        // the catalogue's address: http(s) URL or absolute path
    std::string region;      // the console's region ("US"); empty: unknown
};

// True when the text looks like an AKENO catalogue at all ("akenoCatalog" key).
bool looks_like_catalog(std::string_view text);
ParsedCatalog parse_catalog(std::string_view text, const CatalogOptions &options);

// Resolves a catalogue reference to an http(s) URL or an absolute console
// path; empty (with *why) when the reference is not allowed.
std::string resolve_reference(const std::string &base, const std::string &reference,
                              std::string *why);
// The player kind for a resource: the declared type wins, then the extension.
media::SourceKind media_kind(const std::string &resolved, const std::string &declared_type);

// A stable id from a name: "My Anime Source!" -> "my-anime-source".
std::string slug(std::string_view text);
} // namespace akeno::anime
