// AKENO STREAM PS5 - Persistent settings, watch history and favourites.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Files under platform::data_dir() (/download0/akeno on the console):
//   settings.json   versioned preferences (no secrets)
//   history.json    resume positions, newest first
//   favorites.json  saved items
//   secrets.json    API keys entered by the user; never logged or exported
// A damaged file is renamed to *.corrupt and replaced with defaults.
#pragma once

#include "providers/model.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace akeno
{
struct Settings
{
    static constexpr int kVersion = 1;
    std::string last_mode = "home";
    int max_height = 1080; // 1080, 720 or 480
    int volume = 100;      // 0..100
    bool resume_playback = true;
    bool show_adult_anime = false; // AniList isAdult filter
    std::string youtube_region = "US";
    std::string youtube_safe_search = "moderate";
    bool reduce_motion = false;
};

struct HistoryEntry
{
    MediaItem item;
    double position = 0.0;
    double duration = 0.0;
    std::uint64_t updated = 0; // Unix seconds (0 when the clock is unknown)
    std::uint64_t order = 0;   // monotonic insertion counter

    [[nodiscard]] double progress() const noexcept
    {
        return duration > 0.0 ? (position / duration < 1.0 ? position / duration : 1.0) : 0.0;
    }
    [[nodiscard]] bool finished() const noexcept
    {
        return duration > 0.0 && position >= duration - 15.0;
    }
};

class Store final
{
  public:
    explicit Store(std::string directory);

    void load();
    [[nodiscard]] const std::string &directory() const noexcept
    {
        return directory_;
    }

    [[nodiscard]] const Settings &settings() const noexcept
    {
        return settings_;
    }
    void update_settings(const Settings &settings);

    [[nodiscard]] const std::vector<HistoryEntry> &history() const noexcept
    {
        return history_;
    }
    // Records a playback position; positions under 10 s or within the last
    // 15 s of a known duration clear the resume point but keep the entry.
    void record_progress(const MediaItem &item, double position, double duration);
    [[nodiscard]] double resume_position(const std::string &key) const;
    void remove_history(const std::string &key);
    void clear_history();
    // Unfinished entries with a resume point, newest first.
    [[nodiscard]] std::vector<HistoryEntry> continue_watching(std::size_t limit = 12) const;

    [[nodiscard]] const std::vector<MediaItem> &favorites() const noexcept
    {
        return favorites_;
    }
    [[nodiscard]] bool is_favorite(const std::string &key) const;
    // Returns true if the item is now a favourite.
    bool toggle_favorite(const MediaItem &item);

    [[nodiscard]] std::string youtube_api_key() const
    {
        return youtube_key_;
    }
    void set_youtube_api_key(const std::string &key);

    [[nodiscard]] const std::string &last_error() const noexcept
    {
        return last_error_;
    }

    static constexpr std::size_t kMaxHistory = 100;
    static constexpr std::size_t kMaxFavorites = 300;

  private:
    void save_settings();
    void save_history();
    void save_favorites();
    void save_secrets();
    json::Value load_json(const std::string &name);
    void save_json(const std::string &name, const json::Value &value);

    std::string directory_;
    Settings settings_;
    std::vector<HistoryEntry> history_;
    std::vector<MediaItem> favorites_;
    std::string youtube_key_;
    std::uint64_t order_ = 0;
    std::string last_error_;
};

// Validates a YouTube Data API key's shape (39 characters, "AIza" prefix).
bool plausible_youtube_key(const std::string &key);
} // namespace akeno
