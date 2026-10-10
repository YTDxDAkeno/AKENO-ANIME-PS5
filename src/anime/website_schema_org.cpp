// AKENO STREAM PS5 - Website adapter: schema.org structured data (JSON-LD).
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Many sites describe their catalogue for search engines with schema.org
// JSON-LD (https://schema.org, https://json-ld.org). This adapter reads the
// documented types and properties and nothing else:
//
//   catalogue  ItemList (itemListElement / ListItem: item, url, name,
//              position), TVSeries, Movie, CreativeWorkSeries on the page;
//              <link rel="next"> for the next page.
//   search     WebSite.potentialAction SearchAction: target urlTemplate and
//              query-input ("required name=search_term_string").
//   a series   TVSeries: name, alternateName, description, image, genre,
//              startDate / datePublished, containsSeason (TVSeason:
//              seasonNumber, name, numberOfEpisodes, episode, url),
//              episode (TVEpisode: episodeNumber, name, description, image /
//              thumbnailUrl, duration, datePublished, url, video,
//              associatedMedia).
//   a film     Movie: one episode, from its video (never its trailer).
//   videos     VideoObject / MediaObject: contentUrl, encodingFormat,
//              duration, thumbnailUrl, caption (subtitles), inLanguage,
//              regionsAllowed; isAccessibleForFree false or
//              requiresSubscription marks it as for subscribers.
//
// Only contentUrl names a video file. embedUrl is a player page and is never
// played; an episode without a contentUrl is listed but not playable, and an
// episode page is read for its own structured data only when it plays.
#include "anime/website.hpp"

#include "anime/catalog_format.hpp"
#include "core/url.hpp"
#include "providers/provider.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <set>

