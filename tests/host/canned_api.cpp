// AKENO STREAM PS5 - Canned API responses for host tests and screenshots.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "canned_api.hpp"

#include "core/json.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include "stb/stb_image_write.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace akeno::test
{
namespace
{
struct Anime
{
    int id;
    const char *english;
    const char *romaji;
    const char *native;
    const char *color;
    const char *format;
    int episodes;
    int score;
    int year;
    const char *season;
    const char *status;
    const char *studio;
    std::vector<const char *> genres;
};

const std::vector<Anime> &catalog()
{
    static const std::vector<Anime> list = {
        {16498,
         "Attack on Titan",
         "Shingeki no Kyojin",
         "\xE9\x80\xB2\xE6\x92\x83\xE3\x81\xAE\xE5\xB7\xA8\xE4\xBA\xBA",
         "#e4a15d",
         "TV",
         25,
         85,
         2013,
         "SPRING",
         "FINISHED",
         "Wit Studio",
         {"Action", "Drama", "Fantasy"}},
        {21,
         "One Piece",
         "One Piece",
         "ONE PIECE",
         "#e4a143",
         "TV",
         0,
         87,
         1999,
         "FALL",
         "RELEASING",
         "Toei Animation",
         {"Action", "Adventure", "Comedy"}},
        {154587,
         "Frieren: Beyond Journey's End",
         "Sousou no Frieren",
         "\xE8\x91\xAC\xE9\x80\x81\xE3\x81\xAE\xE3\x83\x95\xE3\x83\xAA\xE3\x83\xBC\xE3\x83\xAC\xE3"
         "\x83\xB3",
         "#a1c9e4",
         "TV",
         28,
         91,
         2023,
         "FALL",
         "FINISHED",
         "MADHOUSE",
         {"Adventure", "Drama", "Fantasy"}},
        {101922,
         "Demon Slayer: Kimetsu no Yaiba",
         "Kimetsu no Yaiba",
         "\xE9\xAC\xBC\xE6\xBB\x85\xE3\x81\xAE\xE5\x88\x83",
         "#4b2635",
         "TV",
         26,
         84,
         2019,
         "SPRING",
         "FINISHED",
         "ufotable",
         {"Action", "Adventure", "Supernatural"}},
        {113415,
         "Jujutsu Kaisen",
         "Jujutsu Kaisen",
         "\xE5\x91\xAA\xE8\xA1\x93\xE5\xBB\xBB\xE6\x88\xA6",
         "#e45d78",
         "TV",
         24,
         85,
         2020,
         "FALL",
         "FINISHED",
         "MAPPA",
         {"Action", "Drama", "Supernatural"}},
        {140960,
         "SPY x FAMILY",
         "SPY x FAMILY",
         "SPY\xC3\x97"
         "FAMILY",
         "#e4c95d",
         "TV",
         12,
         84,
         2022,
         "SPRING",
         "FINISHED",
         "Wit Studio",
         {"Action", "Comedy", "Slice of Life"}},
        {21459,
         "My Hero Academia",
         "Boku no Hero Academia",
         "\xE5\x83\x95\xE3\x81\xAE\xE3\x83\x92\xE3\x83\xBC\xE3\x83\xAD\xE3\x83\xBC\xE3\x82\xA2\xE3"
         "\x82\xAB\xE3\x83\x87\xE3\x83\x9F\xE3\x82\xA2",
         "#e4935d",
         "TV",
         13,
         77,
         2016,
         "SPRING",
         "FINISHED",
         "bones",
         {"Action", "Adventure", "Comedy"}},
        {20,
         "Naruto",
         "NARUTO",
         "NARUTO -\xE3\x83\x8A\xE3\x83\xAB\xE3\x83\x88-",
         "#e47850",
         "TV",
         220,
         79,
         2002,
         "FALL",
         "FINISHED",
         "Studio Pierrot",
         {"Action", "Adventure", "Comedy"}},
    };
    return list;
}

json::Value media_json(const Anime &a, bool details)
{
    json::Value m = json::Value::object();
    m.set("id", a.id);
    json::Value title = json::Value::object();
    title.set("english", a.english);
    title.set("romaji", a.romaji);
    title.set("native", a.native);
    title.set("userPreferred", a.romaji);
    m.set("title", title);
    json::Value cover = json::Value::object();
    cover.set("extraLarge", "https://s4.anilist.co/file/anilistcdn/media/anime/cover/large/bx" +
                                std::to_string(a.id) + ".jpg");
    cover.set("large", "https://s4.anilist.co/file/anilistcdn/media/anime/cover/medium/bx" +
                           std::to_string(a.id) + ".jpg");
    cover.set("color", a.color);
    m.set("coverImage", cover);
    m.set("bannerImage", "https://s4.anilist.co/file/anilistcdn/media/anime/banner/" +
                             std::to_string(a.id) + ".jpg");
    m.set("format", a.format);
    m.set("episodes", a.episodes ? json::Value{a.episodes} : json::Value{});
    m.set("duration", 24);
    m.set("status", a.status);
    m.set("season", a.season);
    m.set("seasonYear", a.year);
    m.set("averageScore", a.score);
    json::Value genres = json::Value::array();
    for (const char *g : a.genres)
        genres.push(g);
    m.set("genres", genres);
    m.set("siteUrl", "https://anilist.co/anime/" + std::to_string(a.id));
    json::Value studios = json::Value::object();
    json::Value nodes = json::Value::array();
    json::Value studio = json::Value::object();
    studio.set("name", a.studio);
    nodes.push(studio);
    studios.set("nodes", nodes);
    m.set("studios", studios);
    if (details)
    {
        m.set("description",
              "Centuries ago, mankind was slaughtered to near extinction by monstrous humanoid "
              "creatures called <i>Titans</i>.<br><br>Now, behind towering walls, humanity "
              "survives &mdash; until the day the walls are breached.");
        json::Value links = json::Value::array();
        json::Value cr = json::Value::object();
        cr.set("site", "Crunchyroll");
        cr.set("url", "https://www.crunchyroll.com/series/GR751KNZY/attack-on-titan");
        cr.set("type", "STREAMING");
        cr.set("color", "#F88B24");
        links.push(cr);
        json::Value tw = json::Value::object();
        tw.set("site", "Twitter");
        tw.set("url", "https://twitter.com/anime_shingeki");
        tw.set("type", "SOCIAL");
        links.push(tw);
        m.set("externalLinks", links);
        json::Value eps = json::Value::array();
        for (int e = 1; e <= 4; ++e)
        {
            json::Value ep = json::Value::object();
            ep.set("title", "Episode " + std::to_string(e) + " - To You, in 2000 Years");
            ep.set("thumbnail",
                   "https://img1.ak.crunchyroll.com/i/spire2-tmb/ep" + std::to_string(e) + ".jpg");
            ep.set("url", "https://www.crunchyroll.com/watch/ep" + std::to_string(e));
            ep.set("site", "Crunchyroll");
            eps.push(ep);
        }
        m.set("streamingEpisodes", eps);
        json::Value trailer = json::Value::object();
        trailer.set("id", "LHtdKWJdif4");
        trailer.set("site", "youtube");
        m.set("trailer", trailer);
        json::Value next = json::Value::object();
        next.set("episode", 5);
        next.set("timeUntilAiring", 200000);
        m.set("nextAiringEpisode", a.status == std::string{"RELEASING"} ? next : json::Value{});
    }
    return m;
}

json::Value page(std::size_t offset)
{
    json::Value p = json::Value::object();
    json::Value media = json::Value::array();
    const auto &all = catalog();
    for (std::size_t i = 0; i < all.size(); ++i)
        media.push(media_json(all[(i + offset) % all.size()], false));
    p.set("media", media);
    return p;
}

void write_png(void *context, void *data, int size)
{
    static_cast<std::string *>(context)->append(static_cast<const char *>(data),
                                                static_cast<std::size_t>(size));
}

net::Response ok(std::string body, std::string type = "application/json")
{
    net::Response r;
    r.outcome = net::Outcome::ok;
    r.status = 200;
    r.body = std::move(body);
    r.content_type = std::move(type);
    return r;
}
} // namespace

std::string anilist_home_response()
{
    json::Value data = json::Value::object();
    data.set("trending", page(0));
    data.set("season", page(2));
    data.set("popular", page(4));
    data.set("top", page(1));
    json::Value root = json::Value::object();
    root.set("data", data);
    return root.dump();
}

std::string anilist_details_response()
{
    json::Value data = json::Value::object();
    data.set("Media", media_json(catalog()[0], true));
    json::Value root = json::Value::object();
    root.set("data", data);
    return root.dump();
}

std::string anilist_search_response()
{
    json::Value data = json::Value::object();
    json::Value p = json::Value::object();
    json::Value media = json::Value::array();
    media.push(media_json(catalog()[2], false));
    media.push(media_json(catalog()[0], false));
    p.set("media", media);
    data.set("Page", p);
    json::Value root = json::Value::object();
    root.set("data", data);
    return root.dump();
}

std::string youtube_videos_response()
{
    static const struct
    {
        const char *id, *title, *channel, *duration, *views, *published;
    } videos[] = {
        {"aqz-KE-bpKQ", "Big Buck Bunny 60fps 4K - Official Blender Foundation Short Film",
         "Blender", "PT10M35S", "18000000", "2014-11-10T14:05:55Z"},
        {"eRsGyueVLvQ", "Sintel - Open Movie by Blender Foundation", "Blender", "PT14M48S",
         "12000000", "2010-09-30T00:00:00Z"},
        {"R6MlUcmOul8", "Tears of Steel - Blender Foundation's fourth Open Movie", "Blender",
         "PT12M14S", "5600000", "2012-09-26T00:00:00Z"},
        {"WhWc3b3KhnY", "Spring - Blender Open Movie", "Blender Studio", "PT7M44S", "9100000",
         "2019-04-04T00:00:00Z"},
        {"Z4C82eyhwgU", "Cosmos Laundromat - First Cycle. Official Blender Foundation release.",
         "Blender", "PT12M10S", "4200000", "2015-08-10T00:00:00Z"},
    };
    json::Value items = json::Value::array();
    for (const auto &v : videos)
    {
        json::Value item = json::Value::object();
        item.set("kind", "youtube#video");
        item.set("id", v.id);
        json::Value snippet = json::Value::object();
        snippet.set("title", v.title);
        snippet.set("channelTitle", v.channel);
        snippet.set("publishedAt", v.published);
        snippet.set(
            "description",
            "An open movie made with Blender, licensed under Creative Commons Attribution.");
        json::Value thumbs = json::Value::object();
        json::Value high = json::Value::object();
        high.set("url", std::string{"https://i.ytimg.com/vi/"} + v.id + "/hqdefault.jpg");
        high.set("width", 480);
        high.set("height", 360);
        thumbs.set("high", high);
        snippet.set("thumbnails", thumbs);
        snippet.set("liveBroadcastContent", "none");
        item.set("snippet", snippet);
        json::Value details = json::Value::object();
        details.set("duration", v.duration);
        item.set("contentDetails", details);
        json::Value stats = json::Value::object();
        stats.set("viewCount", v.views);
        item.set("statistics", stats);
        items.push(item);
    }
    json::Value root = json::Value::object();
    root.set("kind", "youtube#videoListResponse");
    root.set("items", items);
    return root.dump();
}

std::string youtube_search_response()
{
    json::Value items = json::Value::array();
    json::Value video = json::Value::object();
    json::Value id = json::Value::object();
    id.set("kind", "youtube#video");
    id.set("videoId", "aqz-KE-bpKQ");
    video.set("id", id);
    json::Value snippet = json::Value::object();
    snippet.set("title", "Big Buck Bunny 60fps 4K");
    snippet.set("channelTitle", "Blender");
    snippet.set("publishedAt", "2014-11-10T14:05:55Z");
    json::Value thumbs = json::Value::object();
    json::Value medium = json::Value::object();
    medium.set("url", "https://i.ytimg.com/vi/aqz-KE-bpKQ/mqdefault.jpg");
    thumbs.set("medium", medium);
    snippet.set("thumbnails", thumbs);
    video.set("snippet", snippet);
    items.push(video);
    json::Value channel = json::Value::object();
    json::Value cid = json::Value::object();
    cid.set("kind", "youtube#channel");
    cid.set("channelId", "UCSMOQeBJ2RAnuFungnQOxLg");
    channel.set("id", cid);
    json::Value cs = json::Value::object();
    cs.set("title", "Blender");
    cs.set("description", "The official channel of the Blender project.");
    cs.set("thumbnails", thumbs);
    channel.set("snippet", cs);
    items.push(channel);
    json::Value root = json::Value::object();
    root.set("items", items);
    return root.dump();
}

std::string youtube_error_response(const char *reason, int code)
{
    json::Value error = json::Value::object();
    error.set("code", code);
    error.set("message", std::string{"Request failed: "} + reason);
    json::Value errors = json::Value::array();
    json::Value e = json::Value::object();
    e.set("reason", reason);
    errors.push(e);
    error.set("errors", errors);
    json::Value root = json::Value::object();
    root.set("error", error);
    return root.dump();
}

std::string generated_png(int width, int height, unsigned seed)
{
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 3);
    const unsigned a = seed * 2654435761u, b = (seed + 7) * 2246822519u;
    const int r0 = static_cast<int>(a & 255), g0 = static_cast<int>((a >> 8) & 255),
              b0 = static_cast<int>((a >> 16) & 255);
    const int r1 = static_cast<int>(b & 255), g1 = static_cast<int>((b >> 8) & 255),
              b1 = static_cast<int>((b >> 16) & 255);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const int t = (x + y) * 255 / (width + height);
            const bool stripe = ((x / 24 + y / 24) % 7) == 0;
            unsigned char *p = &pixels[(static_cast<std::size_t>(y) * width + x) * 3];
            p[0] =
                static_cast<unsigned char>((r0 * (255 - t) + r1 * t) / 255 / (stripe ? 2 : 1) + 30);
            p[1] =
                static_cast<unsigned char>((g0 * (255 - t) + g1 * t) / 255 / (stripe ? 2 : 1) + 20);
            p[2] =
                static_cast<unsigned char>((b0 * (255 - t) + b1 * t) / 255 / (stripe ? 2 : 1) + 40);
        }
    std::string png;
    stbi_write_png_to_func(write_png, &png, width, height, 3, pixels.data(), width * 3);
    return png;
}

net::Response canned_transport(const net::Request &request)
{
    const std::string &url = request.url;
    if (url.starts_with("https://graphql.anilist.co"))
    {
        if (request.body.find("Media(id") != std::string::npos)
            return ok(anilist_details_response());
        if (request.body.find("SEARCH_MATCH") != std::string::npos)
            return ok(anilist_search_response());
        return ok(anilist_home_response());
    }
    if (url.starts_with("https://www.googleapis.com/youtube/v3/"))
    {
        if (url.find("/search?") != std::string::npos)
            return ok(youtube_search_response());
        return ok(youtube_videos_response());
    }
    if (url.ends_with(".jpg") || url.ends_with(".png"))
    {
        unsigned seed = 0;
        for (char c : url)
            seed = seed * 31 + static_cast<unsigned char>(c);
        const bool portrait = url.find("/cover/") != std::string::npos;
        return ok(generated_png(portrait ? 230 : 480, portrait ? 345 : 270, seed), "image/png");
    }
    net::Response r;
    r.outcome = net::Outcome::network_error;
    r.error = "offline (canned transport)";
    return r;
}
} // namespace akeno::test
