// AKENO STREAM PS5 - The anime episode providers on this console and what they offer.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// <data>/anime-providers.json lists the catalogues and PeerTube channels or
// playlists the user added; <data>/anime-website-providers.json lists the
// website providers (anime/provider_store.hpp explains why they are apart).
// Two more are built in: the AKENO sample series shipped in the app
// (assets/anime/sample) and, when present, the user's own library catalogue
// anime-catalog.json in the install folder (written over FTP, like
// websites.txt).
//
// Loading a provider and checking its media runs on a worker thread
// (load_provider, check_source); the results are installed on the UI thread.
// A series counts as available to watch only when at least one of its
// episodes was checked and found playable - a listing alone is not enough.
#pragma once

#include "anime/probe.hpp"
#include "anime/provider.hpp"
#include "anime/provider_store.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace akeno::anime
{
// An episode that played in AKENO STREAM's own player on this device - about a
// second of pictures presented and half a second of sound, no decoder or audio
// errors - is what makes a provider "Native playback verified". Only the App
// writes one, from what the player measured; a check of a site never does.
// Kept per provider in <data>/anime-native-playback.json (the newest replaces
// the last; removing the provider removes it).
struct NativePlayback
{
    std::uint64_t at = 0; // Unix seconds
    std::string episode;  // "Series - S1 E2 - Title"
    std::string decoder;  // "PS5 hardware decoder (Videodec2)"
    std::string video;    // "h264 1280x720"
    std::string audio;    // "aac 48000 Hz 2 ch"
    std::uint64_t frames = 0;
    std::uint64_t audio_frames = 0;
};
inline constexpr char kNativePlaybackFile[] = "anime-native-playback.json";

struct ProviderState
{
    ProviderConfig config;
    bool loaded = false;
    std::string error;        // the last load failed: why
    bool unreachable = false; // ... because the source did not answer (Temporarily unavailable)
    std::vector<Series> series;
    std::vector<CatalogIssue> errors;
    std::vector<CatalogIssue> warnings;
    int checked = 0; // episodes probed while loading
    // Website providers: the next catalogue page (empty: no more) and the
    // catalogue's size when the site says so.
    std::string next_page;
    int total = -1;
};

struct SearchFilters
{
    int year = 0;       // 0: any
    std::string genre;  // "": any
    std::string status; // "finished" / "releasing" / "upcoming"; "": any
    enum class Availability : std::uint8_t
    {
        all,
        playable,  // series with an episode AKENO can play
        catalogue, // titles without one
    } availability = Availability::all;
};

struct SeriesHit
{
    const Series *series = nullptr;
    int score = 0;
};

class Registry final
{
  public:
    // Built-in providers and anime-providers.json together (as in 1.0.0).
    static constexpr std::size_t kMaxProviders = 30;
    static constexpr std::size_t kMaxWebsiteProviders = 30;
    static constexpr char kSampleId[] = "akeno-sample";
    static constexpr char kPersonalId[] = "personal";

    Registry(std::string data_dir, std::string app_dir);

    // Adds the built-in providers and reads both provider files.
    void load();
    [[nodiscard]] const std::vector<ProviderConfig> &configs() const noexcept
    {
        return configs_;
    }
    [[nodiscard]] const ProviderConfig *config(std::string_view id) const;
    // The provider at this address (or the address it led to), if any.
    [[nodiscard]] const ProviderConfig *find_by_address(std::string_view address) const;
    // False with a reason for a duplicate address, a full list or a file this
    // version may not write; *existing receives the id of the provider that
    // already has the address. *added receives the new provider's id.
    bool add(ProviderConfig config, std::string *why, std::string *existing = nullptr,
             std::string *added = nullptr);
    bool remove(std::string_view id); // built-in providers stay
    void set_anilist_id(std::string_view id, int anilist_id);
    // Website providers: keep one without loading it, or load it again.
    bool set_enabled(std::string_view id, bool enabled);
    // Website providers: what the last check of the catalogue found.
    void set_summary(std::string_view id, int series, int episodes);
    // Website providers: what the last full check (Add, Check Again) found
    // their episodes do in AKENO STREAM. "Not checked", "temporarily
    // unavailable" and "verified" are never saved: the first two say nothing
    // about what the site offers, the last is derived from native playback.
    void set_playback(std::string_view id, PlaybackClass playback);
    // What a provider's episodes do in AKENO STREAM now: an episode checked
    // and found playable wins; a website's unopened series keep what its last
    // check found; a known website (Miruro) is website-only unless its pages
    // named a video that played; a provider that did not answer its last load
    // is temporarily unavailable; one that plays natively and has played on
    // this device is verified.
    [[nodiscard]] PlaybackClass playback_of(std::string_view id) const;
    // UI thread: an episode of this provider played natively (see NativePlayback).
    void record_native_playback(std::string_view id, NativePlayback record);
    [[nodiscard]] const NativePlayback *native_playback(std::string_view id) const;
    // Entries the provider files hold but this version does not load, and
    // other problems found while reading them (one sentence each).
    [[nodiscard]] const std::vector<std::string> &storage_problems() const noexcept
    {
        return problems_;
    }

    // Worker thread: loads a provider and probes up to max_probes episodes
    // (the first of each series first) so availability is measured.
    static ProviderState load_provider(const ProviderConfig &config, const ProviderContext &context,
                                       const net::CancelFlag &cancel, int max_probes);
    // Worker thread: checks up to max_probes episodes of one series (the first
    // of each season first). Returns how many were checked.
    static int check_series(const ProviderConfig &config, const ProviderContext &context,
                            Series &series, const net::CancelFlag &cancel, int max_probes);
    // UI thread.
    void install(ProviderState state);
    // Website providers: entries from a later catalogue page or a search join
    // the provider's list (entries already there stay as they are); next_page,
    // when given, is where the catalogue continues.
    void merge_series(std::string_view provider, std::vector<Series> series,
                      const std::string *next_page);
    // Website providers: a series whose seasons and episodes were loaded.
    void install_series(std::string_view provider, Series series);
    [[nodiscard]] const std::vector<ProviderState> &states() const noexcept
    {
        return states_;
    }
    [[nodiscard]] const ProviderState *state(std::string_view id) const;

    [[nodiscard]] const Series *find_series(std::string_view provider,
                                            std::string_view series) const;
    // Series mapped explicitly to an AniList entry (the series or a season).
    [[nodiscard]] std::vector<const Series *> series_for_anilist(int anilist_id) const;
    [[nodiscard]] std::vector<const Series *> all_series() const;
    // Series with at least one episode checked and found playable.
    [[nodiscard]] std::vector<const Series *> available() const;
    // Newest "updated" first (series without a date last).
    [[nodiscard]] std::vector<const Series *> recently_updated(std::size_t limit) const;
    [[nodiscard]] std::vector<SeriesHit> search(std::string_view query,
                                                const SearchFilters &filters) const;
    // Suggestions for a partial title: provider titles that start with it.
    [[nodiscard]] std::vector<std::string> suggestions(std::string_view prefix,
                                                       std::size_t limit) const;

    // Records what checking or playing an episode found.
    void set_availability(std::string_view provider, std::string_view series,
                          std::string_view season, std::string_view episode, Availability a,
                          const std::string &detail);

    ProviderContext context;

    [[nodiscard]] const std::string &last_error() const noexcept
    {
        return last_error_;
    }

  private:
    // Writes the file that holds this kind of provider, and only that one.
    bool save(ProviderType type, std::string *why = nullptr);
    [[nodiscard]] bool id_taken(const std::string &id) const;
    [[nodiscard]] std::size_t count_of(bool websites) const;
    Series *find_mutable(std::string_view provider, std::string_view series);

    std::string data_dir_;
    std::string app_dir_;
    ProviderFile catalogs_file_;
    ProviderFile websites_file_;
    std::vector<ProviderConfig> configs_;
    std::vector<ProviderState> states_;
    std::vector<std::string> problems_;
    std::string last_error_;
    std::map<std::string, NativePlayback, std::less<>> native_;
    void load_native_playback();
    void save_native_playback() const;
};

// Worker thread: what an address really is, for "Add Streaming Provider" and
// the Websites check. Loads it as a provider when it is one and probes up to
// max_probes episodes. *config receives the provider to save when importable.
ProviderReport check_source(const std::string &address, const std::string &name,
                            const ProviderContext &context, const net::CancelFlag &cancel,
                            ProviderConfig *config, int max_probes = 12);
// Counts and the verdict for a loaded provider (exposed for tests).
ProviderReport report_for(const ProviderState &state, SourceClass base);
} // namespace akeno::anime
