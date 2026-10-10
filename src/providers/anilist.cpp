// AKENO STREAM PS5 - Anime catalogue from the public AniList GraphQL API.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "providers/anilist.hpp"

#include "platform/platform.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace akeno
{
namespace
{
constexpr char kEndpoint[] = "https://graphql.anilist.co";
constexpr std::uint64_t kHomeTtlSeconds = 30 * 60;

constexpr char kFields[] = R"(fragment card on Media {
  id
  title { romaji english native userPreferred }
  coverImage { extraLarge large color }
  bannerImage
  format
  episodes
  duration
  status
  season
  seasonYear
  averageScore
  genres
  siteUrl
  studios(isMain: true) { nodes { name } }
})";

std::string format_name(const std::string &format)
{
    if (format == "TV")
        return "TV";
    if (format == "TV_SHORT")
        return "TV Short";
    if (format == "MOVIE")
        return "Movie";
    if (format == "SPECIAL")
        return "Special";
    if (format == "OVA")
        return "OVA";
    if (format == "ONA")
        return "ONA";
    if (format == "MUSIC")
        return "Music";
    return format;
}

std::uint32_t parse_color(const std::string &hex)
{
    if (hex.size() != 7 || hex[0] != '#')
        return 0;
    std::uint32_t v = 0;
    for (std::size_t i = 1; i < 7; ++i)
    {
        const char c = hex[i];
        v <<= 4;
        if (c >= '0' && c <= '9')
            v |= static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            v |= static_cast<std::uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v |= static_cast<std::uint32_t>(c - 'A' + 10);
        else
            return 0;
    }
    return v;
}

std::string api_error(const json::Value &root, long status)
{
    const json::Value &errors = root["errors"];
    if (errors.is_array() && errors.size() > 0)
    {
        const std::string message = errors[0]["message"].str();
        if (errors[0]["status"].integer() == 429 || status == 429)
            return "AniList is limiting requests right now. Please try again in a minute.";
        if (!message.empty())
            return "AniList: " + message;
    }
    if (status == 429)
        return "AniList is limiting requests right now. Please try again in a minute.";
    return {};
}
} // namespace

AniList::AniList(bool include_adult) : include_adult_{include_adult}
{
}

ProviderInfo AniList::info() const
{
    ProviderInfo info;
    info.id = "anilist";
    info.name = "Anime";
    info.tagline = "Discover anime with the public AniList catalogue.";
    info.attribution = "Anime data and artwork provided by AniList (anilist.co).";
    info.capabilities = {
        {"Browse & search", Support::available,
         "Trending, seasonal, popular and top-rated anime from AniList."},
        {"Details", Support::available,
         "Synopsis, studios, genres, score and the legal services that stream each title."},
        {"Episode playback", Support::unavailable,
         "Licensed anime is streamed by services such as Crunchyroll with DRM. AKENO shows where "
         "to watch "
         "and hands you a link; it does not play or bypass protected video."},
    };
    return info;
}

std::pair<std::string, int> AniList::season_for(std::uint64_t unix_seconds)
{
    if (unix_seconds < 946684800) // before 2000: clock not set
        return {"", 0};
    const std::time_t t = static_cast<std::time_t>(unix_seconds);
    std::tm *utc = std::gmtime(&t);
    if (!utc)
        return {"", 0};
    const int month = utc->tm_mon + 1;
    const int year = utc->tm_year + 1900;
    const char *season = month <= 3   ? "WINTER"
                         : month <= 6 ? "SPRING"
                         : month <= 9 ? "SUMMER"
                                      : "FALL";
    return {season, year};
}

