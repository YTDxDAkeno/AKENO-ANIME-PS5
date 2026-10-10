// AKENO STREAM PS5 - PeerTube, the open federated video platform.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "providers/peertube.hpp"

#include "core/json.hpp"
#include "core/url.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace akeno
{
namespace
{
constexpr std::uint64_t kHomeTtlSeconds = 20 * 60;
constexpr char kService[] = "PeerTube";

// PeerTube video categories (server/core/initializers/constants.ts).
struct Row
{
    const char *title;
    const char *category; // empty: every category
};
constexpr Row kRows[] = {
    {"Latest on PeerTube", ""},     {"Films", "2"}, {"Art & Animation", "4"},
    {"Science & Technology", "15"}, {"Kids", "17"},
};

// Well-known instances whose own channels are shown as rows (documented
// instance API GET /api/v1/videos): open films, education, open source.
struct Instance
{
    const char *title;
    const char *host;
};
constexpr Instance kInstances[] = {
    {"Blender Studio (video.blender.org)", "video.blender.org"},
    {"Framatube", "framatube.org"},
    {"TILvids - educational", "tilvids.com"},
};

std::string absolute(const std::string &host, const std::string &path)
{
    if (path.empty() || path.rfind("http", 0) == 0)
        return path;
    return "https://" + host + (path.front() == '/' ? "" : "/") + path;
}

std::string host_of(const std::string &address)
{
    const auto parsed = url::parse(address);
    return parsed ? parsed->host : std::string{};
}

MediaItem item_from(const json::Value &v)
{
    MediaItem m;
    m.provider = "peertube";
    const std::string uuid = v["uuid"].str();
    std::string host = v["account"]["host"].str(v["channel"]["host"].str());
    if (host.empty())
        host = host_of(v["url"].str());
    if (uuid.empty() || host.empty())
        return m;
    m.id = uuid + "@" + host;
    m.kind = ItemKind::video;
    m.title = v["name"].str();
    const std::string channel = v["channel"]["displayName"].str(v["account"]["displayName"].str());
    m.subtitle = channel.empty() ? host : channel + " - " + host;
    m.description = v["description"].str();
    m.image_url = v["thumbnailUrl"].str();
    if (m.image_url.empty())
        m.image_url = absolute(host, v["thumbnailPath"].str());
    if (!v["previewUrl"].str().empty())
        m.banner_url = v["previewUrl"].str();
    else if (!v["previewPath"].str().empty())
        m.banner_url = absolute(host, v["previewPath"].str());
    m.duration = v["duration"].num();
    if (v["isLive"].boolean())
        m.badge = "LIVE";
    else if (m.duration > 0.0)
        m.badge = format_clock(m.duration);
    const std::string date = v["publishedAt"].str();
    m.meta = (date.size() >= 10 ? date.substr(0, 10) : std::string{}) +
             (v["views"].integer() > 0
                  ? (date.empty() ? "" : " - ") + format_count(v["views"].integer()) + " views"
                  : std::string{});
    m.external_url = v["url"].str();
    if (m.external_url.empty())
        m.external_url = "https://" + host + "/w/" + uuid;
    m.external_label = "Open on phone";
    m.attribution = "PeerTube - " + host;
    m.accent = 0xf1680d;
    return m;
}
} // namespace

PeerTube::PeerTube(std::string index, bool with_instances)
    : index_{std::move(index)}, with_instances_{with_instances}
{
}

ProviderInfo PeerTube::info() const
{
    ProviderInfo info;
    info.id = "peertube";
    info.name = "PeerTube";
    info.tagline = "Videos from the open, federated PeerTube network, played in the app.";
    info.attribution = "Search by Sepia Search (Framasoft). Videos belong to their creators on "
                       "each PeerTube instance.";
    info.capabilities = {
        {"Browse & search", Support::available,
         "Through Sepia Search, the PeerTube search index. Sensitive content is excluded."},
        {"Playback", Support::available,
         "Videos stream from their PeerTube instance (HLS or MP4, no DRM) through the PS5 "
         "hardware decoder."},
        {"Accounts", Support::unavailable, "Signing in to an instance is not part of this build."},
    };
    return info;
}

bool PeerTube::split_id(const std::string &id, std::string *uuid, std::string *host)
{
    const std::size_t at = id.find('@');
    if (at == std::string::npos || at == 0 || at + 1 >= id.size())
        return false;
    *uuid = id.substr(0, at);
    *host = id.substr(at + 1);
    // The host comes from a search index: accept only plain host names.
    return std::all_of(host->begin(), host->end(),
                       [](char c) {
                           return std::isalnum(static_cast<unsigned char>(c)) || c == '.' ||
                                  c == '-' || c == ':';
                       }) &&
           std::all_of(uuid->begin(), uuid->end(), [](char c)
                       { return std::isalnum(static_cast<unsigned char>(c)) || c == '-'; });
}

std::vector<MediaItem> PeerTube::parse_search(const std::string &body, std::string *error)
{
    std::vector<MediaItem> out;
    const auto parsed = json::parse(body);
    if (!parsed.ok || !parsed.value["data"].is_array())
    {
        if (error)
            *error = "Unexpected answer from the PeerTube search index";
        return out;
    }
    for (const auto &v : parsed.value["data"].items())
    {
        if (v["nsfw"].boolean())
            continue;
        MediaItem m = item_from(v);
        if (!m.id.empty() && !m.title.empty())
            out.push_back(std::move(m));
    }
    return out;
}

bool PeerTube::parse_video(const std::string &body, int max_height, MediaItem *item,
                           std::string *error)
{
    const auto parsed = json::parse(body);
    const json::Value &v = parsed.value;
    if (!parsed.ok || !v.is_object() || v["uuid"].str().empty())
    {
        *error = "Unexpected answer from the PeerTube instance";
        return false;
    }
    if (v["nsfw"].boolean())
    {
        *error = "This video is marked as sensitive and is not shown here";
        return false;
    }
    const std::string host = host_of(v["url"].str()).empty()
                                 ? item->id.substr(item->id.find('@') + 1)
                                 : host_of(v["url"].str());
    MediaItem m = item_from(v);
    if (m.id.empty())
        m = *item;
    m.id = item->id; // keep the key used for history and favourites
    if (!v["description"].str().empty())
        m.description = v["description"].str();
    m.genres.clear();
    for (const char *key : {"category", "language", "licence"})
        if (!v[key]["label"].str().empty() && v[key]["label"].str() != "Unknown")
            m.genres.push_back(v[key]["label"].str());

    // HLS first (adaptive; fragmented MP4, sometimes with separate audio).
    for (const auto &playlist : v["streamingPlaylists"].items())
    {
        const std::string address = playlist["playlistUrl"].str();
        if (!address.empty() && url::parse(address))
        {
            m.playable = Playable{media::SourceKind::hls, address};
            break;
        }
    }
    // Otherwise the best plain MP4 file within the quality limit.
    if (!m.playable)
    {
        const json::Value *best = nullptr;
        long long best_height = -1;
        for (const auto &file : v["files"].items())
        {
            const long long height = file["resolution"]["id"].integer();
            const std::string address = file["fileUrl"].str();
            if (address.empty() || height <= 0 || !file["hasVideo"].boolean(true))
                continue;
            const bool fits = height <= max_height;
            const bool best_fits = best_height >= 0 && best_height <= max_height;
            if (!best || (fits && (!best_fits || height > best_height)) ||
                (!fits && !best_fits && height < best_height))
            {
                best = &file;
                best_height = height;
            }
        }
        if (best)
            m.playable = Playable{media::SourceKind::http_file, (*best)["fileUrl"].str()};
    }
    if (!m.playable)
    {
        *error = v["isLive"].boolean() ? "This live stream is not running at the moment"
                                       : "This video has no playable file yet (it may still be "
                                         "processing on " +
                                             host + ")";
        *item = std::move(m);
        return false;
    }
    *item = std::move(m);
    return true;
}

ItemsResult PeerTube::query(std::vector<std::pair<std::string, std::string>> params,
                            const net::CancelFlag &cancel)
{
    ItemsResult result;
    params.emplace_back("nsfw", "false");
    const Fetched f =
        fetch_text(index_ + "/api/v1/search/videos?" + url::build_query(params), cancel, kService);
    if (!f.ok)
    {
        result.error = f.error;
        return result;
    }
    std::string error;
    result.items = parse_search(f.body, &error);
    result.ok = error.empty();
    result.error = error;
    return result;
}

ShelvesResult PeerTube::home(const net::CancelFlag &cancel)
{
    const std::uint64_t now = platform::monotonic_us() / 1000000u;
    {
        std::lock_guard<std::mutex> guard(cache_lock_);
        if (home_cache_.ok && now - home_cached_at_ < kHomeTtlSeconds)
            return home_cache_;
    }
    ShelvesResult result;
    std::string first_error;
    for (const Row &row : kRows)
    {
        if (cancel && cancel->load())
            break;
        std::vector<std::pair<std::string, std::string>> params{
            {"start", "0"}, {"count", "20"}, {"sort", "-publishedAt"}, {"durationMin", "60"}};
        if (*row.category)
            params.emplace_back("categoryOneOf", row.category);
        ItemsResult items = query(std::move(params), cancel);
        if (!items.ok)
        {
            if (first_error.empty())
                first_error = items.error;
            continue;
        }
        if (!items.items.empty())
            result.shelves.push_back({row.title, std::move(items.items), false});
    }
    for (const Instance &instance : kInstances)
    {
        if (!with_instances_)
            break;
        if (cancel && cancel->load())
            break;
        const Fetched f = fetch_text(std::string{"https://"} + instance.host + "/api/v1/videos?" +
                                         url::build_query({{"sort", "-publishedAt"},
                                                           {"count", "20"},
                                                           {"nsfw", "false"},
                                                           {"isLocal", "true"}}),
                                     cancel, instance.host);
        if (!f.ok)
        {
            if (first_error.empty())
                first_error = f.error;
            continue;
        }
        auto items = parse_search(f.body, nullptr);
        if (!items.empty())
            result.shelves.push_back({instance.title, std::move(items), false});
    }
    result.ok = !result.shelves.empty();
    if (!result.ok)
        result.error = first_error.empty() ? "PeerTube returned no videos" : first_error;
    if (result.ok)
    {
        std::lock_guard<std::mutex> guard(cache_lock_);
        home_cache_ = result;
        home_cached_at_ = now;
    }
    return result;
}

ItemsResult PeerTube::search(const std::string &query_text, const net::CancelFlag &cancel)
{
    return query({{"search", query_text}, {"start", "0"}, {"count", "30"}, {"sort", "-match"}},
                 cancel);
}

DetailsResult PeerTube::details(const MediaItem &item, const net::CancelFlag &cancel)
{
    DetailsResult result;
    result.item = item;
    std::string uuid, host;
    if (!split_id(item.id, &uuid, &host))
    {
        result.error = "Unknown PeerTube video";
        return result;
    }
    const Fetched f = fetch_text("https://" + host + "/api/v1/videos/" + uuid, cancel, host);
    if (!f.ok)
    {
        result.error = f.status == 404 ? "This video was removed from " + host : f.error;
        return result;
    }
    std::string error;
    MediaItem detailed = item;
    if (!parse_video(f.body, max_height_, &detailed, &error))
    {
        result.item = std::move(detailed);
        result.error = error;
        return result;
    }
    result.ok = true;
    result.item = std::move(detailed);
    return result;
}
} // namespace akeno
