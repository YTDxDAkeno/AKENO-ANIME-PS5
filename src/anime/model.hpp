// AKENO STREAM PS5 - Anime series, seasons and episodes, independent of any service.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// AniList supplies anime metadata; video comes from separate episode
// providers (an AKENO catalogue, a PeerTube channel, the bundled test series).
// These types carry what a provider actually knows: an episode without a
// title has an empty title, an episode without a playable resource has no
// media, and every episode states its availability and why. Identifiers are
// provider-qualified ("provider/series/season/episode") so watch progress
// never mixes two providers or two series with the same title.
#pragma once

#include "media/player.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace akeno::anime
{
// What AKENO STREAM can do with an episode (or a whole series, its best episode).
enum class Availability : std::uint8_t
{
    catalogue_only,       // the title is known; no provider lists its episodes
    episodes_indexed,     // the episode is listed, but no resource to play it
    unverified,           // a resource is listed; not checked yet
    playable,             // the resource was checked: DRM-free media AKENO plays
    auth_required,        // the provider needs a sign-in AKENO does not have
    drm_unsupported,      // protected with DRM: never played, never bypassed
    region_unavailable,   // not offered in the console's region
    network_error,        // the resource could not be reached just now
    provider_unsupported, // a format or kind of resource AKENO cannot play
    missing,              // the resource is gone (HTTP 404 / file not found)
};
const char *availability_id(Availability a) noexcept;
const char *availability_label(Availability a) noexcept; // "Playable", "DRM not supported"...
Availability availability_from_id(std::string_view id) noexcept;
// Ranks availabilities so a series shows its best episode's state.
int availability_rank(Availability a) noexcept;

struct SubtitleTrack
{
    std::string language; // BCP 47 ("en", "ja")
    std::string label;    // "English"
    std::string url;      // http(s) address or absolute console path
    std::string format;   // "vtt" or "srt"
};

struct AudioTrack
{
    std::string language;
    std::string label;
};

// An authorized playback resource as the provider supplied it.
struct MediaResource
{
    std::string url; // http(s) address or absolute console path
    media::SourceKind kind = media::SourceKind::automatic;
    std::string container; // "mp4", "hls", "ts", "mkv" when declared
    std::vector<AudioTrack> audio;
    std::vector<SubtitleTrack> subtitles;
    std::string drm;            // a DRM system the provider declares ("widevine"); empty: none
    bool requires_auth = false; // the provider needs a sign-in
    std::vector<std::string> regions; // ISO 3166 codes where it is offered (empty: everywhere)
};

struct Episode
{
    std::string id; // provider-local, stable
    int number = 0; // 1-based within its season; 0: unnumbered
    bool special = false;
    std::string title; // empty when the provider has none (never invented)
    std::string description;
    std::string thumbnail;
    double duration = 0.0; // seconds, 0: unknown
    std::string air_date;  // "YYYY-MM-DD" when known
    std::optional<MediaResource> media;
    // The provider's own identifier when the resource is looked up at play
    // time (PeerTube: "uuid@host"); empty for catalogues that list media.
    std::string source_ref;
    Availability availability = Availability::episodes_indexed;
    std::string availability_detail; // why, in one sentence
    std::string link;                // an official page (catalogue-only episodes)
    std::string link_label;          // "Crunchyroll"
};

struct Season
{
    std::string id;
    int number = 0; // 0: specials
    std::string title;
    int year = 0;
    int anilist_id = 0;    // the AniList entry of this season, when mapped explicitly
    int episode_count = 0; // announced count (catalogues may list fewer)
    std::vector<Episode> episodes;
};

struct Series
{
    std::string provider_id;
    std::string provider_name;
    std::string id;
    std::string title;
    std::vector<std::string> alt_titles; // romaji, native-in-Latin, synonyms
    std::string description;
    std::string poster; // portrait artwork
    std::string banner; // wide artwork
    std::vector<std::string> genres;
    int year = 0;
    std::string status; // "finished", "releasing", "upcoming"
    int score = 0;      // 0..100 when known
    int anilist_id = 0; // explicit cross-provider mapping (never guessed)
    int mal_id = 0;
    std::string licence;     // "CC BY 4.0", "Personal library"
    std::string attribution; // credit the provider asks for
    std::string updated;     // "YYYY-MM-DD" of the latest change
    std::vector<Season> seasons;
};

// "provider/series/season/episode": the key for progress and history.
std::string episode_key(std::string_view provider, std::string_view series, std::string_view season,
                        std::string_view episode);
std::string series_key(std::string_view provider, std::string_view series);

// Lower case, letters and digits only, single spaces: "Frieren: Beyond" ->
// "frieren beyond". Used for search, never to link series across providers.
std::string normalize_title(std::string_view text);
// 0 (no match) .. 100 (exact title). Every word of the query must start a
// word of the title (or an alternative title).
int title_match(const Series &series, std::string_view query);

// The series' best episode availability (playable beats unverified ...).
Availability series_availability(const Series &series);
// Episodes with a resource that is not known to be unplayable.
int playable_episodes(const Series &series);
int episode_total(const Series &series);

// The episode after (or before) one, across seasons; nullopt at the end.
struct EpisodeRef
{
    std::size_t season = 0;
    std::size_t episode = 0;
};
std::optional<EpisodeRef> next_episode(const Series &series, EpisodeRef from);
std::optional<EpisodeRef> previous_episode(const Series &series, EpisodeRef from);
std::optional<EpisodeRef> find_episode(const Series &series, std::string_view season_id,
                                       std::string_view episode_id);

// "S1 E3", "Special 2", "E12"
std::string episode_label(const Season &season, const Episode &episode);
} // namespace akeno::anime