MediaItem AniList::parse_media(const json::Value &m)
{
    MediaItem item;
    item.provider = "anilist";
    item.id = std::to_string(m["id"].integer());
    item.kind = ItemKind::series;
    const json::Value &title = m["title"];
    item.title = title["english"].str(title["userPreferred"].str(title["romaji"].str()));
    const std::string romaji = title["romaji"].str();
    // Native (CJK) titles are not shown: the bundled interface font is Latin-only.
    item.subtitle = romaji != item.title ? romaji : std::string{};
    item.image_url = m["coverImage"]["extraLarge"].str(m["coverImage"]["large"].str());
    item.banner_url = m["bannerImage"].str();
    item.portrait = true;
    item.accent = parse_color(m["coverImage"]["color"].str());
    item.description = strip_html(m["description"].str());
    item.external_url = m["siteUrl"].str();
    item.external_label = "Open on AniList";
    for (const auto &g : m["genres"].items())
        item.genres.push_back(g.str());
    std::string meta;
    const auto add = [&](const std::string &part)
    {
        if (part.empty())
            return;
        if (!meta.empty())
            meta += "  \xC2\xB7  ";
        meta += part;
    };
    if (m["seasonYear"].integer())
        add(std::to_string(m["seasonYear"].integer()));
    add(format_name(m["format"].str()));
    if (const long long episodes = m["episodes"].integer())
        add(std::to_string(episodes) + (episodes == 1 ? " episode" : " episodes"));
    if (const long long score = m["averageScore"].integer())
        add("\xE2\x98\x85 " + std::to_string(score) + "%");
    const auto &studios = m["studios"]["nodes"];
    if (studios.size() > 0)
        add(studios[0]["name"].str());
    item.meta = meta;
    const std::string status = m["status"].str();
    if (status == "RELEASING")
        item.badge = "AIRING";
    else if (status == "NOT_YET_RELEASED")
        item.badge = "UPCOMING";
    else if (const long long episodes = m["episodes"].integer(); episodes > 1)
        item.badge = std::to_string(episodes) + " EP";
    else
        item.badge = format_name(m["format"].str());
    item.attribution = "Data: AniList";
    return item;
}

AniList::Response AniList::post(const std::string &query, const json::Value &variables,
                                const net::CancelFlag &cancel)
{
    json::Value body = json::Value::object();
    body.set("query", query);
    body.set("variables", variables);
    net::Client client;
    net::Request request;
    request.url = kEndpoint;
    request.method = "POST";
    request.body = body.dump();
    request.headers = {"Content-Type: application/json", "Accept: application/json"};
    request.cancel = cancel;
    request.max_bytes = 4u * 1024u * 1024u;
    request.total_timeout_ms = 20000;
    const net::Response r = client.perform(request);
    Response out;
    out.body = r.body;
    if (r.ok())
    {
        out.ok = true;
        return out;
    }
    // GraphQL errors arrive with 4xx and a JSON body.
    const auto parsed = json::parse(r.body);
    out.error = parsed.ok ? api_error(parsed.value, r.status) : std::string{};
    if (out.error.empty())
        out.error = "Could not reach AniList (" + r.describe() + ")";
    return out;
}

ShelvesResult AniList::parse_home(const std::string &body)
{
    ShelvesResult result;
    const auto parsed = json::parse(body);
    if (!parsed.ok)
    {
        result.error = "AniList sent an unreadable response";
        return result;
    }
    if (std::string error = api_error(parsed.value, 0);
        !error.empty() && !parsed.value["data"].is_object())
    {
        result.error = error;
        return result;
    }
    static constexpr struct
    {
        const char *alias;
        const char *title;
    } shelves[] = {{"trending", "Trending Now"},
                   {"season", "Popular This Season"},
                   {"popular", "All-Time Popular"},
                   {"top", "Top Rated"}};
    for (const auto &s : shelves)
    {
        Shelf shelf;
        shelf.title = s.title;
        shelf.portrait = true;
        for (const auto &m : parsed.value["data"][s.alias]["media"].items())
            shelf.items.push_back(parse_media(m));
        if (!shelf.items.empty())
            result.shelves.push_back(std::move(shelf));
    }
    result.ok = !result.shelves.empty();
    if (!result.ok)
        result.error = "AniList returned no titles";
    return result;
}

