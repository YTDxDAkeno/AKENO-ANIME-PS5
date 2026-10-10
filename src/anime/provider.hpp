// AKENO STREAM PS5 - Catalogue, episode and playback providers for anime.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Three separate roles:
//   CatalogProvider  - metadata: titles, artwork, descriptions, seasons,
//                      episode metadata, genres, ratings, search (AniList).
//   EpisodeProvider  - which series, seasons and episodes a source really
//                      offers, with its own episode identifiers, languages,
//                      subtitles, regions and restrictions.
//   PlaybackProvider - turns one episode into an authorized, playable
//                      resource for AKENO's native player, or says why not
//                      (DRM, sign-in, region, missing, unsupported).
// A source can take several roles (an AKENO catalogue takes the last two).
// Series from different providers are linked only by explicit ids (an
// AniList id in the catalogue), never by guessing from similar titles.
#pragma once

#include "anime/catalog_format.hpp"
#include "anime/model.hpp"
#include "net/http.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace akeno::anime
{
enum class ProviderType : std::uint8_t
{
    bundled,  // the AKENO sample series shipped in the app (offline)
    catalog,  // an AKENO catalogue (JSON) from the web or console storage
    peertube, // a PeerTube channel (playlists = seasons) or one playlist
};
const char *provider_type_id(ProviderType t) noexcept;
const char *provider_type_label(ProviderType t) noexcept;
ProviderType provider_type_from_id(std::string_view id) noexcept;

struct ProviderConfig
{
    std::string id;
    std::string name;
    ProviderType type = ProviderType::catalog;
    std::string address;     // URL, or an absolute console path for catalogues
    std::uint64_t added = 0; // Unix seconds
    int anilist_id = 0;      // PeerTube: the AniList entry this channel is (optional)
    bool builtin = false;    // shipped or found in the install folder; cannot be removed
};

// What a source turned out to be (Websites and the provider check use it).
enum class SourceClass : std::uint8_t
{
    unreachable,      // no answer
    web_page,         // a browsable website: a bookmark, not a provider
    media_file,       // one video file or stream (Sources / Play a Video Link)
    playlist,         // an M3U list: entries without season metadata (Sources)
    metadata,         // JSON data without playable episodes
    documented_api,   // a documented API that lists videos (PeerTube)
    native_provider,  // a catalogue or API with playable, DRM-free episodes
    protected_source, // only DRM-protected or sign-in-only episodes
};
const char *source_class_label(SourceClass c) noexcept;

struct ProviderReport
{
    SourceClass source = SourceClass::unreachable;
    std::string format; // "AKENO catalogue v1", "PeerTube channel", "Web page (HTML)"
    std::string name;   // what the source calls itself
    int series = 0;
    int seasons = 0;
    int episodes = 0;
    int with_media = 0; // episodes that name a resource
    int probed = 0;     // resources checked
    int playable = 0;   // ... and found playable
    int drm = 0;
    int auth = 0;
    int region = 0;
    int unsupported = 0;
    int missing = 0;
    int network = 0;
    std::vector<CatalogIssue> errors;
    std::vector<CatalogIssue> warnings;
    std::string verdict;     // one or two sentences
    bool importable = false; // a source the Anime mode can use
};

struct LoadedCatalog
{
    bool ok = false;
    std::string error;
    ProviderMeta meta;
    std::vector<Series> series;
    std::vector<CatalogIssue> errors;
    std::vector<CatalogIssue> warnings;
};

struct Resolution
{
    bool ok = false;
    MediaResource media;
    Availability availability = Availability::episodes_indexed;
    std::string detail;
};

struct ProviderContext
{
    std::string region;    // the console's region for regional restrictions
    int max_height = 1080; // PeerTube: the highest MP4 quality to pick
};

class EpisodeProvider
{
  public:
    virtual ~EpisodeProvider() = default;
    [[nodiscard]] virtual const ProviderConfig &config() const = 0;
    // Every series the source offers, with its seasons and episodes.
    virtual LoadedCatalog load(const net::CancelFlag &cancel) = 0;
};

class PlaybackProvider
{
  public:
    virtual ~PlaybackProvider() = default;
    // The resource to play for one episode. A catalogue returns what it lists;
    // an API source asks its server (PeerTube: the video's HLS or MP4 file).
    virtual Resolution resolve(const Series &series, const Season &season, const Episode &episode,
                               const net::CancelFlag &cancel) = 0;
};

class SourceProvider : public EpisodeProvider, public PlaybackProvider
{
};

std::unique_ptr<SourceProvider> make_provider(const ProviderConfig &config,
                                              const ProviderContext &context);

// Recognises PeerTube channel and playlist addresses:
//   https://host/c/<channel>  https://host/video-channels/<channel>
//   https://host/w/p/<playlist>  https://host/videos/watch/playlist/<playlist>
//   https://host/video-playlists/<playlist>
struct PeerTubeAddress
{
    bool ok = false;
    std::string host;
    std::string channel;  // channel handle
    std::string playlist; // playlist id, uuid or short uuid
};
PeerTubeAddress parse_peertube_address(const std::string &address);
} // namespace akeno::anime
