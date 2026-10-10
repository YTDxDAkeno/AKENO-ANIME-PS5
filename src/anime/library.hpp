// AKENO STREAM PS5 - My Anime: watchlist, search history and episode progress.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// <data>/anime-library.json keeps the watchlist and the last searches. Episode
// progress lives in the app's watch history (history.json) under the
// provider-qualified episode key, so Resume, Continue Watching and Home all
// see it; it survives restarts and providers that disappear (their entries
// stay in history and say the series is no longer offered).
#pragma once

#include "anime/model.hpp"
#include "app/store.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace akeno::anime
{
struct WatchlistEntry
{
    std::string provider_id; // empty for an AniList-only title
    std::string series_id;
    int anilist_id = 0;
    std::string title;
    std::string poster;
    std::uint64_t added = 0;

    [[nodiscard]] std::string key() const;
};

class Library final
{
  public:
    static constexpr std::size_t kMaxWatchlist = 300;
    static constexpr std::size_t kMaxSearches = 12;

    explicit Library(std::string directory);
    void load();

    [[nodiscard]] const std::vector<WatchlistEntry> &watchlist() const noexcept
    {
        return watchlist_;
    }
    [[nodiscard]] bool in_watchlist(const WatchlistEntry &entry) const;
    // Returns true when the entry is now on the watchlist.
    bool toggle_watchlist(WatchlistEntry entry);

    [[nodiscard]] const std::vector<std::string> &searches() const noexcept
    {
        return searches_;
    }
    void remember_search(std::string_view query);
    void clear_searches();

    [[nodiscard]] const std::string &last_error() const noexcept
    {
        return last_error_;
    }

  private:
    void save();
    std::string directory_;
    std::vector<WatchlistEntry> watchlist_;
    std::vector<std::string> searches_;
    std::string last_error_;
};

// Progress of one episode from the watch history.
struct EpisodeProgress
{
    double position = 0.0;
    double duration = 0.0;
    bool watched = false; // finished
    bool started = false;
};
EpisodeProgress progress_of(const Store &store, const std::string &episode_key);

// The episode to offer first: the one in progress, else the one after the
// last watched, else the first playable one.
struct NextUp
{
    EpisodeRef ref;
    bool resume = false;
    double position = 0.0;
};
std::optional<NextUp> next_up(const Series &series, const Store &store);
} // namespace akeno::anime
