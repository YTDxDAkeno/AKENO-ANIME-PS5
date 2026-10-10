// AKENO STREAM PS5 - My Anime: watchlist, search history and episode progress.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "anime/library.hpp"

#include "anime/session.hpp"
#include "core/fs.hpp"
#include "core/json.hpp"
#include "platform/platform.hpp"

#include <algorithm>

namespace akeno::anime
{
namespace
{
constexpr char kFile[] = "anime-library.json";
}

std::string WatchlistEntry::key() const
{
    if (!provider_id.empty())
        return series_key(provider_id, series_id);
    return "anilist/" + std::to_string(anilist_id);
}

Library::Library(std::string directory) : directory_{std::move(directory)}
{
}

void Library::load()
{
    watchlist_.clear();
    searches_.clear();
    const std::string path = fs::join(directory_, kFile);
    const auto text = fs::read_text(path, 2 * 1024 * 1024);
    if (!text)
        return;
    const auto parsed = json::parse(*text);
    if (!parsed.ok || !parsed.value.is_object())
    {
        last_error_ = std::string{kFile} + " is damaged; kept as " + kFile + ".corrupt";
        (void)fs::write_atomic(path + ".corrupt", *text);
        return;
    }
    for (const auto &w : parsed.value["watchlist"].items())
    {
        WatchlistEntry e;
        e.provider_id = w["provider"].str();
        e.series_id = w["series"].str();
        e.anilist_id = static_cast<int>(w["anilistId"].integer());
        e.title = w["title"].str();
        e.poster = w["poster"].str();
        e.added = static_cast<std::uint64_t>(w["added"].integer());
        if ((e.provider_id.empty() || e.series_id.empty()) && e.anilist_id <= 0)
            continue;
        if (!in_watchlist(e) && watchlist_.size() < kMaxWatchlist)
            watchlist_.push_back(std::move(e));
    }
    for (const auto &q : parsed.value["searches"].items())
        if (!q.str().empty() && searches_.size() < kMaxSearches)
            searches_.push_back(q.str().substr(0, 80));
}

void Library::save()
{
    json::Value root = json::Value::object();
    root.set("version", 1);
    json::Value list = json::Value::array();
    for (const WatchlistEntry &e : watchlist_)
    {
        json::Value w = json::Value::object();
        if (!e.provider_id.empty())
        {
            w.set("provider", e.provider_id);
            w.set("series", e.series_id);
        }
        if (e.anilist_id)
            w.set("anilistId", e.anilist_id);
        w.set("title", e.title);
        w.set("poster", e.poster);
        w.set("added", static_cast<long long>(e.added));
        list.push(std::move(w));
    }
    root.set("watchlist", std::move(list));
    json::Value searches = json::Value::array();
    for (const std::string &q : searches_)
        searches.push(q);
    root.set("searches", std::move(searches));
    std::string error;
    if (!fs::write_atomic(fs::join(directory_, kFile), root.dump(true), &error))
        last_error_ = "Could not save " + std::string{kFile} + ": " + error;
}

bool Library::in_watchlist(const WatchlistEntry &entry) const
{
    const std::string key = entry.key();
    return std::any_of(
        watchlist_.begin(), watchlist_.end(), [&](const WatchlistEntry &e)
        { return e.key() == key || (entry.anilist_id && e.anilist_id == entry.anilist_id); });
}

bool Library::toggle_watchlist(WatchlistEntry entry)
{
    const std::string key = entry.key();
    const auto it = std::find_if(
        watchlist_.begin(), watchlist_.end(), [&](const WatchlistEntry &e)
        { return e.key() == key || (entry.anilist_id && e.anilist_id == entry.anilist_id); });
    if (it != watchlist_.end())
    {
        watchlist_.erase(it);
        save();
        return false;
    }
    if (!entry.added)
        entry.added = platform::wall_clock_seconds();
    watchlist_.insert(watchlist_.begin(), std::move(entry));
    if (watchlist_.size() > kMaxWatchlist)
        watchlist_.resize(kMaxWatchlist);
    save();
    return true;
}

void Library::remember_search(std::string_view query)
{
    std::string q{query.substr(0, 80)};
    while (!q.empty() && q.back() == ' ')
        q.pop_back();
    while (!q.empty() && q.front() == ' ')
        q.erase(q.begin());
    if (q.empty())
        return;
    const std::string n = normalize_title(q);
    searches_.erase(std::remove_if(searches_.begin(), searches_.end(),
                                   [&n](const std::string &x) { return normalize_title(x) == n; }),
                    searches_.end());
    searches_.insert(searches_.begin(), q);
    if (searches_.size() > kMaxSearches)
        searches_.resize(kMaxSearches);
    save();
}

void Library::clear_searches()
{
    searches_.clear();
    save();
}

EpisodeProgress progress_of(const Store &store, const std::string &episode_key)
{
    EpisodeProgress p;
    const std::string key = std::string{kEpisodeItemProvider} + ":" + episode_key;
    for (const HistoryEntry &h : store.history())
        if (h.item.key() == key)
        {
            p.started = true;
            p.duration = h.duration;
            p.watched = h.finished();
            p.position = p.watched ? h.duration : h.position;
            break;
        }
    return p;
}

std::optional<NextUp> next_up(const Series &series, const Store &store)
{
    // Newest history entry of this series decides.
    const std::string prefix =
        std::string{kEpisodeItemProvider} + ":" + series_key(series.provider_id, series.id) + "/";
    for (const HistoryEntry &h : store.history())
    {
        const std::string key = h.item.key();
        if (key.rfind(prefix, 0) != 0)
            continue;
        const std::string rest = key.substr(prefix.size()); // season/episode
        const std::size_t slash = rest.find('/');
        if (slash == std::string::npos)
            continue;
        const auto ref = find_episode(series, rest.substr(0, slash), rest.substr(slash + 1));
        if (!ref)
            continue;
        if (!h.finished() && h.position > 0.0)
            return NextUp{*ref, true, h.position};
        // The next episode that can play; else simply the next one.
        const auto first_next = next_episode(series, *ref);
        for (auto next = first_next; next; next = next_episode(series, *next))
        {
            const Availability a =
                series.seasons[next->season].episodes[next->episode].availability;
            if (a == Availability::playable || a == Availability::unverified)
                return NextUp{*next, false, 0.0};
        }
        if (first_next)
            return NextUp{*first_next, false, 0.0};
        return NextUp{*ref, false, 0.0}; // the last one again
    }
    for (std::size_t s = 0; s < series.seasons.size(); ++s)
        for (std::size_t e = 0; e < series.seasons[s].episodes.size(); ++e)
        {
            const Episode &ep = series.seasons[s].episodes[e];
            if (ep.availability == Availability::playable ||
                ep.availability == Availability::unverified)
                return NextUp{{s, e}, false, 0.0};
        }
    return std::nullopt;
}
} // namespace akeno::anime
