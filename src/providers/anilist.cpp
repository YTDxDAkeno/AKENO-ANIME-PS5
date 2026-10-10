// AKENO STREAM PS5 - Anime catalogue from the public AniList GraphQL API.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "providers/anilist.hpp"

#include "platform/platform.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <set>

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

// ---------------------------------------------------------------------------
// Anime mode: more rows, filtered search and series with seasons.
namespace
{
bool season_format(const std::string &format)
{
    return format == "TV" || format == "TV_SHORT" || format == "ONA";
}

std::string series_status(const std::string &status)
{
    if (status == "FINISHED")
        return "finished";
    if (status == "RELEASING")
        return "releasing";
    if (status == "NOT_YET_RELEASED")
        return "upcoming";
    if (status == "CANCELLED")
        return "cancelled";
    return {};
}

std::string best_title(const json::Value &title)
{
    return title["english"].str(title["userPreferred"].str(title["romaji"].str()));
}

constexpr char kChainQuery[] =
    "query ($id: Int) {\n  Media(id: $id, type: ANIME) {\n    id format status seasonYear "
    "episodes\n"
    "    title { romaji english userPreferred }\n"
    "    relations { edges { relationType(version: 2) node { id type format } } }\n  }\n}\n";
} // namespace

ShelvesResult AniList::parse_genre_rows(const std::string &body)
{
    ShelvesResult result;
    const auto parsed = json::parse(body);
    if (!parsed.ok || !parsed.value["data"].is_object())
    {
        result.error =
            parsed.ok ? api_error(parsed.value, 0) : "AniList sent an unreadable response";
        if (result.error.empty())
            result.error = "AniList returned no rows";
        return result;
    }
    const json::Value &data = parsed.value["data"];
    Shelf fresh{"New Episodes", {}, true};
    std::set<std::string> seen;
    for (const auto &a : data["newEpisodes"]["airingSchedules"].items())
    {
        const json::Value &m = a["media"];
        if (!m.is_object() || m["isAdult"].boolean())
            continue;
        MediaItem item = parse_media(m);
        if (!seen.insert(item.id).second)
            continue;
        if (const long long n = a["episode"].integer(); n > 0)
            item.badge = "EP " + std::to_string(n);
        fresh.items.push_back(std::move(item));
    }
    if (!fresh.items.empty())
        result.shelves.push_back(std::move(fresh));
    static constexpr struct
    {
        const char *alias;
        const char *title;
    } rows[] = {{"action", "Action"},
                {"romance", "Romance"},
                {"fantasy", "Fantasy"},
                {"comedy", "Comedy"},
                {"movies", "Movies"}};
    for (const auto &r : rows)
    {
        Shelf shelf{r.title, {}, true};
        for (const auto &m : data[r.alias]["media"].items())
            shelf.items.push_back(parse_media(m));
        if (!shelf.items.empty())
            result.shelves.push_back(std::move(shelf));
    }
    result.ok = !result.shelves.empty();
    if (!result.ok)
        result.error = "AniList returned no rows";
    return result;
}

ShelvesResult AniList::genre_rows(const net::CancelFlag &cancel)
{
    const std::uint64_t now = platform::wall_clock_seconds();
    {
        std::lock_guard<std::mutex> guard(cache_lock_);
        if (rows_cache_.ok && rows_cached_at_ && now >= rows_cached_at_ &&
            now - rows_cached_at_ < kHomeTtlSeconds)
            return rows_cache_;
    }
    const bool clock = now > 946684800;
    std::string query = "query ($perPage: Int, $adult: Boolean";
    if (clock)
        query += ", $since: Int, $until: Int";
    query += ") {\n";
    if (clock)
        query += "  newEpisodes: Page(page: 1, perPage: 30) { airingSchedules(airingAt_greater: "
                 "$since, airingAt_lesser: $until, sort: TIME_DESC) { episode airingAt media { "
                 "...card isAdult } } }\n";
    for (const char *genre : {"Action", "Romance", "Fantasy", "Comedy"})
    {
        std::string alias = genre;
        alias[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(alias[0])));
        query += "  " + alias +
                 ": Page(page: 1, perPage: $perPage) { media(type: ANIME, genre: \"" + genre +
                 "\", sort: TRENDING_DESC, isAdult: $adult) { ...card } }\n";
    }
    query += "  movies: Page(page: 1, perPage: $perPage) { media(type: ANIME, format: MOVIE, "
             "sort: TRENDING_DESC, isAdult: $adult) { ...card } }\n}\n";
    query += kFields;
    json::Value variables = json::Value::object();
    variables.set("perPage", 15);
    variables.set("adult", include_adult_);
    if (clock)
    {
        variables.set("since", static_cast<long long>(now - 3 * 86400));
        variables.set("until", static_cast<long long>(now));
    }
    const Response r = post(query, variables, cancel);
    if (!r.ok)
        return {false, r.error, {}};
    ShelvesResult result = parse_genre_rows(r.body);
    if (result.ok)
    {
        std::lock_guard<std::mutex> guard(cache_lock_);
        rows_cache_ = result;
        rows_cached_at_ = now;
    }
    return result;
}

