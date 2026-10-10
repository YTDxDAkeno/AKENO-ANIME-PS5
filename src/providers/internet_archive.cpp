// AKENO STREAM PS5 - Public-domain films from the Internet Archive.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "providers/internet_archive.hpp"

#include "core/json.hpp"
#include "core/url.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace akeno
{
namespace
{
constexpr std::uint64_t kHomeTtlSeconds = 60 * 60;
constexpr char kService[] = "the Internet Archive";
// Curated collections only (see the header).
constexpr char kCollections[] = "(collection:(feature_films) OR collection:(prelinger) OR "
                                "(collection:(animationandcartoons) AND year:[1900 TO 1963]))";

struct Row
{
    const char *title;
    const char *query;
    bool portrait; // film posters rather than frames
};
constexpr Row kRows[] = {
    {"Feature Films", "collection:(feature_films) AND mediatype:(movies)", true},
    {"Classic Cartoons",
     "collection:(animationandcartoons) AND mediatype:(movies) AND year:[1900 TO 1963]", false},
    {"Science Fiction & Horror",
     "collection:(feature_films) AND mediatype:(movies) AND subject:(horror OR \"science "
     "fiction\" OR sci-fi)",
     true},
    {"Silent Era", "collection:(feature_films) AND mediatype:(movies) AND year:[1890 TO 1929]",
     true},
    {"Prelinger Archives", "collection:(prelinger) AND mediatype:(movies)", false},
};

// Archive fields are a string or a list of strings.
std::string first_text(const json::Value &v)
{
    if (v.is_array())
        return v.size() ? v[0].str() : std::string{};
    if (v.is_number())
        return std::to_string(v.integer());
    return v.str();
}

std::vector<std::string> texts(const json::Value &v)
{
    std::vector<std::string> out;
    if (v.is_array())
        for (const auto &e : v.items())
            out.push_back(e.str());
    else if (!first_text(v).empty())
        out.push_back(first_text(v));
    return out;
}

std::string lower(std::string s)
{
    for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "5400.5" or "01:30:00" -> seconds
double parse_length(const std::string &text)
{
    if (text.find(':') == std::string::npos)
        return std::atof(text.c_str());
    double total = 0.0;
    std::size_t at = 0;
    while (at <= text.size())
    {
        const std::size_t colon = text.find(':', at);
        total = total * 60.0 + std::atof(text.substr(at, colon - at).c_str());
        if (colon == std::string::npos)
            break;
        at = colon + 1;
    }
    return total;
}

std::string download_url(const std::string &base, const std::string &identifier,
                         const std::string &name)
{
    std::string out = base + "/download/" + url::encode_component(identifier);
    std::size_t at = 0;
    while (at <= name.size())
    {
        const std::size_t slash = name.find('/', at);
        out += "/" + url::encode_component(name.substr(at, slash - at));
        if (slash == std::string::npos)
            break;
        at = slash + 1;
    }
    return out;
}

bool valid_identifier(const std::string &id)
{
    return !id.empty() && id.size() < 200 &&
           std::all_of(id.begin(), id.end(),
                       [](char c) {
                           return std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
                                  c == '-' || c == '.';
                       });
}

// How well a file suits the PS5 pipeline: 0 = not usable.
int file_rank(const json::Value &file)
{
    const std::string name = lower(file["name"].str());
    const std::string format = lower(file["format"].str());
    const auto ends = [&](const char *ext)
    {
        const std::size_t n = std::char_traits<char>::length(ext);
        return name.size() > n && name.compare(name.size() - n, n, ext) == 0;
    };
    if (!(ends(".mp4") || ends(".m4v") || ends(".mkv") || ends(".mov")))
        return 0;
    if (format.find("512kb") != std::string::npos || format.find("mpeg2") != std::string::npos)
        return 0; // MPEG-4 Part 2 / MPEG-2 derivatives: not decodable here
    if (format.rfind("h.264", 0) == 0)
        return 3; // the archive's H.264 + AAC derivative
    if (format == "mpeg4")
        return 2; // an uploaded MP4, usually H.264
    return 1;
}
} // namespace

InternetArchive::InternetArchive(std::string base) : base_{std::move(base)}
{
}

ProviderInfo InternetArchive::info() const
{
    ProviderInfo info;
    info.id = "archive";
    info.name = "Internet Archive";
    info.tagline = "Public-domain feature films, classic cartoons and archive films.";
    info.attribution = "Content from the Internet Archive (archive.org). Feature Films are films "
                       "the archive believes to be in the public domain.";
    info.capabilities = {
        {"Browse & search", Support::available,
         "Curated collections: Feature Films, Animation & Cartoons up to 1963, Prelinger "
         "Archives."},
        {"Playback", Support::available,
         "MP4/MKV files with H.264 video stream from archive.org with seeking. Items that only "
         "have other formats cannot be played."},
    };
    return info;
}

std::string InternetArchive::search_query(const std::string &words)
{
    std::string clean;
    for (char c : words)
    {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || u >= 0x80 || c == ' ' || c == '\'' || c == '.' || c == ',')
            clean += c;
        else
            clean += ' ';
    }
    const auto begin = clean.find_first_not_of(' ');
    if (begin == std::string::npos)
        return {};
    clean = clean.substr(begin, clean.find_last_not_of(' ') - begin + 1);
    return "(title:(" + clean + ") OR subject:(" + clean + ") OR creator:(" + clean +
           ")) AND mediatype:(movies) AND " + kCollections;
}

std::vector<MediaItem> InternetArchive::parse_search(const std::string &body,
                                                     const std::string &base, std::string *error)
{
    std::vector<MediaItem> out;
    const auto parsed = json::parse(body);
    const json::Value &docs = parsed.value["response"]["docs"];
    if (!parsed.ok || !docs.is_array())
    {
        if (error)
            *error = "Unexpected answer from the Internet Archive";
        return out;
    }
    for (const auto &d : docs.items())
    {
        MediaItem m;
        m.provider = "archive";
        m.id = first_text(d["identifier"]);
        m.title = first_text(d["title"]);
        if (!valid_identifier(m.id) || m.title.empty())
            continue;
        m.kind = ItemKind::video;
        const std::string year = first_text(d["year"]);
        const std::string creator = first_text(d["creator"]);
        m.subtitle = year.empty() ? creator : (creator.empty() ? year : year + " - " + creator);
        if (m.subtitle.empty())
            m.subtitle = "Internet Archive";
        m.description = strip_html(first_text(d["description"]));
        m.image_url = base + "/services/img/" + m.id;
        m.external_url = base + "/details/" + m.id;
        m.external_label = "Open on phone";
        m.attribution = "Internet Archive";
        if (d["downloads"].integer() > 0)
            m.meta = format_count(d["downloads"].integer()) + " views on archive.org";
        m.accent = 0x8d6e63;
        out.push_back(std::move(m));
    }
    return out;
}

bool InternetArchive::parse_metadata(const std::string &body, const std::string &base,
                                     int max_height, MediaItem *item, std::string *error)
{
    const auto parsed = json::parse(body);
    const json::Value &md = parsed.value["metadata"];
    if (!parsed.ok || !md.is_object())
    {
        *error = "This item is not available on the Internet Archive";
        return false;
    }
    const std::string identifier =
        first_text(md["identifier"]).empty() ? item->id : first_text(md["identifier"]);
    if (!first_text(md["title"]).empty())
        item->title = first_text(md["title"]);
    const std::string description = strip_html(first_text(md["description"]));
    if (!description.empty())
        item->description = description;
    const std::string year = first_text(md["year"]).empty() ? first_text(md["date"]).substr(0, 4)
                                                            : first_text(md["year"]);
    item->genres.clear();
    for (const std::string &subject : texts(md["subject"]))
    {
        // "Comedy; Drama" in one string, or a list.
        std::size_t at = 0;
        while (at < subject.size() && item->genres.size() < 4)
        {
            std::size_t end = subject.find(';', at);
            if (end == std::string::npos)
                end = subject.size();
            std::string g = subject.substr(at, end - at);
            g.erase(0, g.find_first_not_of(' '));
            g.erase(g.find_last_not_of(' ') + 1);
            if (!g.empty() && g.size() < 30)
                item->genres.push_back(g);
            at = end + 1;
        }
    }
    const std::string licence = first_text(md["licenseurl"]);
    const std::vector<std::string> collections = texts(md["collection"]);
    const bool feature_films =
        std::find(collections.begin(), collections.end(), "feature_films") != collections.end();
    item->attribution = !licence.empty() ? "Licence: " + licence
                        : feature_films
                            ? "Internet Archive Feature Films (believed to be in the public domain)"
                            : "Internet Archive";

    const json::Value *best = nullptr;
    int best_rank = 0;
    long long best_height = -1;
    for (const auto &file : parsed.value["files"].items())
    {
        const int rank = file_rank(file);
        if (rank == 0)
            continue;
        const long long height = std::atoll(first_text(file["height"]).c_str());
        const bool fits = height <= 0 || height <= max_height;
        const bool best_fits = best_height <= 0 || best_height <= max_height;
        bool better = !best || rank > best_rank;
        if (best && rank == best_rank)
            better = (fits && !best_fits) ||
                     (fits == best_fits && (fits ? height > best_height : height < best_height));
        if (better)
        {
            best = &file;
            best_rank = rank;
            best_height = height;
        }
    }
    double length = 0.0;
    if (best)
        length = parse_length(first_text((*best)["length"]));
    if (length <= 0.0)
        length = parse_length(first_text(md["runtime"]));
    item->duration = length;
    std::string meta = year;
    if (length > 0.0)
        meta += (meta.empty() ? "" : " - ") + format_clock(length);
    if (!meta.empty())
        item->meta = meta;
    if (!best)
    {
        *error = "This item has no MP4 or MKV video the PS5 can play";
        return false;
    }
    item->playable = Playable{media::SourceKind::http_file,
                              download_url(base, identifier, (*best)["name"].str())};
    return true;
}

ItemsResult InternetArchive::query(const std::string &q, int rows, const net::CancelFlag &cancel)
{
    ItemsResult result;
    const std::string address = base_ + "/advancedsearch.php?" +
                                url::build_query({{"q", q},
                                                  {"fl[]", "identifier"},
                                                  {"fl[]", "title"},
                                                  {"fl[]", "year"},
                                                  {"fl[]", "creator"},
                                                  {"fl[]", "description"},
                                                  {"fl[]", "downloads"},
                                                  {"sort[]", "downloads desc"},
                                                  {"rows", std::to_string(rows)},
                                                  {"page", "1"},
                                                  {"output", "json"}});
    const Fetched f = fetch_text(address, cancel, kService);
    if (!f.ok)
    {
        result.error = f.error;
        return result;
    }
    std::string error;
    result.items = parse_search(f.body, base_, &error);
    result.ok = error.empty();
    result.error = error;
    return result;
}

ShelvesResult InternetArchive::home(const net::CancelFlag &cancel)
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
        ItemsResult items = query(row.query, 30, cancel);
        if (!items.ok)
        {
            if (first_error.empty())
                first_error = items.error;
            continue;
        }
        for (auto &m : items.items)
            m.portrait = row.portrait;
        if (!items.items.empty())
            result.shelves.push_back({row.title, std::move(items.items), row.portrait});
    }
    result.ok = !result.shelves.empty();
    if (!result.ok)
        result.error = first_error.empty() ? "The Internet Archive returned nothing" : first_error;
    else
    {
        std::lock_guard<std::mutex> guard(cache_lock_);
        home_cache_ = result;
        home_cached_at_ = now;
    }
    return result;
}

ItemsResult InternetArchive::search(const std::string &words, const net::CancelFlag &cancel)
{
    const std::string q = search_query(words);
    if (q.empty())
        return {true, {}, {}};
    return query(q, 40, cancel);
}

DetailsResult InternetArchive::details(const MediaItem &item, const net::CancelFlag &cancel)
{
    DetailsResult result;
    result.item = item;
    if (!valid_identifier(item.id))
    {
        result.error = "Unknown Internet Archive item";
        return result;
    }
    const Fetched f = fetch_text(base_ + "/metadata/" + item.id, cancel, kService, 16u << 20);
    if (!f.ok)
    {
        result.error = f.error;
        return result;
    }
    std::string error;
    result.ok = parse_metadata(f.body, base_, max_height_, &result.item, &error);
    result.error = error;
    return result;
}
} // namespace akeno
