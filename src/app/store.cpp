// AKENO STREAM PS5 - Persistent settings, watch history and favourites.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/store.hpp"

#include "core/fs.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cctype>

namespace akeno
{
namespace
{
int clamp_height(long long h)
{
    if (h >= 1080)
        return 1080;
    if (h >= 720)
        return 720;
    return 480;
}
} // namespace

bool plausible_youtube_key(const std::string &key)
{
    if (key.size() != 39 || !key.starts_with("AIza"))
        return false;
    return std::all_of(
        key.begin(), key.end(),
        [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'; });
}

std::string extract_youtube_key(std::string_view text)
{
    const auto key_char = [](char c)
    { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'; };
    for (std::size_t at = text.find("AIza"); at != std::string_view::npos;
         at = text.find("AIza", at + 1))
    {
        if (at > 0 && key_char(text[at - 1]))
            continue; // part of a longer token
        std::size_t end = at;
        while (end < text.size() && key_char(text[end]))
            ++end;
        const std::string candidate{text.substr(at, end - at)};
        if (plausible_youtube_key(candidate))
            return candidate;
    }
    return {};
}

Store::Store(std::string directory) : directory_{std::move(directory)}
{
}

json::Value Store::load_json(const std::string &name)
{
    const std::string path = fs::join(directory_, name);
    const auto text = fs::read_text(path, 4u * 1024u * 1024u);
    if (!text)
        return {};
    const auto parsed = json::parse(*text);
    if (!parsed.ok || !parsed.value.is_object())
    {
        // Keep the damaged file for inspection and start again.
        const std::string backup = path + ".corrupt";
        (void)fs::write_atomic(backup, *text);
        (void)fs::remove_file(path);
        last_error_ = name + " was damaged and has been reset";
        return {};
    }
    return parsed.value;
}

void Store::save_json(const std::string &name, const json::Value &value)
{
    std::string error;
    if (!fs::write_atomic(fs::join(directory_, name), value.dump(true), &error))
        last_error_ = "could not save " + name + ": " + error;
}

void Store::load()
{
    (void)fs::make_directory(directory_);
    const json::Value s = load_json("settings.json");
    if (s.is_object())
    {
        settings_.last_mode = s["last_mode"].str(settings_.last_mode);
        settings_.max_height = clamp_height(s["max_height"].integer(settings_.max_height));
        settings_.volume =
            static_cast<int>(std::clamp<long long>(s["volume"].integer(100), 0, 100));
        settings_.resume_playback = s["resume_playback"].boolean(true);
        settings_.show_adult_anime = s["show_adult_anime"].boolean(false);
        settings_.youtube_region = s["youtube_region"].str("US");
        settings_.youtube_safe_search = s["youtube_safe_search"].str("moderate");
        settings_.reduce_motion = s["reduce_motion"].boolean(false);
        settings_.sources_notice_accepted = s["sources_notice_accepted"].boolean(false);
    }
    const json::Value h = load_json("history.json");
    history_.clear();
    for (const auto &e : h["entries"].items())
    {
        HistoryEntry entry;
        entry.item = MediaItem::from_json(e["item"]);
        if (entry.item.id.empty())
            continue;
        entry.position = std::max(0.0, e["position"].num());
        entry.duration = std::max(0.0, e["duration"].num());
        entry.updated = static_cast<std::uint64_t>(e["updated"].integer());
        entry.order = static_cast<std::uint64_t>(e["order"].integer());
        order_ = std::max(order_, entry.order);
        history_.push_back(std::move(entry));
        if (history_.size() >= kMaxHistory)
            break;
    }
    std::sort(history_.begin(), history_.end(),
              [](const HistoryEntry &a, const HistoryEntry &b) { return a.order > b.order; });
    const json::Value f = load_json("favorites.json");
    favorites_.clear();
    for (const auto &item : f["items"].items())
    {
        MediaItem m = MediaItem::from_json(item);
        if (!m.id.empty() && favorites_.size() < kMaxFavorites)
            favorites_.push_back(std::move(m));
    }
    sources_.clear();
    if (const auto text = fs::read_text(fs::join(directory_, "sources.json"), 1024u * 1024u))
    {
        std::string error;
        sources_ = parse_source_list(*text, &error);
        if (sources_.size() > kMaxSources)
            sources_.resize(kMaxSources);
        if (!error.empty())
            last_error_ = error;
    }
    const json::Value secrets = load_json("secrets.json");
    youtube_key_ = secrets["youtube_api_key"].str();
    if (!youtube_key_.empty() && !plausible_youtube_key(youtube_key_))
        youtube_key_.clear();
}

void Store::update_settings(const Settings &settings)
{
    settings_ = settings;
    settings_.max_height = clamp_height(settings_.max_height);
    settings_.volume = std::clamp(settings_.volume, 0, 100);
    save_settings();
}

void Store::save_settings()
{
    json::Value s = json::Value::object();
    s.set("version", Settings::kVersion);
    s.set("last_mode", settings_.last_mode);
    s.set("max_height", settings_.max_height);
    s.set("volume", settings_.volume);
    s.set("resume_playback", settings_.resume_playback);
    s.set("show_adult_anime", settings_.show_adult_anime);
    s.set("youtube_region", settings_.youtube_region);
    s.set("youtube_safe_search", settings_.youtube_safe_search);
    s.set("reduce_motion", settings_.reduce_motion);
    s.set("sources_notice_accepted", settings_.sources_notice_accepted);
    save_json("settings.json", s);
}

void Store::record_progress(const MediaItem &item, double position, double duration)
{
    if (item.id.empty())
        return;
    const std::string key = item.key();
    auto it = std::find_if(history_.begin(), history_.end(),
                           [&](const HistoryEntry &e) { return e.item.key() == key; });
    HistoryEntry entry;
    if (it != history_.end())
    {
        entry = *it;
        history_.erase(it);
    }
    entry.item = item;
    entry.duration = duration > 0.0 ? duration : entry.duration;
    entry.position = position < 10.0 ? 0.0 : position;
    if (entry.duration > 0.0 && position >= entry.duration - 15.0)
        entry.position = entry.duration; // finished
    entry.updated = platform::wall_clock_seconds();
    entry.order = ++order_;
    history_.insert(history_.begin(), std::move(entry));
    if (history_.size() > kMaxHistory)
        history_.resize(kMaxHistory);
    save_history();
}

double Store::resume_position(const std::string &key) const
{
    for (const auto &e : history_)
        if (e.item.key() == key)
            return e.finished() ? 0.0 : e.position;
    return 0.0;
}

void Store::remove_history(const std::string &key)
{
    history_.erase(std::remove_if(history_.begin(), history_.end(),
                                  [&](const HistoryEntry &e) { return e.item.key() == key; }),
                   history_.end());
    save_history();
}

void Store::clear_history()
{
    history_.clear();
    save_history();
}

std::vector<HistoryEntry> Store::continue_watching(std::size_t limit) const
{
    std::vector<HistoryEntry> out;
    for (const auto &e : history_)
    {
        if (e.position > 0.0 && !e.finished() && e.item.playable)
            out.push_back(e);
        if (out.size() >= limit)
            break;
    }
    return out;
}

void Store::save_history()
{
    json::Value root = json::Value::object();
    root.set("version", 1);
    json::Value entries = json::Value::array();
    for (const auto &e : history_)
    {
        json::Value v = json::Value::object();
        v.set("item", e.item.to_json());
        v.set("position", e.position);
        v.set("duration", e.duration);
        v.set("updated", static_cast<long long>(e.updated));
        v.set("order", static_cast<long long>(e.order));
        entries.push(v);
    }
    root.set("entries", entries);
    save_json("history.json", root);
}

bool Store::is_favorite(const std::string &key) const
{
    return std::any_of(favorites_.begin(), favorites_.end(),
                       [&](const MediaItem &m) { return m.key() == key; });
}

bool Store::toggle_favorite(const MediaItem &item)
{
    const std::string key = item.key();
    const auto it = std::find_if(favorites_.begin(), favorites_.end(),
                                 [&](const MediaItem &m) { return m.key() == key; });
    bool now_favorite;
    if (it != favorites_.end())
    {
        favorites_.erase(it);
        now_favorite = false;
    }
    else
    {
        favorites_.insert(favorites_.begin(), item);
        if (favorites_.size() > kMaxFavorites)
            favorites_.resize(kMaxFavorites);
        now_favorite = true;
    }
    save_favorites();
    return now_favorite;
}

void Store::save_favorites()
{
    json::Value root = json::Value::object();
    root.set("version", 1);
    json::Value items = json::Value::array();
    for (const auto &m : favorites_)
        items.push(m.to_json());
    root.set("items", items);
    save_json("favorites.json", root);
}

bool Store::add_source(SourceEntry entry)
{
    entry.from_install_folder = false;
    if (sources_.size() >= kMaxSources ||
        std::any_of(sources_.begin(), sources_.end(),
                    [&](const SourceEntry &s) { return s.url == entry.url; }))
        return false;
    sources_.push_back(std::move(entry));
    save_sources();
    return true;
}

void Store::remove_source(const std::string &url)
{
    sources_.erase(std::remove_if(sources_.begin(), sources_.end(),
                                  [&](const SourceEntry &s) { return s.url == url; }),
                   sources_.end());
    save_sources();
}

void Store::save_sources()
{
    std::string error;
    if (!fs::write_atomic(fs::join(directory_, "sources.json"), dump_source_list(sources_), &error))
        last_error_ = "could not save sources.json: " + error;
}

void Store::set_youtube_api_key(const std::string &key)
{
    youtube_key_ = key;
    save_secrets();
}

void Store::save_secrets()
{
    json::Value root = json::Value::object();
    root.set("version", 1);
    if (!youtube_key_.empty())
        root.set("youtube_api_key", youtube_key_);
    save_json("secrets.json", root);
}
} // namespace akeno
