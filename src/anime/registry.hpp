// AKENO STREAM PS5 - The anime episode providers on this console and what they offer.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// <data>/anime-providers.json lists the providers the user added (AKENO
// catalogues, PeerTube channels or playlists). Two more are built in: the
// AKENO sample series shipped in the app (assets/anime/sample) and, when
// present, the user's own library catalogue anime-catalog.json in the
// install folder (written over FTP, like websites.txt).
//
// Loading a provider and checking its media runs on a worker thread
// (load_provider, check_source); the results are installed on the UI thread.
// A series counts as available to watch only when at least one of its
// episodes was checked and found playable - a listing alone is not enough.
#pragma once

#include "anime/probe.hpp"
#include "anime/provider.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace akeno::anime
{
struct ProviderState
{
    ProviderConfig config;
    bool loaded = false;
    std::string error; // the last load failed: why
    std::vector<Series> series;
    std::vector<CatalogIssue> errors;
    std::vector<CatalogIssue> warnings;
    int checked = 0; // episodes probed while loading
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
    static constexpr std::size_t kMaxProviders = 30;
    static constexpr char kSampleId[] = "akeno-sample";
    static constexpr char kPersonalId[] = "personal";

    Registry(std::string data_dir, std::string app_dir);

    // Reads anime-providers.json and adds the built-in providers.
    void load();
    [[nodiscard]] const std::vector<ProviderConfig> &configs() const noexcept
    {
        return configs_;
    }
    [[nodiscard]] const ProviderConfig *config(std::string_view id) const;
    // False with a reason for a duplicate address, a full list or a bad id.
    bool add(ProviderConfig config, std::string *why);
    bool remove(std::string_view id); // built-in providers stay
    void set_anilist_id(std::string_view id, int anilist_id);

    // Worker thread: loads a provider and probes up to max_probes episodes
    // (the first of each series first) so availability is measured.
    static ProviderState load_provider(const ProviderConfig &config, const ProviderContext &context,
                                       const net::CancelFlag &cancel, int max_probes);
    // UI thread.
    void install(ProviderState state);
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
    void save();
    Series *find_mutable(std::string_view provider, std::string_view series);

    std::string data_dir_;
    std::string app_dir_;
    std::vector<ProviderConfig> configs_;
    std::vector<ProviderState> states_;
    std::string last_error_;
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