ShelvesResult AniList::home(const net::CancelFlag &cancel)
{
    const std::uint64_t now = platform::wall_clock_seconds();
    {
        std::lock_guard<std::mutex> guard(cache_lock_);
        if (home_cache_.ok && home_cached_at_ && now >= home_cached_at_ &&
            now - home_cached_at_ < kHomeTtlSeconds)
            return home_cache_;
    }
    const auto [season, year] = season_for(now);
    std::string query = "query ($perPage: Int, $adult: Boolean";
    if (!season.empty())
        query += ", $season: MediaSeason, $year: Int";
    query += ") {\n"
             "  trending: Page(page: 1, perPage: $perPage) { media(sort: TRENDING_DESC, type: "
             "ANIME, isAdult: $adult) { ...card } }\n";
    if (!season.empty())
        query += "  season: Page(page: 1, perPage: $perPage) { media(sort: POPULARITY_DESC, type: "
                 "ANIME, season: $season, "
                 "seasonYear: $year, isAdult: $adult) { ...card } }\n";
    query += "  popular: Page(page: 1, perPage: $perPage) { media(sort: POPULARITY_DESC, type: "
             "ANIME, isAdult: $adult) { ...card } }\n"
             "  top: Page(page: 1, perPage: $perPage) { media(sort: SCORE_DESC, type: ANIME, "
             "isAdult: $adult) { ...card } }\n"
             "}\n";
    query += kFields;
    json::Value variables = json::Value::object();
    variables.set("perPage", 20);
    variables.set("adult", include_adult_);
    if (!season.empty())
    {
        variables.set("season", season);
        variables.set("year", year);
    }
    const Response r = post(query, variables, cancel);
    if (!r.ok)
        return {false, r.error, {}};
    ShelvesResult result = parse_home(r.body);
    if (result.ok)
    {
        std::lock_guard<std::mutex> guard(cache_lock_);
        home_cache_ = result;
        home_cached_at_ = now;
    }
    return result;
}

ItemsResult AniList::parse_search(const std::string &body)
{
    ItemsResult result;
    const auto parsed = json::parse(body);
    if (!parsed.ok)
    {
        result.error = "AniList sent an unreadable response";
        return result;
    }
    for (const auto &m : parsed.value.at_path("data.Page.media").items())
        result.items.push_back(parse_media(m));
    result.ok = parsed.value["data"].is_object();
    if (!result.ok)
        result.error = api_error(parsed.value, 0);
    return result;
}

ItemsResult AniList::search(const std::string &text, const net::CancelFlag &cancel)
{
    std::string query =
        "query ($search: String, $adult: Boolean) {\n"
        "  Page(page: 1, perPage: 30) { media(search: $search, type: ANIME, sort: SEARCH_MATCH, "
        "isAdult: $adult) { ...card } }\n}\n";
    query += kFields;
    json::Value variables = json::Value::object();
    variables.set("search", text);
    variables.set("adult", include_adult_);
    const Response r = post(query, variables, cancel);
    if (!r.ok)
        return {false, r.error, {}};
    return parse_search(r.body);
}