namespace akeno::anime
{
namespace
{
constexpr std::size_t kMaxBlocks = 32;
constexpr std::size_t kMaxBlockBytes = 1024u * 1024u;
constexpr std::size_t kMaxEntriesPerPage = 200;
constexpr int kMaxPagesPerSeries = 24;
constexpr std::size_t kMaxAddress = 4096;

std::string lower(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view strip_type_prefix(std::string_view type)
{
    for (std::string_view prefix : {"https://schema.org/", "http://schema.org/", "schema:"})
        if (type.substr(0, prefix.size()) == prefix)
            return type.substr(prefix.size());
    return type;
}

// ---------------------------------------------------------------------------
// HTML: just enough to find <script type="application/ld+json"> and
// <link rel="next">. Attribute values are read with or without quotes.
using Attributes = std::vector<std::pair<std::string, std::string>>;

std::string decode_entities(std::string text)
{
    static const std::pair<const char *, const char *> kEntities[] = {
        {"&amp;", "&"}, {"&quot;", "\""}, {"&#39;", "'"},  {"&apos;", "'"},
        {"&lt;", "<"},  {"&gt;", ">"},    {"&#x2F;", "/"}, {"&#47;", "/"}};
    for (const auto &[from, to] : kEntities)
        for (std::size_t p = 0; (p = text.find(from, p)) != std::string::npos; p += 1)
            text.replace(p, std::strlen(from), to);
    return text;
}

// Reads the attributes of a tag starting just after its name; *end receives
// the position after '>'.
Attributes read_attributes(std::string_view html, std::size_t at, std::size_t *end)
{
    Attributes out;
    std::size_t i = at;
    const auto space = [&html](std::size_t p)
    { return p < html.size() && std::isspace(static_cast<unsigned char>(html[p])); };
    while (i < html.size() && out.size() < 32)
    {
        while (space(i))
            ++i;
        if (i >= html.size() || html[i] == '>')
            break;
        if (html[i] == '/')
        {
            ++i;
            continue;
        }
        std::size_t name_start = i;
        while (i < html.size() && !space(i) && html[i] != '=' && html[i] != '>' && html[i] != '/')
            ++i;
        std::string name = lower(html.substr(name_start, i - name_start));
        while (space(i))
            ++i;
        std::string value;
        if (i < html.size() && html[i] == '=')
        {
            ++i;
            while (space(i))
                ++i;
            if (i < html.size() && (html[i] == '"' || html[i] == '\''))
            {
                const char quote = html[i++];
                const std::size_t close = html.find(quote, i);
                if (close == std::string_view::npos)
                    break;
                value = std::string{html.substr(i, close - i)};
                i = close + 1;
            }
            else
            {
                const std::size_t start = i;
                while (i < html.size() && !space(i) && html[i] != '>')
                    ++i;
                value = std::string{html.substr(start, i - start)};
            }
        }
        if (!name.empty())
            out.emplace_back(std::move(name), decode_entities(std::move(value)));
    }
    const std::size_t close = html.find('>', i);
    *end = close == std::string_view::npos ? html.size() : close + 1;
    return out;
}

std::string attribute(const Attributes &list, std::string_view name)
{
    for (const auto &[key, value] : list)
        if (key == name)
            return value;
    return {};
}

bool has_token(const std::string &value, std::string_view token)
{
    std::size_t i = 0;
    const std::string v = lower(value);
    while (i < v.size())
    {
        while (i < v.size() && std::isspace(static_cast<unsigned char>(v[i])))
            ++i;
        std::size_t start = i;
        while (i < v.size() && !std::isspace(static_cast<unsigned char>(v[i])))
            ++i;
        if (v.substr(start, i - start) == token)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// JSON-LD values.
const std::string &text_of(const json::Value &v)
{
    if (v.is_string())
        return v.str();
    if (v.is_array())
        for (const json::Value &x : v.items())
            if (x.is_string() && !x.str().empty())
                return x.str();
    if (v.is_object())
    {
        if (v["@value"].is_string())
            return v["@value"].str();
        return v["name"].str();
    }
    return json::Value::empty_string();
}

std::string clean_text(const json::Value &v, std::size_t limit)
{
    std::string out = strip_html(text_of(v));
    if (out.size() > limit)
        out.resize(limit);
    return out;
}

std::vector<std::string> texts_of(const json::Value &v, std::size_t limit)
{
    std::vector<std::string> out;
    const auto add = [&out, limit](const json::Value &x)
    {
        const std::string t = strip_html(text_of(x));
        if (!t.empty() && t.size() <= 120 && out.size() < limit &&
            std::find(out.begin(), out.end(), t) == out.end())
            out.push_back(t);
    };
    if (v.is_array())
        for (const json::Value &x : v.items())
            add(x);
    else
        add(v);
    return out;
}

int number_of(const json::Value &v)
{
    if (v.is_number())
        return v.num() >= 0 && v.num() < 100000 ? static_cast<int>(v.num()) : 0;
    if (v.is_string() && !v.str().empty() && v.str().size() < 7 &&
        std::all_of(v.str().begin(), v.str().end(),
                    [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        return std::atoi(v.str().c_str());
    return 0;
}

int year_of(const json::Value &v)
{
    const std::string &t = text_of(v);
    if (t.size() >= 4 && std::all_of(t.begin(), t.begin() + 4, [](char c)
                                     { return std::isdigit(static_cast<unsigned char>(c)); }))
    {
        const int y = std::atoi(t.substr(0, 4).c_str());
        return y >= 1900 && y <= 2200 ? y : 0;
    }
    return 0;
}

std::string date_of(const json::Value &v)
{
    const std::string &t = text_of(v);
    return t.size() >= 10 && t[4] == '-' && t[7] == '-' ? t.substr(0, 10) : std::string{};
}

bool truthy(const json::Value &v)
{
    if (v.is_bool())
        return v.boolean();
    if (v.is_string())
        return !v.str().empty() && lower(v.str()) != "false";
    return v.is_object() || (v.is_array() && v.size() > 0);
}

bool falsy_flag(const json::Value &v)
{
    return (v.is_bool() && !v.boolean()) || (v.is_string() && lower(v.str()) == "false");
}

// An http(s) address from a reference (resolved against the page).
std::string address_of(const std::string &base, const json::Value &v)
{
    std::string reference;
    if (v.is_string())
        reference = v.str();
    else if (v.is_object())
        reference = v["url"].is_string() ? v["url"].str() : v["@id"].str();
    else if (v.is_array())
        for (const json::Value &x : v.items())
            if (!(reference = address_of(base, x)).empty())
                return reference;
    if (reference.empty() || reference.size() > kMaxAddress)
        return {};
    const auto resolved = url::resolve(base, reference);
    if (!resolved)
        return {};
    const auto u = url::parse(*resolved);
    return u && u->is_http() ? *resolved : std::string{};
}

std::string image_of(const std::string &base, const json::Value &v)
{
    if (v.is_array())
    {
        for (const json::Value &x : v.items())
            if (std::string a = image_of(base, x); !a.empty())
                return a;
        return {};
    }
    if (v.is_object())
        return address_of(base, v["url"].is_string() ? v["url"] : v["contentUrl"]);
    return address_of(base, v);
}

const json::Value &first_of(const json::Value &v)
{
    return v.is_array() ? v[0] : v;
}

// The values of a property that may be one value or a list of them, as
// references into the parsed page (missing: none).
std::vector<std::reference_wrapper<const json::Value>> each(const json::Value &v)
{
    std::vector<std::reference_wrapper<const json::Value>> out;
    if (v.is_array())
        for (const json::Value &x : v.items())
            out.emplace_back(x);
    else if (!v.is_null())
        out.emplace_back(v);
    return out;
}

std::string id_from(const std::string &address, const std::string &fallback)
{
    if (const auto u = url::parse(address))
    {
        std::string id = slug(u->path + (u->has_query ? " " + u->query : std::string{}));
        if (!id.empty())
            return id;
    }
    return slug(fallback);
}

std::string unique_id(std::string id, std::set<std::string> &taken)
{
    if (id.empty())
        id = "item";
    std::string out = id;
    for (int n = 2; !taken.insert(out).second; ++n)
        out = id + "-" + std::to_string(n);
    return out;
}

// ---------------------------------------------------------------------------
// From nodes to series, seasons and episodes.
void collect_nodes(const json::Value &v, std::vector<const json::Value *> &out, int depth)
{
    if (depth > 4 || out.size() > 2000)
        return;
    if (v.is_array())
    {
        for (const json::Value &x : v.items())
            collect_nodes(x, out, depth + 1);
        return;
    }
    if (!v.is_object())
        return;
    if (v.has("@graph"))
        collect_nodes(v["@graph"], out, depth + 1);
    if (v.has("@type"))
        out.push_back(&v);
    // A search results or collection page names its list as its main entity.
    if (v.has("mainEntity"))
        collect_nodes(v["mainEntity"], out, depth + 1);
}

bool is_series_type(const json::Value &n)
{
    return schema::is_type(n, "TVSeries") || schema::is_type(n, "CreativeWorkSeries") ||
           schema::is_type(n, "MovieSeries") || schema::is_type(n, "Movie");
}

bool is_video_type(const json::Value &n)
{
    return schema::is_type(n, "VideoObject") || schema::is_type(n, "MediaObject");
}

std::string encoding_container(const std::string &format)
{
    const std::string f = lower(format);
    if (f.find("mpegurl") != std::string::npos)
        return "hls";
    if (f.find("mp2t") != std::string::npos)
        return "ts";
    if (f.find("matroska") != std::string::npos)
        return "mkv";
    if (f.find("mp4") != std::string::npos)
        return "mp4";
    return {};
}

struct Reader
{
    std::string base; // the page these nodes came from

    // The video file of an episode, film or video, when the site names one.
    std::optional<MediaResource> media(const json::Value &node, int depth = 0) const
    {
        if (depth > 2 || !node.is_object())
            return std::nullopt;
        if (is_video_type(node) || node.has("contentUrl"))
        {
            const std::string address = address_of(base, node["contentUrl"]);
            if (!address.empty())
            {
                MediaResource m;
                m.url = address;
                m.container = encoding_container(text_of(node["encodingFormat"]));
                m.kind = media_kind(address, m.container);
                for (const json::Value &c : each(node["caption"]))
                    if (c.is_object() && m.subtitles.size() < 16)
                    {
                        SubtitleTrack t;
                        t.url = address_of(base,
                                           c["contentUrl"].is_null() ? c["url"] : c["contentUrl"]);
                        const std::string format = lower(text_of(c["encodingFormat"]));
                        t.format = format.find("srt") != std::string::npos   ? "srt"
                                   : format.find("vtt") != std::string::npos ? "vtt"
                                   : t.url.ends_with(".srt")                 ? "srt"
                                                                             : "vtt";
                        t.language = text_of(c["inLanguage"]);
                        t.label = clean_text(c["name"], 40);
                        if (t.label.empty())
                            t.label = t.language;
                        if (!t.url.empty())
                            m.subtitles.push_back(std::move(t));
                    }
                if (const std::string language = text_of(node["inLanguage"]); !language.empty())
                    m.audio.push_back({language, language});
                for (const std::string &r : texts_of(node["regionsAllowed"], 250))
                    if (r.size() == 2)
                        m.regions.push_back(std::string{static_cast<char>(std::toupper(r[0])),
                                                        static_cast<char>(std::toupper(r[1]))});
                m.requires_auth =
                    falsy_flag(node["isAccessibleForFree"]) || truthy(node["requiresSubscription"]);
                return m;
            }
        }
        for (const char *property : {"video", "associatedMedia", "encoding"})
        {
            const json::Value &v = node[property];
            for (const json::Value &x : each(v))
                if (auto m = media(x, depth + 1))
                {
                    // The episode's own flags count for its video too.
                    m->requires_auth = m->requires_auth ||
                                       falsy_flag(node["isAccessibleForFree"]) ||
                                       truthy(node["requiresSubscription"]);
                    return m;
                }
        }
        return std::nullopt;
    }

    std::optional<Episode> episode(const json::Value &node, int number) const
    {
        if (!node.is_object())
            return std::nullopt;
        Episode e;
        e.title = clean_text(node["name"], 200);
        e.number = number_of(node["episodeNumber"]);
        if (!e.number)
            e.number = number;
        e.description = clean_text(node["description"], 600);
        e.thumbnail =
            image_of(base, node.has("thumbnailUrl") ? node["thumbnailUrl"] : node["image"]);
        const json::Value &video = first_of(node["video"]);
        if (e.thumbnail.empty())
            e.thumbnail = image_of(base, video["thumbnailUrl"]);
        std::string duration = text_of(node["duration"]);
        if (duration.empty())
            duration = text_of(video["duration"]);
        e.duration = parse_iso8601_duration(duration);
        e.air_date = date_of(node["datePublished"]);
        if (e.air_date.empty())
            e.air_date = date_of(node["uploadDate"]);
        const std::string page = address_of(base, node["url"]);
        e.media = media(node);
        if (e.media)
        {
            e.availability = Availability::unverified;
            e.availability_detail = "The site names a video file; checked before it plays.";
        }
        else if (!page.empty() && page != base)
        {
            // The episode's own page may name its file: read when it plays.
            e.source_ref = page;
            e.availability = Availability::unverified;
            e.availability_detail = "Looked up on the episode's page when it plays.";
        }
        else
        {
            e.availability = Availability::episodes_indexed;
            e.availability_detail =
                node.has("embedUrl") || first_of(node["video"]).has("embedUrl")
                    ? "The site offers this episode in a web player only (Websites)."
                    : "The site lists this episode without a video file.";
        }
        e.id = id_from(page, e.number ? "e" + std::to_string(e.number) : e.title);
        return e;
    }

    // Episodes listed in a node's "episode" (or "hasPart" of TVEpisodes).
    std::vector<Episode> episodes(const json::Value &node) const
    {
        std::vector<Episode> out;
        std::set<std::string> ids;
        const json::Value &list = node.has("episode") ? node["episode"] : node["episodes"];
        int position = 0;
        for (const json::Value &x : each(list))
        {
            ++position;
            const json::Value &item = x.has("item") ? x["item"] : x;
            if (out.size() >= kMaxEpisodesPerSeries)
                break;
            // A list position counts as the episode's number only in an ItemList.
            if (auto e = episode(item, x.has("item") ? number_of(x["position"]) : 0))
            {
                if (!e->title.empty() || e->number || e->media || !e->source_ref.empty())
                {
                    e->id = unique_id(e->id.empty() ? "e" + std::to_string(position) : e->id, ids);
                    out.push_back(std::move(*e));
                }
            }
        }
        return out;
    }

    Season season(const json::Value &node, int index) const
    {
        Season s;
        s.number = number_of(node["seasonNumber"]);
        if (!s.number)
            s.number = index;
        s.title = clean_text(node["name"], 120);
        if (s.title.empty())
            s.title = "Season " + std::to_string(s.number);
        s.year = year_of(node.has("startDate") ? node["startDate"] : node["datePublished"]);
        s.episodes = episodes(node);
        s.episode_count =
            std::max(number_of(node["numberOfEpisodes"]), static_cast<int>(s.episodes.size()));
        const std::string page = address_of(base, node["url"]);
        s.id =
            page.empty() || page == base ? "s" + std::to_string(s.number) : id_from(page, s.title);
        return s;
    }

    // What a catalogue entry or a series page says about a series. Seasons
    // and episodes given inline are read; seasons that only name their page
    // keep it in their id for series() to load.
    Series series(const json::Value &node) const
    {
        Series s;
        s.title = clean_text(node["name"], 200);
        s.alt_titles = texts_of(node["alternateName"], 6);
        s.description = clean_text(node["description"], 1200);
        s.poster = image_of(base, node["image"]);
        if (s.poster.empty())
            s.poster = image_of(base, node["thumbnailUrl"]);
        s.banner = s.poster;
        s.genres = texts_of(node["genre"], 8);
        s.year = year_of(node.has("startDate") ? node["startDate"] : node["datePublished"]);
        if (!s.year)
            s.year = year_of(node["dateCreated"]);
        s.score = 0;
        s.attribution = clean_text(node["publisher"], 80);
        s.source_ref = address_of(base, node["url"]);
        if (schema::is_type(node, "Movie"))
        {
            Season film;
            film.id = "film";
            film.number = 1;
            film.title = "Film";
            if (auto e = episode(node, 1))
            {
                e->id = "film";
                if (e->title.empty())
                    e->title = s.title;
                film.episodes.push_back(std::move(*e));
            }
            film.episode_count = 1;
            s.seasons.push_back(std::move(film));
            return s;
        }
        int index = 0;
        std::set<std::string> ids;
        const json::Value &seasons = node["containsSeason"];
        for (const json::Value &x : each(seasons))
        {
            if (!x.is_object() || s.seasons.size() >= kMaxSeasonsPerSeries)
                continue;
            Season season = this->season(x, ++index);
            season.id = unique_id(season.id, ids);
            s.seasons.push_back(std::move(season));
        }
        std::vector<Episode> direct = episodes(node);
        if (!direct.empty())
        {
            Season one;
            one.id = unique_id("s1", ids);
            one.number = 1;
            one.title = s.seasons.empty() ? "Episodes" : "More episodes";
            one.episodes = std::move(direct);
            one.episode_count = static_cast<int>(one.episodes.size());
            s.seasons.push_back(std::move(one));
        }
        return s;
    }

    // Videos on a page that lists no series: one collection of them.
    std::optional<Series> collection(const std::vector<const json::Value *> &nodes,
                                     const std::string &name) const
    {
        Season season;
        season.id = "videos";
        season.number = 1;
        season.title = "Videos";
        std::set<std::string> ids;
        for (const json::Value *n : nodes)
        {
            if (!(is_video_type(*n) || schema::is_type(*n, "TVEpisode")) ||
                season.episodes.size() >= kMaxEpisodesPerSeries)
                continue;
            if (auto e = episode(*n, 0))
            {
                if (e->title.empty() && !e->media)
                    continue;
                e->id = unique_id(e->id, ids);
                season.episodes.push_back(std::move(*e));
            }
        }
        if (season.episodes.empty())
            return std::nullopt;
        // The page's order is the site's; unnumbered videos are counted in it.
        int n = 0;
        for (Episode &e : season.episodes)
            if (!e.number)
                e.number = ++n;
        season.episode_count = static_cast<int>(season.episodes.size());
        Series s;
        s.title = name.empty() ? "Videos" : name;
        s.id = "videos";
        if (!season.episodes.front().thumbnail.empty())
            s.poster = s.banner = season.episodes.front().thumbnail;
        s.seasons.push_back(std::move(season));
        return s;
    }
};

struct PageData
{
    std::vector<json::Value> blocks; // owns the nodes
    std::vector<const json::Value *> nodes;
    std::string next;
};

PageData read_page(const SitePage &page)
{
    PageData out;
    out.blocks = schema::json_ld_blocks(page.body);
    for (const json::Value &b : out.blocks)
        collect_nodes(b, out.nodes, 0);
    out.next = schema::next_link(page.body, page.url);
    return out;
}

std::string site_name(const std::vector<const json::Value *> &nodes)
{
    for (const json::Value *n : nodes)
        if (schema::is_type(*n, "WebSite") || schema::is_type(*n, "Organization"))
            if (std::string name = clean_text((*n)["name"], 80); !name.empty())
                return name;
    return {};
}

// "https://example.com/search?q={search_term_string}" from a SearchAction.
std::string search_template(const std::vector<const json::Value *> &nodes, const std::string &base)
{
    for (const json::Value *n : nodes)
    {
        if (!schema::is_type(*n, "WebSite"))
            continue;
        const json::Value &actions = (*n)["potentialAction"];
        for (const json::Value &a : each(actions))
        {
            if (!schema::is_type(a, "SearchAction"))
                continue;
            std::string placeholder = "search_term_string";
            const json::Value &input = a["query-input"];
            const std::string spec = input.is_object() ? input["valueName"].str() : text_of(input);
            if (const std::size_t at = spec.find("name="); at != std::string::npos)
                placeholder = spec.substr(at + 5, spec.find(' ', at) - at - 5);
            else if (input.is_object() && !spec.empty())
                placeholder = spec;
            const json::Value &target = a["target"];
            for (const json::Value &t : each(target))
            {
                std::string templ = t.is_object() ? t["urlTemplate"].str() : t.str();
                if (templ.find("{" + placeholder + "}") == std::string::npos)
                    continue;
                // Resolved with the placeholder in place, then checked.
                templ.replace(templ.find("{" + placeholder + "}"), placeholder.size() + 2,
                              "AKENOQUERY");
                const std::string resolved = address_of(base, json::Value{templ});
                if (!resolved.empty() && resolved.find("AKENOQUERY") != std::string::npos)
                    return resolved;
            }
        }
    }
    return {};
}

std::string setting(const ProviderConfig &config, const char *key)
{
    const auto it = config.settings.find(key);
    return it == config.settings.end() ? std::string{} : it->second;
}

// The catalogue entries a page lists.
SiteListing listing_from(const SitePage &page, const ProviderConfig &config)
{
    SiteListing out;
    const PageData data = read_page(page);
    const Reader reader{page.url};
    std::set<std::string> ids, seen;
    const auto add = [&](Series s)
    {
        if (s.title.empty() || out.series.size() >= kMaxEntriesPerPage)
            return;
        const std::string key = s.source_ref.empty() ? s.title : s.source_ref;
        if (!seen.insert(key).second)
            return;
        bool has_episodes = false, season_pages = false;
        for (const Season &season : s.seasons)
        {
            has_episodes = has_episodes || !season.episodes.empty();
            season_pages = season_pages || season.episodes.empty();
        }
        const bool own_page = !s.source_ref.empty() && s.source_ref != page.url &&
                              same_site(s.source_ref, config.address);
        // Listed here; seasons and episodes come from the series' own page
        // (or, on that page, from the pages of seasons listed without episodes).
        s.details_pending = (own_page && (!has_episodes || s.seasons.size() < 2)) ||
                            (s.source_ref == page.url && season_pages);
        if (s.details_pending)
            s.seasons.clear();
        if (!own_page && !s.source_ref.empty() && s.source_ref != page.url)
            return; // another site's series: not this provider's
        s.id =
            unique_id(s.source_ref.empty() ? slug(s.title) : id_from(s.source_ref, s.title), ids);
        out.series.push_back(std::move(s));
    };
    std::vector<const json::Value *> videos = data.nodes; // for a collection
    for (const json::Value *n : data.nodes)
    {
        if (!schema::is_type(*n, "ItemList"))
            continue;
        const json::Value &list = (*n)["itemListElement"];
        for (const json::Value &element : each(list))
        {
            const json::Value &item = element.has("item") ? element["item"] : element;
            Series s;
            if (item.is_object() && (item.has("@type") || item.has("name")))
                s = reader.series(item);
            if (s.title.empty())
                s.title = clean_text(element["name"], 200);
            if (s.source_ref.empty())
                s.source_ref = address_of(page.url, item.is_string() ? item : element["url"]);
            if (s.poster.empty())
                s.poster = image_of(page.url, element["image"]);
            if (item.is_object() && (is_video_type(item) || schema::is_type(item, "TVEpisode")))
            {
                videos.push_back(&item); // a video in a list: part of a collection, below
                continue;
            }
            add(std::move(s));
        }
    }
    for (const json::Value *n : data.nodes)
        if (is_series_type(*n))
            add(reader.series(*n));
    if (out.series.empty())
        if (auto c = reader.collection(videos, site_name(data.nodes)))
        {
            Series s = std::move(*c);
            s.source_ref = page.url;
            s.id = unique_id(id_from(page.url, s.title), ids);
            out.series.push_back(std::move(s));
        }
    if (!data.next.empty() && same_site(data.next, config.address) && data.next != page.url)
        out.next_page = data.next;
    out.ok = true;
    return out;
}

class SchemaOrgAdapter final : public WebsiteAdapter
{
  public:
    [[nodiscard]] const char *id() const noexcept override
    {
        return "schema-org";
    }
    [[nodiscard]] const char *label() const noexcept override
    {
        return "Structured data (schema.org)";
    }
    [[nodiscard]] int version() const noexcept override
    {
        return 1;
    }

    SiteDetection detect(const SitePage &page, const net::CancelFlag &) const override
    {
        SiteDetection d;
        const PageData data = read_page(page);
        if (data.blocks.empty())
        {
            d.why_not = "The page has no schema.org structured data (JSON-LD).";
            return d;
        }
        std::map<std::string, int> types;
        for (const json::Value *n : data.nodes)
        {
            const json::Value &t = (*n)["@type"];
            for (const json::Value &x : each(t))
                if (x.is_string())
                    ++types[std::string{strip_type_prefix(x.str())}];
        }
        ProviderConfig probe;
        probe.address = page.url;
        const SiteListing listing = listing_from(page, probe);
        std::string found;
        for (const auto &[type, n] : types)
            found += (found.empty() ? "" : ", ") + std::to_string(n) + " " + type;
        d.evidence.push_back("JSON-LD: " + found);
        if (listing.series.empty())
        {
            d.why_not =
                "The page's structured data (" + found + ") describes no series, films or videos.";
            return d;
        }
        d.recognised = true;
        d.homepage = page.url;
        d.site_name = site_name(data.nodes);
        d.format = "website with schema.org structured data";
        d.settings["catalogue"] = page.url;
        const std::string search = search_template(data.nodes, page.url);
        if (!search.empty() && same_site(search, page.url))
        {
            d.settings["search"] = search;
            d.evidence.push_back("Search: SearchAction " + url::redact(search));
        }
        if (!listing.next_page.empty())
            d.evidence.push_back("More pages: rel=\"next\"");
        d.evidence.push_back(std::to_string(listing.series.size()) +
                             (listing.series.size() == 1 ? " entry" : " entries") +
                             " on the first page");
        ProviderConfig c;
        c.settings = d.settings;
        d.capabilities = capabilities(c);
        d.capabilities.pagination = !listing.next_page.empty();
        bool seasons = false, episodes = false;
        std::vector<const json::Value *> described = data.nodes;
        for (const json::Value *n : data.nodes)
            if (schema::is_type(*n, "ItemList"))
                for (const json::Value &element : each((*n)["itemListElement"]))
                    if (element["item"].is_object())
                        described.push_back(&element["item"]);
        for (const json::Value *n : described)
        {
            seasons = seasons || schema::is_type(*n, "TVSeries") || schema::is_type(*n, "TVSeason");
            episodes = episodes || schema::is_type(*n, "TVSeries") ||
                       schema::is_type(*n, "TVEpisode") || is_video_type(*n) ||
                       schema::is_type(*n, "Movie");
        }
        for (const Series &s : listing.series)
            episodes = episodes || s.details_pending || !s.seasons.empty();
        d.capabilities.seasons = seasons;
        d.capabilities.episodes = episodes;
        return d;
    }

    [[nodiscard]] SiteCapabilities capabilities(const ProviderConfig &config) const override
    {
        SiteCapabilities c;
        c.catalogue = true;
        c.series_details = true;
        c.search = !setting(config, "search").empty();
        c.pagination = true; // when the site links a next page
        c.seasons = c.episodes = true;
        return c;
    }

    SiteListing catalogue(const ProviderConfig &config, const std::string &page,
                          const ProviderContext &, const net::CancelFlag &cancel) const override
    {
        const std::string start =
            setting(config, "catalogue").empty() ? config.address : setting(config, "catalogue");
        return list(config, page.empty() ? start : page, cancel);
    }

    SiteListing search(const ProviderConfig &config, const std::string &query,
                       const std::string &page, const ProviderContext &,
                       const net::CancelFlag &cancel) const override
    {
        const std::string templ = setting(config, "search");
        if (templ.empty() || templ.find("AKENOQUERY") == std::string::npos)
        {
            SiteListing out;
            out.error = "The site offers no search.";
            out.error_kind = SiteError::unsupported;
            return out;
        }
        std::string address = templ;
        address.replace(address.find("AKENOQUERY"), 10, url::encode_component(query));
        return list(config, page.empty() ? address : page, cancel);
    }

    SiteSeries series(const ProviderConfig &config, const Series &entry, const ProviderContext &,
                      const net::CancelFlag &cancel) const override
    {
        SiteSeries out;
        if (!entry.details_pending || entry.source_ref.empty())
        {
            out.series = entry;
            out.ok = true;
            return out;
        }
        if (!same_site(entry.source_ref, config.address))
        {
            out.error = "The series page is on another site.";
            out.error_kind = SiteError::unsupported;
            return out;
        }
        const SitePage page = fetch_site_page(entry.source_ref, cancel);
        if (!page.ok)
        {
            out.error = page.error;
            out.error_kind = page.error_kind;
            return out;
        }
        const PageData data = read_page(page);
        const Reader reader{page.url};
        const json::Value *best = nullptr;
        for (const json::Value *n : data.nodes)
            if (is_series_type(*n) || schema::is_type(*n, "TVSeason"))
            {
                const std::string own = address_of(page.url, (*n)["url"]);
                const bool matches = own == page.url || own == entry.source_ref ||
                                     clean_text((*n)["name"], 200) == entry.title;
                if (!best || (matches && !schema::is_type(*best, "TVSeries")))
                    best = n;
                if (matches && schema::is_type(*n, "TVSeries"))
                    break;
            }
        if (best && schema::is_type(*best, "TVSeason"))
        {
            // A page for one season: that season is the series.
            out.series.title = entry.title;
            Season s = reader.season(*best, 1);
            out.series.seasons.push_back(std::move(s));
        }
        else if (best)
            out.series = reader.series(*best);
        else if (auto c = reader.collection(data.nodes, entry.title))
            out.series = std::move(*c);
        else
        {
            out.error = "The series page describes no seasons or episodes.";
            out.error_kind = SiteError::unsupported;
            return out;
        }
        // Seasons that name only their own page are read from it.
        int budget = kMaxPagesPerSeries;
        for (Season &season : out.series.seasons)
        {
            if (!best || !season.episodes.empty() || (cancel && cancel->load()) || budget <= 0)
                continue;
            const std::string address = season_page(*best, season, reader);
            if (address.empty() || !same_site(address, config.address))
                continue;
            --budget;
            const SitePage sp = fetch_site_page(address, cancel);
            if (!sp.ok)
            {
                out.warnings.push_back({season.title, "could not be read: " + sp.error});
                continue;
            }
            const PageData sd = read_page(sp);
            const Reader sr{sp.url};
            for (const json::Value *n : sd.nodes)
                if (schema::is_type(*n, "TVSeason"))
                {
                    Season loaded = sr.season(*n, season.number);
                    season.episodes = std::move(loaded.episodes);
                    season.episode_count =
                        std::max(season.episode_count, static_cast<int>(season.episodes.size()));
                    break;
                }
        }
        // Seasons in order; specials (0) last.
        std::stable_sort(out.series.seasons.begin(), out.series.seasons.end(),
                         [](const Season &a, const Season &b)
                         {
                             const int x = a.number == 0 ? 1 << 20 : a.number;
                             const int y = b.number == 0 ? 1 << 20 : b.number;
                             return x < y;
                         });
        for (Season &season : out.series.seasons)
            if (std::all_of(season.episodes.begin(), season.episodes.end(),
                            [](const Episode &e) { return e.number > 0; }))
                std::stable_sort(season.episodes.begin(), season.episodes.end(),
                                 [](const Episode &a, const Episode &b)
                                 { return a.number < b.number; });
        out.ok = true;
        return out;
    }

    Resolution resolve(const ProviderConfig &config, const Series &, const Season &,
                       const Episode &episode, const ProviderContext &,
                       const net::CancelFlag &cancel) const override
    {
        Resolution r;
        if (episode.media)
        {
            r.media = *episode.media;
            r.ok = episode.availability == Availability::unverified ||
                   episode.availability == Availability::playable ||
                   episode.availability == Availability::network_error;
            r.availability = r.ok ? Availability::unverified : episode.availability;
            r.detail = episode.availability_detail;
            return r;
        }
        if (episode.source_ref.empty())
        {
            r.availability = Availability::episodes_indexed;
            r.detail = episode.availability_detail.empty()
                           ? "The site lists this episode without a video file."
                           : episode.availability_detail;
            return r;
        }
        if (!same_site(episode.source_ref, config.address))
        {
            r.availability = Availability::provider_unsupported;
            r.detail = "The episode's page is on another site.";
            return r;
        }
        const SitePage page = fetch_site_page(episode.source_ref, cancel);
        if (!page.ok)
        {
            r.availability = page.error_kind == SiteError::not_found ? Availability::missing
                             : page.error_kind == SiteError::auth_required
                                 ? Availability::auth_required
                                 : Availability::network_error;
            r.detail = page.error;
            return r;
        }
        const PageData data = read_page(page);
        const Reader reader{page.url};
        std::optional<MediaResource> found;
        for (const json::Value *n : data.nodes)
        {
            if (!(schema::is_type(*n, "TVEpisode") || schema::is_type(*n, "Movie") ||
                  is_video_type(*n)))
                continue;
            if (auto m = reader.media(*n))
            {
                const std::string own = address_of(page.url, (*n)["url"]);
                found = std::move(m);
                if (own.empty() || own == page.url)
                    break; // the page's own episode
            }
        }
        if (!found)
        {
            r.availability = Availability::provider_unsupported;
            r.detail = "The episode's page names no video file AKENO STREAM can play (a web "
                       "player stays in the browser).";
            return r;
        }
        if (found->requires_auth)
        {
            r.availability = Availability::auth_required;
            r.detail = "The site marks this video as for subscribers or signed-in viewers only.";
            return r;
        }
        r.media = std::move(*found);
        r.ok = true;
        r.availability = Availability::unverified;
        r.detail = "Named on the episode's page; checked before it plays.";
        return r;
    }

  private:
    static SiteListing list(const ProviderConfig &config, const std::string &address,
                            const net::CancelFlag &cancel)
    {
        SiteListing out;
        if (!same_site(address, config.address))
        {
            out.error = "The page is on another site.";
            out.error_kind = SiteError::unsupported;
            return out;
        }
        const SitePage page = fetch_site_page(address, cancel);
        if (!page.ok)
        {
            out.error = page.error;
            out.error_kind = page.error_kind;
            return out;
        }
        return listing_from(page, config);
    }

    // A season's own page, when the series page names one.
    static std::string season_page(const json::Value &series, const Season &season,
                                   const Reader &reader)
    {
        const json::Value &seasons = series["containsSeason"];
        int index = 0;
        for (const json::Value &x : each(seasons))
        {
            ++index;
            const int number = number_of(x["seasonNumber"]) ? number_of(x["seasonNumber"]) : index;
            if (number == season.number)
                return address_of(reader.base, x["url"]);
        }
        return {};
    }
};
} // namespace

namespace schema
{
bool is_type(const json::Value &node, std::string_view type)
{
    const json::Value &t = node["@type"];
    if (t.is_string())
        return strip_type_prefix(t.str()) == type;
    for (const json::Value &x : t.items())
        if (x.is_string() && strip_type_prefix(x.str()) == type)
            return true;
    return false;
}

std::vector<json::Value> json_ld_blocks(std::string_view html)
{
    std::vector<json::Value> out;
    if (html.size() > kMaxSitePageBytes)
        html = html.substr(0, kMaxSitePageBytes);
    const std::string folded = lower(html);
    std::size_t at = 0;
    while (out.size() < kMaxBlocks)
    {
        const std::size_t open = folded.find("<script", at);
        if (open == std::string::npos)
            break;
        std::size_t body = 0;
        const Attributes attrs = read_attributes(html, open + 7, &body);
        const std::size_t close = folded.find("</script", body);
        if (close == std::string::npos)
            break;
        at = close + 8;
        const std::string type = lower(attribute(attrs, "type"));
        if (type.rfind("application/ld+json", 0) != 0 || close - body > kMaxBlockBytes)
            continue;
        std::string_view text = html.substr(body, close - body);
        // Some sites wrap the JSON in an HTML comment or CDATA section.
        for (std::string_view wrapper : {"<!--", "<![CDATA["})
        {
            std::size_t i = 0;
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
                ++i;
            if (text.substr(i, wrapper.size()) == wrapper)
                text = text.substr(i + wrapper.size());
        }
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
            text.remove_suffix(1);
        for (std::string_view tail : {"-->", "]]>"})
            if (text.size() >= tail.size() && text.substr(text.size() - tail.size()) == tail)
                text.remove_suffix(tail.size());
        json::ParseLimits limits;
        limits.max_bytes = kMaxBlockBytes;
        limits.max_depth = 32;
        auto parsed = json::parse(text, limits);
        if (parsed.ok && (parsed.value.is_object() || parsed.value.is_array()))
            out.push_back(std::move(parsed.value));
    }
    return out;
}

std::string next_link(std::string_view html, const std::string &page_url)
{
    if (html.size() > kMaxSitePageBytes)
        html = html.substr(0, kMaxSitePageBytes);
    const std::string folded = lower(html);
    for (const char *tag : {"<link", "<a "})
    {
        std::size_t at = 0;
        while ((at = folded.find(tag, at)) != std::string::npos)
        {
            std::size_t end = 0;
            const std::size_t name = at + std::strlen(tag);
            const Attributes attrs = read_attributes(html, name, &end);
            at = end;
            if (!has_token(attribute(attrs, "rel"), "next"))
                continue;
            const std::string href = attribute(attrs, "href");
            if (href.empty() || href.size() > kMaxAddress)
                continue;
            if (const auto resolved = url::resolve(page_url, href))
                if (const auto u = url::parse(*resolved); u && u->is_http())
                    return *resolved;
        }
    }
    return {};
}
} // namespace schema

const WebsiteAdapter &schema_org_adapter()
{
    static const SchemaOrgAdapter adapter;
    return adapter;
}
} // namespace akeno::anime
