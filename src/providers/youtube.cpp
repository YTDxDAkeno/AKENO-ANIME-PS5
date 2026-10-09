// AKENO STREAM PS5 - YouTube browsing through the official YouTube Data API v3.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "providers/youtube.hpp"

#include "core/url.hpp"
#include "platform/platform.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace akeno
{
namespace
{
constexpr char kApi[] = "https://www.googleapis.com/youtube/v3/";
constexpr std::uint64_t kHomeTtlSeconds = 20 * 60;

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
long long days_from_civil(long long y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

long long parse_rfc3339(const std::string &t)
{
    int y, mo, d, h, mi, s;
    if (std::sscanf(t.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &s) != 6)
        return 0;
    if (mo < 1 || mo > 12 || d < 1 || d > 31)
        return 0;
    return days_from_civil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400 +
           h * 3600 + mi * 60 + s;
}

std::string best_thumbnail(const json::Value &thumbnails)
{
    for (const char *size : {"high", "medium", "standard", "default"})
    {
        const std::string url = thumbnails[size]["url"].str();
        if (!url.empty())
            return url;
    }
    return {};
}

MediaItem video_item(const std::string &id, const json::Value &snippet, std::uint64_t now)
{
    MediaItem m;
    m.provider = "youtube";
    m.id = id;
    m.kind = ItemKind::video;
    m.title = snippet["title"].str();
    m.subtitle = snippet["channelTitle"].str();
    m.description = snippet["description"].str();
    m.image_url = best_thumbnail(snippet["thumbnails"]);
    m.external_url = "https://www.youtube.com/watch?v=" + id;
    m.external_label = "Watch on YouTube";
    m.attribution = "YouTube";
    m.meta = YouTube::relative_age(snippet["publishedAt"].str(), now);
    if (snippet["liveBroadcastContent"].str() == "live")
        m.badge = "LIVE";
    return m;
}
} // namespace

YouTube::YouTube(KeySource key, std::string region, std::string safe_search)
    : key_{std::move(key)}, region_{std::move(region)}, safe_search_{std::move(safe_search)}
{
}

ProviderInfo YouTube::info() const
{
    ProviderInfo info;
    info.id = "youtube";
    info.name = "YouTube";
    info.tagline = "Search and browse YouTube with the official YouTube Data API.";
    info.attribution = "Content and data from YouTube. YouTube is a trademark of Google LLC.";
    const bool have_key = configured();
    info.capabilities = {
        {"Browse & search", have_key ? Support::available : Support::needs_setup,
         have_key ? "Using your YouTube Data API v3 key. Searches cost 100 of the 10,000 daily "
                    "quota units."
                  : "Needs your own free YouTube Data API v3 key (Google Cloud Console). Add it in "
                    "Settings."},
        {"Video playback", Support::unavailable,
         "YouTube allows playback only in its official players (embedded web player or official "
         "apps). This "
         "native app has no compliant player and does not extract streams, so each video offers a "
         "QR code to "
         "watch it on your phone, or open the official YouTube app on PS5."},
        {"Sign-in & subscriptions", Support::unavailable,
         "Requires Google OAuth for TV devices with a registered client ID. Not configured in this "
         "build."},
    };
    return info;
}

std::string YouTube::relative_age(const std::string &rfc3339, std::uint64_t now)
{
    const long long t = parse_rfc3339(rfc3339);
    if (t <= 0 || now < 946684800 || static_cast<long long>(now) < t)
        return rfc3339.size() >= 10 ? rfc3339.substr(0, 10) : std::string{};
    const long long age = static_cast<long long>(now) - t;
    char text[48];
    const auto plural = [&](long long n, const char *unit)
    {
        std::snprintf(text, sizeof(text), "%lld %s%s ago", n, unit, n == 1 ? "" : "s");
        return std::string{text};
    };
    if (age < 3600)
        return plural(std::max(1LL, age / 60), "minute");
    if (age < 86400)
        return plural(age / 3600, "hour");
    if (age < 86400 * 31)
        return plural(age / 86400, "day");
    if (age < 86400 * 365)
        return plural(age / (86400 * 30), "month");
    return plural(age / (86400 * 365), "year");
}

std::string YouTube::parse_error(const std::string &body, long status)
{
    const auto parsed = json::parse(body);
    const json::Value &error = parsed.value["error"];
    const std::string reason = error["errors"][0]["reason"].str();
    if (reason == "quotaExceeded" || reason == "dailyLimitExceeded")
        return "Your YouTube API key has used up today's quota. It resets at midnight Pacific "
               "Time.";
    if (reason == "keyInvalid" ||
        (reason == "badRequest" && error["message"].str().find("API key") != std::string::npos))
        return "The YouTube API key is not valid. Check it in Settings.";
    if (reason == "accessNotConfigured" || reason == "SERVICE_DISABLED")
        return "The YouTube Data API v3 is not enabled for this key's Google Cloud project.";
    if (reason == "keyExpired")
        return "The YouTube API key has expired. Create a new one in Google Cloud Console.";
    if (!error["message"].str().empty())
        return "YouTube: " + error["message"].str();
    return status ? "YouTube request failed (HTTP " + std::to_string(status) + ")"
                  : "YouTube request failed";
}

YouTube::Fetch YouTube::get(const std::string &path,
                            std::vector<std::pair<std::string, std::string>> params,
                            const net::CancelFlag &cancel)
{
    Fetch out;
    const std::string key = key_();
    if (key.empty())
    {
        out.error = "Add your YouTube Data API key in Settings to browse YouTube.";
        return out;
    }
    params.emplace_back("key", key);
    net::Client client;
    net::Request request;
    request.url = std::string{kApi} + path + "?" + url::build_query(params);
    request.headers = {"Accept: application/json"};
    request.cancel = cancel;
    request.max_bytes = 2u * 1024u * 1024u;
    const net::Response r = client.perform(request);
    out.body = r.body;
    if (r.ok())
    {
        out.ok = true;
        return out;
    }
    out.error = r.outcome == net::Outcome::http_error
                    ? parse_error(r.body, r.status)
                    : "Could not reach YouTube (" + r.describe() + ")";
    return out;
}

std::vector<MediaItem> YouTube::parse_videos(const std::string &body, std::uint64_t now)
{
    std::vector<MediaItem> out;
    const auto parsed = json::parse(body);
    for (const auto &v : parsed.value["items"].items())
    {
        const std::string id = v["id"].is_string() ? v["id"].str() : v["id"]["videoId"].str();
        if (id.empty())
            continue;
        MediaItem m = video_item(id, v["snippet"], now);
        const double seconds = parse_iso8601_duration(v["contentDetails"]["duration"].str());
        if (seconds > 0.0)
        {
            m.duration = seconds;
            if (m.badge.empty())
                m.badge = format_clock(seconds);
        }
        const std::string views = v["statistics"]["viewCount"].str();
        if (!views.empty())
            m.meta = format_count(std::atoll(views.c_str())) + " views  \xC2\xB7  " + m.meta;
        out.push_back(std::move(m));
    }
    return out;
}

std::vector<MediaItem> YouTube::parse_search(const std::string &body, std::uint64_t now)
{
    std::vector<MediaItem> out;
    const auto parsed = json::parse(body);
    for (const auto &v : parsed.value["items"].items())
    {
        const std::string kind = v["id"]["kind"].str();
        if (kind == "youtube#video")
        {
            out.push_back(video_item(v["id"]["videoId"].str(), v["snippet"], now));
        }
        else if (kind == "youtube#channel")
        {
            MediaItem c;
            c.provider = "youtube";
            c.id = "channel:" + v["id"]["channelId"].str();
            c.kind = ItemKind::channel;
            c.title = v["snippet"]["title"].str();
            c.subtitle = "Channel";
            c.description = v["snippet"]["description"].str();
            c.image_url = best_thumbnail(v["snippet"]["thumbnails"]);
            c.external_url = "https://www.youtube.com/channel/" + v["id"]["channelId"].str();
            c.external_label = "Open on YouTube";
            out.push_back(std::move(c));
        }
    }
    return out;
}

ShelvesResult YouTube::home(const net::CancelFlag &cancel)
{
    const std::uint64_t now = platform::wall_clock_seconds();
    {
        std::lock_guard<std::mutex> guard(cache_lock_);
        if (home_cache_.ok && home_cached_at_ && now - home_cached_at_ < kHomeTtlSeconds)
            return home_cache_;
    }
    ShelvesResult result;
    const Fetch trending = get("videos",
                               {{"part", "snippet,contentDetails,statistics"},
                                {"chart", "mostPopular"},
                                {"maxResults", "24"},
                                {"regionCode", region_}},
                               cancel);
    if (!trending.ok)
        return {false, trending.error, {}};
    result.shelves.push_back({"Trending", parse_videos(trending.body, now), false});
    // Category 1 is "Film & Animation". Some regions have no chart for it.
    const Fetch animation = get("videos",
                                {{"part", "snippet,contentDetails,statistics"},
                                 {"chart", "mostPopular"},
                                 {"videoCategoryId", "1"},
                                 {"maxResults", "24"},
                                 {"regionCode", region_}},
                                cancel);
    if (animation.ok)
    {
        auto items = parse_videos(animation.body, now);
        if (!items.empty())
            result.shelves.push_back({"Film & Animation", std::move(items), false});
    }
    result.ok = true;
    std::lock_guard<std::mutex> guard(cache_lock_);
    home_cache_ = result;
    home_cached_at_ = now;
    return result;
}

ItemsResult YouTube::search(const std::string &query, const net::CancelFlag &cancel)
{
    const Fetch r = get("search",
                        {{"part", "snippet"},
                         {"type", "video,channel"},
                         {"maxResults", "24"},
                         {"q", query},
                         {"safeSearch", safe_search_},
                         {"regionCode", region_}},
                        cancel);
    if (!r.ok)
        return {false, r.error, {}};
    return {true, {}, parse_search(r.body, platform::wall_clock_seconds())};
}

DetailsResult YouTube::details(const MediaItem &item, const net::CancelFlag &cancel)
{
    DetailsResult result;
    result.item = item;
    const std::uint64_t now = platform::wall_clock_seconds();
    if (item.kind == ItemKind::channel)
    {
        const std::string channel = item.id.substr(item.id.find(':') + 1);
        const Fetch c = get(
            "channels", {{"part", "snippet,contentDetails,statistics"}, {"id", channel}}, cancel);
        if (!c.ok)
            return {false, c.error, item, {}};
        const auto parsed = json::parse(c.body);
        const json::Value &ch = parsed.value["items"][0];
        result.item.description = ch["snippet"]["description"].str(item.description);
        const std::string subs = ch["statistics"]["subscriberCount"].str();
        if (!subs.empty())
            result.item.meta = format_count(std::atoll(subs.c_str())) + " subscribers";
        const std::string uploads = ch["contentDetails"]["relatedPlaylists"]["uploads"].str();
        if (!uploads.empty())
        {
            const Fetch list =
                get("playlistItems",
                    {{"part", "snippet"}, {"playlistId", uploads}, {"maxResults", "24"}}, cancel);
            if (list.ok)
            {
                Shelf shelf{"Latest Uploads", {}, false};
                for (const auto &v : json::parse(list.body).value["items"].items())
                {
                    const std::string id = v["snippet"]["resourceId"]["videoId"].str();
                    if (!id.empty())
                        shelf.items.push_back(video_item(id, v["snippet"], now));
                }
                if (!shelf.items.empty())
                    result.related.push_back(std::move(shelf));
            }
        }
        result.ok = true;
        return result;
    }
    const Fetch v =
        get("videos", {{"part", "snippet,contentDetails,statistics"}, {"id", item.id}}, cancel);
    if (!v.ok)
        return {false, v.error, item, {}};
    auto videos = parse_videos(v.body, now);
    if (!videos.empty())
        result.item = videos.front();
    result.ok = true;
    return result;
}
} // namespace akeno
