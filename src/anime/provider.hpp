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
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace akeno::anime
{
enum class ProviderType : std::uint8_t
{
    bundled,  // the AKENO sample series shipped in the app (offline)
    catalog,  // an AKENO catalogue (JSON) from the web or console storage
    peertube, // a PeerTube channel (playlists = seasons) or one playlist
    // A website read by one of AKENO's website adapters (anime/website.hpp).
    // Stored in anime-website-providers.json, never in anime-providers.json:
    // 1.0.0 reads that file and would rewrite an unknown type as "catalog".
    website,
};
const char *provider_type_id(ProviderType t) noexcept;
const char *provider_type_label(ProviderType t) noexcept;
// nullopt for a type this version does not know: such an entry is kept as it
// is and never reinterpreted as another type.
std::optional<ProviderType> provider_type_from_id(std::string_view id) noexcept;

// What a provider's episodes do in AKENO STREAM, shown wherever the provider
// appears (Add Streaming Provider, its card, its home, its series). Only
// "plays" and "partly plays" ever come with a Play button: a catalogue-only
// or website-only provider shows its series and details, never a Play button.
enum class PlaybackClass : std::uint8_t
{
    unknown,         // not checked yet, or its videos could not be reached
    native,          // the checked episodes play in AKENO STREAM's own player
    partial,         // some checked episodes play; others are protected or missing
    catalogue_only,  // series and details only: no episode names a video AKENO may play
    browser_only,    // a known website whose episodes play only on its own pages (PS5 browser)
    protected_media, // every checked episode needs DRM, a sign-in or another region
    unsupported,     // no adapter recognises it, or no permitted way in
};
const char *playback_class_id(PlaybackClass c) noexcept;    // "catalogue-only"
const char *playback_class_label(PlaybackClass c) noexcept; // "Catalogue only"
// One sentence: what the user can do with such a provider.
const char *playback_class_detail(PlaybackClass c) noexcept;
std::optional<PlaybackClass> playback_class_from_id(std::string_view id) noexcept;
// True when its episodes may have a Play button.
inline bool plays_natively(PlaybackClass c) noexcept
{
    return c == PlaybackClass::native || c == PlaybackClass::partial;
}

struct ProviderConfig
{
    std::string id;
    std::string name;
    ProviderType type = ProviderType::catalog;
    // URL, or an absolute console path for catalogues. A website provider's
    // homepage as it answered (after redirects).
    std::string address;
    std::uint64_t added = 0; // Unix seconds
    int anilist_id = 0;      // PeerTube: the AniList entry this channel is (optional)
    bool builtin = false;    // shipped or found in the install folder; cannot be removed

    // Website providers only.
    std::string original_url;                    // the address the user entered (normalised)
    std::string adapter;                         // the website adapter that reads it ("schema-org")
    int adapter_version = 0;                     // the adapter's format version when it was added
    bool enabled = true;                         // a disabled provider is kept but not loaded
    std::map<std::string, std::string> settings; // the adapter's own configuration
    // What the last check found (shown before the provider has loaded).
    int known_series = 0;
    int known_episodes = 0;
    std::uint64_t checked = 0;                       // Unix seconds of that check
    PlaybackClass playback = PlaybackClass::unknown; // what that check found
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
    // What the loaded data offers ("Seasons", "Subtitles: en, ja", "AniList links"),
    // read from the series themselves; empty when nothing was loaded.
    std::vector<std::string> capabilities;
    std::vector<std::string> preview;     // the first series: "Title - 2 seasons, 5 episodes"
    std::vector<std::string> preview_art; // their posters (same order; may be empty strings)
    std::string verdict;                  // one or two sentences
    bool importable = false;              // a source the Anime mode can use
    // Website providers: the adapter that recognised the site and what it found.
    std::string adapter;               // "schema-org"; empty: not a website provider
    std::string adapter_label;         // "Structured data (schema.org)"
    std::vector<std::string> evidence; // "JSON-LD: an ItemList of 12 TVSeries"
    int total_series = -1;             // the whole catalogue, when the site says so
    std::string existing_id;           // the address is already this provider
    // What its episodes do in AKENO STREAM (classify_playback()).
    PlaybackClass playback = PlaybackClass::unknown;
    // A known website (anime/site_policy.hpp): its name and how to watch it in
    // the PS5 browser instead ("Turn on Autoplay in Miruro's player").
    std::string known_site;
    std::string browser_hint;
};

// The playback class a check found, from its counts alone: a page that loads
// or an HTTP 200 never counts as playback.
PlaybackClass classify_playback(const ProviderReport &report) noexcept;

struct LoadedCatalog
{
    bool ok = false;
    std::string error;
    ProviderMeta meta;
    std::vector<Series> series;
    std::vector<CatalogIssue> errors;
    std::vector<CatalogIssue> warnings;
    // Website providers: where the next catalogue page starts (empty: no more).
    std::string next_page;
    int total = -1; // series in the whole catalogue when the source says so
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