ItemsResult AniList::search(const std::string &text, const Filters &filters,
                            const net::CancelFlag &cancel)
{
    std::string params = "$adult: Boolean";
    std::string args = "type: ANIME, isAdult: $adult";
    json::Value variables = json::Value::object();
    variables.set("adult", include_adult_);
    if (!text.empty())
    {
        params += ", $search: String";
        args += ", search: $search, sort: SEARCH_MATCH";
        variables.set("search", text);
    }
    else
        args += ", sort: POPULARITY_DESC";
    if (filters.year > 0)
    {
        params += ", $year: Int";
        args += ", seasonYear: $year";
        variables.set("year", filters.year);
    }
    if (!filters.genre.empty())
    {
        params += ", $genre: String";
        args += ", genre: $genre";
        variables.set("genre", filters.genre);
    }
    if (!filters.status.empty())
    {
        params += ", $status: MediaStatus";
        args += ", status: $status";
        variables.set("status", filters.status);
    }
    std::string query = "query (" + params + ") {\n  Page(page: 1, perPage: 30) { media(" + args +
                        ") { ...card } }\n}\n";
    query += kFields;
    const Response r = post(query, variables, cancel);
    if (!r.ok)
        return {false, r.error, {}};
    return parse_search(r.body);
}

std::vector<anime::Episode> AniList::parse_streaming_episodes(const json::Value &list)
{
    std::vector<anime::Episode> out;
    int fallback = 0;
    for (const auto &e : list.items())
    {
        const std::string full = e["title"].str();
        const std::string link = e["url"].str();
        if (full.empty() || link.empty())
            continue;
        anime::Episode ep;
        ++fallback;
        // "Episode 12 - Title"; some services use other forms: kept whole.
        int number = 0;
        std::string title = full;
        if (full.rfind("Episode ", 0) == 0)
        {
            std::size_t i = 8;
            while (i < full.size() && std::isdigit(static_cast<unsigned char>(full[i])))
                number = number * 10 + (full[i++] - '0');
            if (number > 0)
            {
                title = full.substr(i);
                const std::size_t start = title.find_first_not_of(" -:\xE2\x80\x93");
                title = start == std::string::npos ? std::string{} : title.substr(start);
            }
        }
        ep.number = number > 0 ? number : fallback;
        ep.id = "e" + std::to_string(ep.number);
        ep.title = title;
        ep.thumbnail = e["thumbnail"].str();
        ep.link = link;
        ep.link_label = e["site"].str();
        ep.availability = anime::Availability::catalogue_only;
        ep.availability_detail =
            "Streams on " +
            (ep.link_label.empty() ? std::string{"an official service"} : ep.link_label) +
            " with DRM. AKENO STREAM shows its page link; it cannot play it.";
        out.push_back(std::move(ep));
        if (out.size() >= 500)
            break;
    }
    std::stable_sort(out.begin(), out.end(), [](const anime::Episode &a, const anime::Episode &b)
                     { return a.number < b.number; });
    // Two entries with one number (e.g. two sites): keep the first.
    out.erase(std::unique(out.begin(), out.end(),
                          [](const anime::Episode &a, const anime::Episode &b)
                          { return a.number == b.number; }),
              out.end());
    return out;
}

std::vector<anime::Episode> AniList::season_episodes(int anilist_id, const net::CancelFlag &cancel)
{
    {
        std::lock_guard<std::mutex> guard(cache_lock_);
        if (const auto found = episodes_cache_.find(anilist_id); found != episodes_cache_.end())
            return found->second;
    }
    const std::string query = "query ($id: Int) {\n  Media(id: $id, type: ANIME) { "
                              "streamingEpisodes { title thumbnail url site } }\n}\n";
    json::Value variables = json::Value::object();
    variables.set("id", anilist_id);
    const Response r = post(query, variables, cancel);
    if (!r.ok)
        return {};
    const auto parsed = json::parse(r.body);
    std::vector<anime::Episode> episodes =
        parse_streaming_episodes(parsed.value.at_path("data.Media.streamingEpisodes"));
    std::lock_guard<std::mutex> guard(cache_lock_);
    if (episodes_cache_.size() > 100)
        episodes_cache_.clear();
    episodes_cache_[anilist_id] = episodes;
    return episodes;
}

