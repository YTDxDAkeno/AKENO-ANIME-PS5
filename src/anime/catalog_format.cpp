// AKENO STREAM PS5 - The AKENO streaming catalogue format (JSON), parsed and validated.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "anime/catalog_format.hpp"

#include "core/fs.hpp"
#include "core/json.hpp"
#include "core/url.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace akeno::anime
{
namespace
{
std::string lower(std::string text)
{
    for (char &c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string extension_of(const std::string &address)
{
    std::string path = address;
    if (const std::size_t q = path.find_first_of("?#"); q != std::string::npos)
        path.resize(q);
    const std::size_t slash = path.find_last_of('/');
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return {};
    return lower(path.substr(dot + 1));
}

bool is_local_base(const std::string &base)
{
    return !base.empty() && base.front() == '/';
}

// A short, printable string from JSON (titles, ids, descriptions).
std::string text_of(const json::Value &v, std::size_t limit)
{
    std::string out = v.str();
    out.erase(std::remove_if(out.begin(), out.end(), [](char c)
                             { return static_cast<unsigned char>(c) < 0x20 && c != '\n'; }),
              out.end());
    if (out.size() > limit)
    {
        out.resize(limit);
        // Do not cut a UTF-8 sequence in half.
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80)
            out.pop_back();
        if (!out.empty() && static_cast<unsigned char>(out.back()) >= 0xC0)
            out.pop_back();
    }
    return out;
}

bool valid_id(const std::string &id)
{
    return !id.empty() && id.size() <= 80 &&
           std::all_of(id.begin(), id.end(),
                       [](char c) {
                           return std::isalnum(static_cast<unsigned char>(c)) || c == '-' ||
                                  c == '_' || c == '.';
                       });
}

std::string id_of(const json::Value &v, const std::string &fallback)
{
    if (v.is_string())
        return v.str();
    if (v.is_number())
        return std::to_string(v.integer());
    return fallback;
}

std::vector<std::string> strings_of(const json::Value &v, std::size_t limit, std::size_t length)
{
    std::vector<std::string> out;
    for (const auto &item : v.items())
    {
        std::string s = text_of(item, length);
        if (!s.empty())
            out.push_back(std::move(s));
        if (out.size() >= limit)
            break;
    }
    return out;
}

class Parser
{
  public:
    Parser(const CatalogOptions &options, ParsedCatalog &out) : options_{options}, out_{out}
    {
    }

    void error(std::string path, std::string message)
    {
        out_.errors.push_back({std::move(path), std::move(message)});
    }
    void warning(std::string path, std::string message)
    {
        if (out_.warnings.size() < 200)
            out_.warnings.push_back({std::move(path), std::move(message)});
    }

    std::string artwork(const json::Value &v, const std::string &path)
    {
        if (!v.is_string() || v.str().empty())
            return {};
        std::string why;
        std::string resolved = resolve_reference(options_.base, v.str(), &why);
        if (resolved.empty())
            warning(path, "artwork ignored: " + why);
        return resolved;
    }

    std::optional<MediaResource> media(const json::Value &m, const std::string &path)
    {
        if (m.is_null())
            return std::nullopt;
        if (!m.is_object())
        {
            error(path, "must be an object");
            return std::nullopt;
        }
        MediaResource r;
        std::string why;
        if (!m["url"].is_string() || m["url"].str().empty())
        {
            error(path + ".url", "is required: the address of the video");
            return std::nullopt;
        }
        r.url = resolve_reference(options_.base, m["url"].str(), &why);
        if (r.url.empty())
        {
            error(path + ".url", why);
            return std::nullopt;
        }
        const std::string type = lower(m["type"].str());
        static const std::set<std::string> kTypes = {"",    "mp4",  "m4v", "mov",   "mkv",
                                                     "hls", "m3u8", "ts",  "mpegts"};
        if (!kTypes.count(type))
        {
            error(path + ".type", "\"" + type + "\" is not a supported type (mp4, hls, ts, mkv)");
            return std::nullopt;
        }
        r.container = type == "m3u8" ? "hls" : type == "mpegts" ? "ts" : type;
        r.kind = media_kind(r.url, r.container);
        if (r.url.rfind("http://", 0) == 0)
            warning(path + ".url", "uses plain http (not encrypted)");
        for (std::size_t i = 0; i < m["audio"].size() && i < 16; ++i)
        {
            const json::Value &a = m["audio"][i];
            r.audio.push_back({text_of(a["language"], 16), text_of(a["label"], 40)});
        }
        for (std::size_t i = 0; i < m["subtitles"].size() && i < 16; ++i)
        {
            const json::Value &s = m["subtitles"][i];
            const std::string sp = path + ".subtitles[" + std::to_string(i) + "]";
            std::string sw;
            SubtitleTrack t;
            t.language = text_of(s["language"], 16);
            t.label = text_of(s["label"], 40);
            t.url = resolve_reference(options_.base, s["url"].str(), &sw);
            t.format = lower(s["format"].str(extension_of(t.url)));
            if (t.url.empty())
            {
                warning(sp + ".url", "subtitle ignored: " + sw);
                continue;
            }
            if (t.format != "vtt" && t.format != "srt")
            {
                warning(sp, "subtitle ignored: only WebVTT (.vtt) and SubRip (.srt) are supported");
                continue;
            }
            if (t.label.empty())
                t.label = t.language.empty() ? "Subtitles" : t.language;
            r.subtitles.push_back(std::move(t));
        }
        if (m["drm"].is_string() && !m["drm"].str().empty())
            r.drm = text_of(m["drm"], 40);
        else if (m["drm"].is_bool() && m["drm"].boolean())
            r.drm = "yes";
        r.requires_auth = m["requiresAuth"].boolean();
        r.regions = strings_of(m["regions"], 250, 2);
        for (std::string &region : r.regions)
            for (char &c : region)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return r;
    }

    void availability(Episode &e)
    {
        if (!e.media)
        {
            e.availability = Availability::episodes_indexed;
            e.availability_detail = "The catalogue lists this episode without a video address.";
            return;
        }
        const MediaResource &m = *e.media;
        if (!m.drm.empty())
        {
            e.availability = Availability::drm_unsupported;
            e.availability_detail = "The provider protects this episode with DRM (" + m.drm +
                                    "). AKENO STREAM plays only DRM-free video.";
        }
        else if (m.requires_auth)
        {
            e.availability = Availability::auth_required;
            e.availability_detail =
                "The provider needs a sign-in for this episode; AKENO STREAM has none for it.";
        }
        else if (!m.regions.empty() && !options_.region.empty() &&
                 std::find(m.regions.begin(), m.regions.end(), options_.region) == m.regions.end())
        {
            e.availability = Availability::region_unavailable;
            e.availability_detail =
                "Offered only in " + std::to_string(m.regions.size()) + " other region(s).";
        }
        else if (m.kind == media::SourceKind::automatic && is_local(m.url))
        {
            e.availability = Availability::provider_unsupported;
            e.availability_detail = "AKENO STREAM cannot play this kind of file.";
        }
        else
        {
            e.availability = Availability::unverified;
            e.availability_detail = "Listed by the provider; checked when you open the series.";
        }
    }

    static bool is_local(const std::string &url)
    {
        return !url.empty() && url.front() == '/';
    }

    Episode episode(const json::Value &v, const std::string &path, int index)
    {
        Episode e;
        e.id = id_of(v["id"], v["number"].is_number() ? std::to_string(v["number"].integer())
                                                      : std::to_string(index + 1));
        if (!valid_id(e.id))
            error(path + ".id", "must be letters, digits, '-', '_' or '.' (at most 80)");
        e.number = static_cast<int>(v["number"].integer(index + 1));
        if (e.number < 0 || e.number > 100000)
        {
            error(path + ".number", "must be between 0 and 100000");
            e.number = 0;
        }
        e.special = v["special"].boolean();
        e.title = text_of(v["title"], 160);
        if (e.title.empty())
            warning(path + ".title", "missing: the episode is shown with its number only");
        e.description = text_of(v["description"], 2000);
        e.thumbnail = artwork(v["thumbnail"], path + ".thumbnail");
        e.duration = std::max(0.0, v["duration"].num());
        e.air_date = text_of(v["airDate"], 10);
        e.media = media(v["media"], path + ".media");
        availability(e);
        return e;
    }

    Season season(const json::Value &v, const std::string &path, int index, std::size_t &budget)
    {
        Season s;
        s.number = static_cast<int>(v["number"].integer(index + 1));
        s.id = id_of(v["id"], "s" + std::to_string(s.number));
        if (!valid_id(s.id))
            error(path + ".id", "must be letters, digits, '-', '_' or '.' (at most 80)");
        s.title = text_of(v["title"], 120);
        if (s.title.empty())
            s.title = s.number == 0 ? "Specials" : "Season " + std::to_string(s.number);
        s.year = static_cast<int>(v["year"].integer());
        s.anilist_id = static_cast<int>(v["anilistId"].integer());
        s.episode_count = static_cast<int>(v["episodeCount"].integer());
        const json::Value &episodes = v["episodes"];
        if (!episodes.is_array())
        {
            if (!episodes.is_null())
                error(path + ".episodes", "must be a list");
            return s;
        }
        std::set<std::string> ids;
        for (std::size_t i = 0; i < episodes.size(); ++i)
        {
            if (budget == 0)
            {
                warning(path + ".episodes", "too many episodes; the rest were skipped");
                break;
            }
            --budget;
            const std::string ep = path + ".episodes[" + std::to_string(i) + "]";
            if (!episodes[i].is_object())
            {
                error(ep, "must be an object");
                continue;
            }
            Episode e = episode(episodes[i], ep, static_cast<int>(i));
            if (!ids.insert(e.id).second)
            {
                error(ep + ".id", "\"" + e.id + "\" is used twice in this season");
                continue;
            }
            s.episodes.push_back(std::move(e));
        }
        // Episodes are shown in their order: specials last within a season.
        std::stable_sort(s.episodes.begin(), s.episodes.end(),
                         [](const Episode &a, const Episode &b)
                         {
                             if (a.special != b.special)
                                 return !a.special;
                             return a.number < b.number;
                         });
        if (s.episode_count < static_cast<int>(s.episodes.size()))
            s.episode_count = static_cast<int>(s.episodes.size());
        return s;
    }

    Series series(const json::Value &v, const std::string &path)
    {
        Series s;
        s.provider_id = out_.provider.id;
        s.provider_name = out_.provider.name;
        s.id = id_of(v["id"], slug(v["title"].str()));
        if (!valid_id(s.id))
            error(path + ".id", "must be letters, digits, '-', '_' or '.' (at most 80)");
        s.title = text_of(v["title"], 160);
        if (s.title.empty())
            error(path + ".title", "is required");
        s.alt_titles = strings_of(v["altTitles"], 12, 160);
        s.description = text_of(v["description"], 4000);
        s.poster = artwork(v["poster"], path + ".poster");
        s.banner = artwork(v["banner"], path + ".banner");
        s.genres = strings_of(v["genres"], 12, 40);
        s.year = static_cast<int>(v["year"].integer());
        s.status = lower(text_of(v["status"], 20));
        s.score = static_cast<int>(std::clamp(v["score"].integer(), 0LL, 100LL));
        s.anilist_id = static_cast<int>(v["anilistId"].integer());
        s.mal_id = static_cast<int>(v["malId"].integer());
        s.licence = text_of(v["licence"], 80);
        if (s.licence.empty())
            s.licence = out_.provider.licence;
        s.attribution = text_of(v["attribution"], 200);
        if (s.attribution.empty())
            s.attribution = out_.provider.attribution;
        s.updated = text_of(v["updated"], 10);
        const json::Value &seasons = v["seasons"];
        if (!seasons.is_array() || seasons.size() == 0)
        {
            error(path + ".seasons", "needs at least one season");
            return s;
        }
        std::set<std::string> ids;
        std::size_t budget = kMaxEpisodesPerSeries;
        for (std::size_t i = 0; i < seasons.size() && i < kMaxSeasonsPerSeries; ++i)
        {
            const std::string sp = path + ".seasons[" + std::to_string(i) + "]";
            if (!seasons[i].is_object())
            {
                error(sp, "must be an object");
                continue;
            }
            Season season_value = season(seasons[i], sp, static_cast<int>(i), budget);
            if (!ids.insert(season_value.id).second)
            {
                error(sp + ".id", "\"" + season_value.id + "\" is used twice in this series");
                continue;
            }
            s.seasons.push_back(std::move(season_value));
        }
        if (seasons.size() > kMaxSeasonsPerSeries)
            warning(path + ".seasons", "too many seasons; the rest were skipped");
        // Specials (season 0) after the numbered seasons.
        std::stable_sort(s.seasons.begin(), s.seasons.end(),
                         [](const Season &a, const Season &b)
                         {
                             if ((a.number == 0) != (b.number == 0))
                                 return b.number == 0;
                             return a.number < b.number;
                         });
        return s;
    }

  private:
    const CatalogOptions &options_;
    ParsedCatalog &out_;
};
} // namespace

bool looks_like_catalog(std::string_view text)
{
    const std::size_t start = text.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
    return start != std::string_view::npos && text[start] == '{' &&
           text.find("\"akenoCatalog\"") != std::string_view::npos;
}

std::string slug(std::string_view text)
{
    std::string out;
    for (const char c : normalize_title(text))
        out += c == ' ' ? '-' : c;
    if (out.size() > 60)
        out.resize(60);
    while (!out.empty() && out.back() == '-')
        out.pop_back();
    return out;
}

std::string resolve_reference(const std::string &base, const std::string &reference,
                              std::string *why)
{
    const auto fail = [why](const char *text)
    {
        if (why)
            *why = text;
        return std::string{};
    };
    if (reference.empty())
        return fail("the address is empty");
    if (reference.size() > 4096)
        return fail("the address is too long");
    const std::string lowered = lower(reference.substr(0, 12));
    if (lowered.rfind("http://", 0) == 0 || lowered.rfind("https://", 0) == 0)
    {
        const auto parsed = url::parse(reference);
        if (!parsed || parsed->host.empty())
            return fail("not a valid http(s) address");
        return parsed->str();
    }
    if (reference.find("://") != std::string::npos || lowered.rfind("javascript:", 0) == 0 ||
        lowered.rfind("data:", 0) == 0 || lowered.rfind("file:", 0) == 0)
        return fail("only http(s) addresses and files next to the catalogue are allowed");
    if (is_local_base(base))
    {
        // On console storage: next to the catalogue, or an absolute console path.
        const std::string path =
            reference.front() == '/' ? reference : fs::join(fs::parent(base), reference);
        if (!fs::safe_path(path) || path.find("/./") != std::string::npos)
            return fail("not a safe file path (no '..')");
        return path;
    }
    if (base.empty())
        return fail("a relative address needs the catalogue's own address");
    if (reference.front() == '/' && reference.size() > 1 && reference[1] == '/')
        return fail("protocol-relative addresses are not allowed");
    const auto resolved = url::resolve(base, reference);
    if (!resolved)
        return fail("could not be resolved against the catalogue's address");
    const auto parsed = url::parse(*resolved);
    if (!parsed || !parsed->is_http())
        return fail("a web catalogue may only name http(s) addresses");
    return *resolved;
}

media::SourceKind media_kind(const std::string &resolved, const std::string &declared_type)
{
    const bool local = !resolved.empty() && resolved.front() == '/';
    std::string type = declared_type;
    if (type.empty())
        type = extension_of(resolved);
    if (type == "m3u8" || type == "hls")
        return local ? media::SourceKind::automatic : media::SourceKind::hls;
    if (type == "ts" || type == "mpegts" || type == "m2ts" || type == "mts")
        return local ? media::SourceKind::local_file : media::SourceKind::http_ts;
    if (type == "mp4" || type == "m4v" || type == "mov" || type == "mkv")
        return local ? media::SourceKind::local_file : media::SourceKind::http_file;
    return media::SourceKind::automatic;
}

ParsedCatalog parse_catalog(std::string_view text, const CatalogOptions &options)
{
    ParsedCatalog out;
    Parser parser{options, out};
    const auto parsed = json::parse(text);
    if (!parsed.ok)
    {
        parser.error("", "not valid JSON (" + parsed.error + " at byte " +
                             std::to_string(parsed.offset) + ")");
        return out;
    }
    const json::Value &root = parsed.value;
    if (!root.is_object() || !root.has("akenoCatalog"))
    {
        parser.error("", "not an AKENO catalogue: the \"akenoCatalog\" version is missing");
        return out;
    }
    const long long version = root["akenoCatalog"].integer();
    if (version != kCatalogVersion)
    {
        parser.error("akenoCatalog", "version " + std::to_string(version) +
                                         " is not supported (this app reads version " +
                                         std::to_string(kCatalogVersion) + ")");
        return out;
    }
    const json::Value &p = root["provider"];
    out.provider.name = text_of(p["name"], 60);
    if (out.provider.name.empty())
        parser.error("provider.name", "is required");
    out.provider.id = !options.provider_id.empty() ? options.provider_id
                                                   : id_of(p["id"], slug(out.provider.name));
    if (!valid_id(out.provider.id))
        parser.error("provider.id", "must be letters, digits, '-', '_' or '.' (at most 80)");
    out.provider.description = text_of(p["description"], 500);
    out.provider.homepage = text_of(p["homepage"], 300);
    out.provider.licence = text_of(p["licence"], 80);
    out.provider.attribution = text_of(p["attribution"], 200);

    const json::Value &series = root["series"];
    if (!series.is_array())
    {
        parser.error("series", "must be a list of series");
        return out;
    }
    std::set<std::string> ids;
    for (std::size_t i = 0; i < series.size(); ++i)
    {
        if (i >= kMaxCatalogSeries)
        {
            parser.warning("series", "more than " + std::to_string(kMaxCatalogSeries) +
                                         " series; the rest were skipped");
            break;
        }
        const std::string path = "series[" + std::to_string(i) + "]";
        if (!series[i].is_object())
        {
            parser.error(path, "must be an object");
            continue;
        }
        Series s = parser.series(series[i], path);
        if (!ids.insert(s.id).second)
        {
            parser.error(path + ".id", "\"" + s.id + "\" is used twice in this catalogue");
            continue;
        }
        if (!s.title.empty() && !s.seasons.empty())
            out.series.push_back(std::move(s));
    }
    out.ok = out.errors.empty();
    return out;
}
} // namespace akeno::anime