DetailsResult AniList::parse_details(const std::string &body)
{
    DetailsResult result;
    const auto parsed = json::parse(body);
    if (!parsed.ok || !parsed.value.at_path("data.Media").is_object())
    {
        result.error =
            parsed.ok ? api_error(parsed.value, 0) : "AniList sent an unreadable response";
        if (result.error.empty())
            result.error = "Title not found";
        return result;
    }
    const json::Value &m = parsed.value.at_path("data.Media");
    result.item = parse_media(m);
    const json::Value &next = m["nextAiringEpisode"];
    if (next.is_object())
    {
        const long long seconds = next["timeUntilAiring"].integer();
        if (seconds > 0)
        {
            char text[96];
            std::snprintf(text, sizeof(text), "Episode %lld airs in %lld d %lld h",
                          next["episode"].integer(), seconds / 86400, (seconds / 3600) % 24);
            result.item.meta += "  \xC2\xB7  ";
            result.item.meta += text;
        }
    }

    // Where to watch: AniList's external links of type STREAMING.
    Shelf services;
    services.title = "Where to Watch (official services)";
    for (const auto &link : m["externalLinks"].items())
    {
        if (link["type"].str() != "STREAMING")
            continue;
        MediaItem service;
        service.provider = "link";
        service.id = link["url"].str();
        service.kind = ItemKind::info;
        service.title = link["site"].str();
        service.subtitle = "Official streaming service";
        service.external_url = link["url"].str();
        service.external_label = "Open on " + link["site"].str();
        service.description =
            "Episodes of this title are licensed to " + link["site"].str() +
            ". Use the QR code to open the official page on your phone, or use the " +
            link["site"].str() + " app.";
        service.accent = parse_color(link["color"].str());
        if (!service.title.empty() && !service.external_url.empty())
            services.items.push_back(std::move(service));
    }
    if (!services.items.empty())
        result.related.push_back(std::move(services));

    Shelf episodes;
    episodes.title = "Episodes (links to official streams)";
    for (const auto &e : m["streamingEpisodes"].items())
    {
        MediaItem ep;
        ep.provider = "link";
        ep.id = e["url"].str();
        ep.kind = ItemKind::info;
        ep.title = e["title"].str();
        ep.subtitle = e["site"].str();
        ep.image_url = e["thumbnail"].str();
        ep.external_url = e["url"].str();
        ep.external_label = "Watch on " + e["site"].str();
        ep.description = "This episode streams on " + e["site"].str() +
                         " (DRM-protected). Scan the code to open it on your phone.";
        if (!ep.title.empty() && !ep.external_url.empty())
            episodes.items.push_back(std::move(ep));
        if (episodes.items.size() >= 60)
            break;
    }
    if (!episodes.items.empty())
        result.related.push_back(std::move(episodes));

    const json::Value &trailer = m["trailer"];
    if (trailer["site"].str() == "youtube" && !trailer["id"].str().empty())
    {
        MediaItem t;
        t.provider = "link";
        t.id = "yt-" + trailer["id"].str();
        t.kind = ItemKind::info;
        t.title = "Official Trailer";
        t.subtitle = "YouTube";
        t.image_url = "https://i.ytimg.com/vi/" + trailer["id"].str() + "/hqdefault.jpg";
        t.external_url = "https://www.youtube.com/watch?v=" + trailer["id"].str();
        t.external_label = "Watch on YouTube";
        t.description =
            "The trailer is hosted on YouTube. Scan the code to watch it on your phone.";
        result.related.insert(result.related.begin(), Shelf{"Trailer", {t}, false});
    }
    result.ok = true;
    return result;
}

DetailsResult AniList::details(const MediaItem &item, const net::CancelFlag &cancel)
{
    {
        std::lock_guard<std::mutex> guard(cache_lock_);
        if (const auto found = details_cache_.find(item.id); found != details_cache_.end())
            return found->second;
    }
    std::string query = "query ($id: Int) {\n  Media(id: $id, type: ANIME) {\n    ...card\n"
                        "    description(asHtml: false)\n"
                        "    nextAiringEpisode { episode timeUntilAiring }\n"
                        "    externalLinks { site url type color }\n"
                        "    streamingEpisodes { title thumbnail url site }\n"
                        "    trailer { id site }\n  }\n}\n";
    query += kFields;
    json::Value variables = json::Value::object();
    variables.set("id", std::atoll(item.id.c_str()));
    const Response r = post(query, variables, cancel);
    if (!r.ok)
        return {false, r.error, item, {}};
    DetailsResult result = parse_details(r.body);
    if (result.ok)
    {
        std::lock_guard<std::mutex> guard(cache_lock_);
        if (details_cache_.size() > 60)
            details_cache_.clear();
        details_cache_[item.id] = result;
    }
    return result;
}
} // namespace akeno