AniList::SeriesResult AniList::series(int anilist_id, const net::CancelFlag &cancel)
{
    {
        std::lock_guard<std::mutex> guard(cache_lock_);
        if (const auto found = series_cache_.find(anilist_id); found != series_cache_.end())
            return found->second;
    }
    SeriesResult out;
    std::string query =
        "query ($id: Int) {\n  Media(id: $id, type: ANIME) {\n    ...card\n"
        "    idMal synonyms title { native }\n"
        "    description(asHtml: false)\n"
        "    streamingEpisodes { title thumbnail url site }\n"
        "    relations { edges { relationType(version: 2) node { id type format } } }\n"
        "  }\n}\n";
    query += kFields;
    json::Value variables = json::Value::object();
    variables.set("id", anilist_id);
    const Response r = post(query, variables, cancel);
    if (!r.ok)
    {
        out.error = r.error;
        return out;
    }
    const auto parsed = json::parse(r.body);
    const json::Value &m = parsed.value.at_path("data.Media");
    if (!parsed.ok || !m.is_object())
    {
        out.error = "Title not found";
        return out;
    }
    const MediaItem card = parse_media(m);
    anime::Series &s = out.series;
    s.provider_id = "anilist";
    s.provider_name = "AniList";
    s.id = std::to_string(anilist_id);
    s.title = card.title;
    const json::Value &title = m["title"];
    for (const char *key : {"romaji", "english", "userPreferred"})
        if (!title[key].str().empty() && title[key].str() != s.title &&
            std::find(s.alt_titles.begin(), s.alt_titles.end(), title[key].str()) ==
                s.alt_titles.end())
            s.alt_titles.push_back(title[key].str());
    for (const auto &syn : m["synonyms"].items())
        if (!syn.str().empty() && s.alt_titles.size() < 12)
            s.alt_titles.push_back(syn.str());
    s.description = strip_html(m["description"].str());
    s.poster = card.image_url;
    s.banner = card.banner_url;
    s.genres = card.genres;
    s.year = static_cast<int>(m["seasonYear"].integer());
    s.status = series_status(m["status"].str());
    s.score = static_cast<int>(m["averageScore"].integer());
    s.anilist_id = anilist_id;
    s.mal_id = static_cast<int>(m["idMal"].integer());
    s.attribution = "Anime data: AniList";

    // Seasons: walk prequels back to the first season, then sequels forward.
    struct Entry
    {
        int id;
        std::string title;
        int year;
        int episodes;
    };
    const auto light = [&](int id, std::vector<int> *prequels, std::vector<int> *sequels,
                           Entry *entry) -> bool
    {
        json::Value v = json::Value::object();
        v.set("id", id);
        const Response lr = post(kChainQuery, v, cancel);
        if (!lr.ok)
            return false;
        const auto p = json::parse(lr.body);
        const json::Value &x = p.value.at_path("data.Media");
        if (!x.is_object())
            return false;
        *entry = {id, best_title(x["title"]), static_cast<int>(x["seasonYear"].integer()),
                  static_cast<int>(x["episodes"].integer())};
        for (const auto &edge : x["relations"]["edges"].items())
        {
            const json::Value &node = edge["node"];
            if (node["type"].str() != "ANIME" || !season_format(node["format"].str()))
                continue;
            const std::string relation = edge["relationType"].str();
            if (relation == "PREQUEL" && prequels)
                prequels->push_back(static_cast<int>(node["id"].integer()));
            if (relation == "SEQUEL" && sequels)
                sequels->push_back(static_cast<int>(node["id"].integer()));
        }
        return true;
    };
    std::vector<Entry> chain{
        {anilist_id, card.title, s.year, static_cast<int>(m["episodes"].integer())}};
    if (season_format(m["format"].str()))
    {
        std::vector<int> prequels, sequels;
        for (const auto &edge : m["relations"]["edges"].items())
        {
            const json::Value &node = edge["node"];
            if (node["type"].str() != "ANIME" || !season_format(node["format"].str()))
                continue;
            if (edge["relationType"].str() == "PREQUEL")
                prequels.push_back(static_cast<int>(node["id"].integer()));
            if (edge["relationType"].str() == "SEQUEL")
                sequels.push_back(static_cast<int>(node["id"].integer()));
        }
        std::set<int> seen{anilist_id};
        for (int steps = 0; !prequels.empty() && steps < 6; ++steps)
        {
            const int id = prequels.front();
            if (!seen.insert(id).second)
                break;
            std::vector<int> next;
            Entry e{};
            if (!light(id, &next, nullptr, &e))
                break;
            chain.insert(chain.begin(), e);
            prequels = next;
        }
        for (int steps = 0; !sequels.empty() && steps < 6; ++steps)
        {
            const int id = sequels.front();
            if (!seen.insert(id).second)
                break;
            std::vector<int> next;
            Entry e{};
            if (!light(id, nullptr, &next, &e))
                break;
            chain.push_back(e);
            sequels = next;
        }
    }
    for (std::size_t i = 0; i < chain.size(); ++i)
    {
        anime::Season season;
        season.id = "al" + std::to_string(chain[i].id);
        season.number = static_cast<int>(i + 1);
        season.title = chain.size() == 1 ? "Season 1" : chain[i].title;
        season.year = chain[i].year;
        season.anilist_id = chain[i].id;
        season.episode_count = chain[i].episodes;
        if (chain[i].id == anilist_id)
        {
            season.episodes = parse_streaming_episodes(m["streamingEpisodes"]);
            out.focus_season = static_cast<int>(i);
        }
        s.seasons.push_back(std::move(season));
    }
    out.ok = true;
    std::lock_guard<std::mutex> guard(cache_lock_);
    if (series_cache_.size() > 60)
        series_cache_.clear();
    series_cache_[anilist_id] = out;
    return out;
}
} // namespace akeno
