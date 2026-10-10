// AKENO STREAM PS5 - Sources the user adds: playlists, feeds and addresses.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "providers/sources.hpp"

#include "core/json.hpp"
#include "core/url.hpp"

#include <algorithm>
#include <cctype>
#include <map>

namespace akeno
{
const char *const kSourcesNotice =
    "AKENO STREAM does not include, find or recommend any sources. You choose what to add and "
    "you are responsible for having the right to watch it. DRM-protected streams cannot be "
    "played.";

namespace
{
constexpr std::size_t kMaxSources = 200;
constexpr std::size_t kMaxListBytes = 16u << 20;
constexpr std::size_t kClassifyBytes = 8192;

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

std::string_view skip_bom(std::string_view s)
{
    if (s.size() >= 3 && s.substr(0, 3) == "\xEF\xBB\xBF")
        s.remove_prefix(3);
    return s;
}

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool starts_with_nocase(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && lower(s.substr(0, prefix.size())) == prefix;
}

// "https://a/b/c.m3u8?x=1" -> "m3u8"
std::string extension_of(const std::string &address)
{
    std::string_view path = address;
    path = path.substr(0, path.find_first_of("?#"));
    const std::size_t slash = path.rfind('/');
    const std::size_t dot = path.rfind('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash))
        return {};
    return lower(path.substr(dot + 1));
}

std::string last_segment(const std::string &address)
{
    std::string_view path = address;
    path = path.substr(0, path.find_first_of("?#"));
    while (!path.empty() && path.back() == '/')
        path.remove_suffix(1);
    const std::size_t slash = path.rfind('/');
    return url::decode_component(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

std::string host_of(const std::string &address)
{
    const auto parsed = url::parse(address);
    return parsed ? parsed->host : std::string{};
}

// Absolute http(s) address for reference, or empty (other schemes, bad text).
std::string resolve_http(const std::string &base, std::string_view reference)
{
    reference = trim(reference);
    if (reference.empty())
        return {};
    std::optional<std::string> resolved;
    if (starts_with_nocase(reference, "http://") || starts_with_nocase(reference, "https://"))
        resolved = std::string(reference);
    else if (reference.find("://") == std::string_view::npos && !base.empty())
        resolved = url::resolve(base, reference);
    if (!resolved)
        return {};
    const auto parsed = url::parse(*resolved);
    return parsed && parsed->is_http() ? *resolved : std::string{};
}

std::string scheme_of(std::string_view reference)
{
    const std::size_t colon = reference.find("://");
    if (colon == std::string_view::npos || colon == 0 || colon > 12)
        return {};
    return lower(reference.substr(0, colon));
}

// Collects entries into shelves by group, in order of first appearance.
struct Grouper
{
    std::string fallback;
    std::vector<Shelf> shelves;
    std::map<std::string, std::size_t> index;
    std::size_t entries = 0;

    void add(const std::string &group, MediaItem item)
    {
        std::string title = group.empty() ? fallback : group;
        auto it = index.find(title);
        if (it == index.end())
        {
            if (shelves.size() >= kMaxSourceGroups)
                title = "More";
            it = index.find(title);
            if (it == index.end())
            {
                it = index.emplace(title, shelves.size()).first;
                shelves.push_back({title, {}, false});
            }
        }
        shelves[it->second].items.push_back(std::move(item));
        ++entries;
    }
};

struct Skipped
{
    std::size_t bad = 0;
    std::size_t drm = 0;
    std::size_t dash = 0;
    std::map<std::string, std::size_t> schemes;
    bool truncated = false;

    [[nodiscard]] std::string describe() const
    {
        std::string out;
        const auto append = [&](const std::string &part)
        { out += (out.empty() ? "" : "; ") + part; };
        if (truncated)
            append("only the first " + std::to_string(kMaxSourceEntries) + " entries are shown");
        if (drm)
            append(std::to_string(drm) + " DRM-protected entr" + (drm == 1 ? "y" : "ies") +
                   " skipped");
        std::size_t other = 0;
        std::string names;
        for (const auto &[scheme, count] : schemes)
        {
            other += count;
            names += (names.empty() ? "" : ", ") + scheme;
        }
        if (other)
            append(std::to_string(other) + " entr" + (other == 1 ? "y uses" : "ies use") +
                   " unsupported protocols (" + names + ")");
        if (dash)
            append(std::to_string(dash) + " MPEG-DASH entr" + (dash == 1 ? "y" : "ies") +
                   " skipped (DASH is not supported)");
        if (bad)
            append(std::to_string(bad) + " invalid entr" + (bad == 1 ? "y" : "ies") + " skipped");
        return out;
    }
};

MediaItem source_item(const std::string &address, std::string title, std::string subtitle)
{
    MediaItem m;
    m.provider = "source";
    m.id = address;
    m.title = title.empty() ? last_segment(address) : std::move(title);
    if (m.title.empty())
        m.title = host_of(address);
    m.subtitle = std::move(subtitle);
    m.kind = ItemKind::video;
    m.playable = Playable{guess_source_kind(address), address};
    return m;
}

void make_folder(MediaItem &m)
{
    m.kind = ItemKind::folder;
    m.playable.reset();
    m.badge = "LIST";
}

// key="value" pairs of an #EXTINF line (values may contain commas).
std::map<std::string, std::string> extinf_attributes(std::string_view text)
{
    std::map<std::string, std::string> out;
    std::size_t i = 0;
    while (i < text.size())
    {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        const std::size_t key_start = i;
        while (i < text.size() && text[i] != '=' &&
               !std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        if (i >= text.size() || text[i] != '=')
        {
            while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
                ++i;
            continue;
        }
        const std::string key = lower(text.substr(key_start, i - key_start));
        ++i;
        std::string value;
        if (i < text.size() && text[i] == '"')
        {
            const std::size_t end = text.find('"', i + 1);
            value = std::string(
                text.substr(i + 1, (end == std::string_view::npos ? text.size() : end) - i - 1));
            i = end == std::string_view::npos ? text.size() : end + 1;
        }
        else
        {
            const std::size_t start = i;
            while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
                ++i;
            value = std::string(text.substr(start, i - start));
        }
        if (!key.empty())
            out[key] = value;
    }
    return out;
}
} // namespace

const char *format_name(SourceFormat format) noexcept
{
    switch (format)
    {
    case SourceFormat::channel_list:
        return "M3U list";
    case SourceFormat::hls:
        return "HLS stream";
    case SourceFormat::feed:
        return "JSON feed";
    case SourceFormat::media:
        return "Media address";
    case SourceFormat::unknown:
        break;
    }
    return "Unknown";
}

SourceFormat classify_source(std::string_view content_type, std::string_view first_bytes)
{
    const std::string_view text = trim(skip_bom(first_bytes.substr(0, kClassifyBytes)));
    const std::string type = lower(content_type);
    if (text.empty())
        return SourceFormat::unknown;
    if (text.front() == '{' || text.front() == '[')
        return SourceFormat::feed;
    if (starts_with_nocase(text, "#extm3u") || starts_with_nocase(text, "#extinf"))
    {
        for (const char *tag : {"#EXT-X-STREAM-INF", "#EXT-X-TARGETDURATION",
                                "#EXT-X-MEDIA-SEQUENCE", "#EXT-X-MAP", "#EXT-X-PLAYLIST-TYPE"})
            if (text.find(tag) != std::string_view::npos)
                return SourceFormat::hls;
        return SourceFormat::channel_list;
    }
    const bool binary =
        std::any_of(text.begin(), text.end(),
                    [](char c)
                    {
                        const auto u = static_cast<unsigned char>(c);
                        return u == 0 || (u < 0x20 && u != '\n' && u != '\r' && u != '\t');
                    });
    if (binary || static_cast<unsigned char>(text.front()) == 0x47 || type.starts_with("video/") ||
        type.starts_with("audio/"))
        return SourceFormat::media;
    if (starts_with_nocase(text, "http://") || starts_with_nocase(text, "https://"))
        return SourceFormat::channel_list; // a plain list of addresses
    return SourceFormat::unknown;
}

media::SourceKind guess_source_kind(const std::string &address)
{
    const std::string ext = extension_of(address);
    if (ext == "m3u8")
        return media::SourceKind::hls;
    if (ext == "ts" || ext == "m2ts" || ext == "mts")
        return media::SourceKind::http_ts;
    if (ext == "mp4" || ext == "m4v" || ext == "mkv" || ext == "mov")
        return media::SourceKind::http_file;
    return media::SourceKind::automatic;
}

SourceListing parse_channel_list(std::string_view text, const std::string &base_url,
                                 const std::string &name)
{
    SourceListing out;
    out.title = name;
    Grouper groups;
    groups.fallback = name.empty() ? "Entries" : name;
    Skipped skipped;

    struct Pending
    {
        double duration = 0.0;
        std::string title, group, logo, extgrp;
        bool drm = false;
    } pending;

    text = skip_bom(text);
    std::size_t at = 0;
    while (at < text.size())
    {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string_view line = trim(text.substr(at, end - at));
        at = end + 1;
        if (line.empty() || starts_with_nocase(line, "#extm3u"))
            continue;
        if (starts_with_nocase(line, "#extinf:"))
        {
            const bool drm = pending.drm; // #KODIPROP lines may come first
            pending = {};
            pending.drm = drm;
            std::string_view rest = line.substr(8);
            // The title follows the first comma outside quotes.
            bool quoted = false;
            std::size_t comma = std::string_view::npos;
            for (std::size_t i = 0; i < rest.size(); ++i)
            {
                if (rest[i] == '"')
                    quoted = !quoted;
                else if (rest[i] == ',' && !quoted)
                {
                    comma = i;
                    break;
                }
            }
            const std::string_view head = rest.substr(0, comma);
            if (comma != std::string_view::npos)
                pending.title = std::string(trim(rest.substr(comma + 1)));
            const std::size_t space = head.find_first_of(" \t");
            pending.duration = std::atof(std::string(head.substr(0, space)).c_str());
            if (space != std::string_view::npos)
            {
                const auto attributes = extinf_attributes(head.substr(space));
                if (auto it = attributes.find("group-title"); it != attributes.end())
                    pending.group = it->second;
                if (auto it = attributes.find("tvg-logo"); it != attributes.end())
                    pending.logo = it->second;
                if (pending.title.empty())
                    if (auto it = attributes.find("tvg-name"); it != attributes.end())
                        pending.title = it->second;
            }
            continue;
        }
        if (starts_with_nocase(line, "#extgrp:"))
        {
            pending.extgrp = std::string(trim(line.substr(8)));
            continue;
        }
        if (starts_with_nocase(line, "#kodiprop:") &&
            (line.find("license") != std::string_view::npos ||
             line.find("drm") != std::string_view::npos))
        {
            pending.drm = true; // a licence server: the stream is DRM-protected
            continue;
        }
        if (line.front() == '#')
            continue; // #EXTVLCOPT and other player options are not applied
        // An address line.
        const Pending entry = pending;
        pending = {};
        if (groups.entries >= kMaxSourceEntries)
        {
            skipped.truncated = true;
            break;
        }
        if (entry.drm)
        {
            ++skipped.drm;
            continue;
        }
        const std::string address = resolve_http(base_url, line);
        if (address.empty())
        {
            const std::string scheme = scheme_of(line);
            if (!scheme.empty() && scheme != "http" && scheme != "https")
                ++skipped.schemes[scheme];
            else
                ++skipped.bad;
            continue;
        }
        if (extension_of(address) == "mpd")
        {
            ++skipped.dash;
            continue;
        }
        const std::string group = !entry.group.empty() ? entry.group : entry.extgrp;
        MediaItem m = source_item(address, entry.title, group.empty() ? name : group);
        m.image_url = resolve_http(base_url, entry.logo);
        // -1 only means "unknown length" (live channels and many films alike).
        if (entry.duration > 0.0)
        {
            m.duration = entry.duration;
            m.badge = format_clock(entry.duration);
        }
        if (extension_of(address) == "m3u")
            make_folder(m);
        groups.add(group, std::move(m));
    }
    out.entries = groups.entries;
    out.shelves = std::move(groups.shelves);
    out.notice = skipped.describe();
    out.ok = out.entries > 0;
    if (!out.ok)
        out.error = out.notice.empty() ? "The list contains no entries"
                                       : "No playable entries: " + out.notice;
    return out;
}

SourceListing parse_feed(std::string_view text, const std::string &base_url,
                         const std::string &name)
{
    SourceListing out;
    const auto parsed = json::parse(skip_bom(text), {kMaxListBytes, 32});
    if (!parsed.ok)
    {
        out.error = "The feed is not valid JSON: " + parsed.error;
        return out;
    }
    const json::Value &root = parsed.value;
    const json::Value *list = &root;
    if (root.is_object())
    {
        out.title = root["title"].str();
        out.description = root["description"].str();
        for (const char *key : {"items", "streams", "entries"})
            if (root[key].is_array())
            {
                list = &root[key];
                break;
            }
    }
    if (out.title.empty())
        out.title = name;
    if (!list->is_array())
    {
        out.error = "The feed has no \"items\" list";
        return out;
    }
    Grouper groups;
    groups.fallback = out.title.empty() ? "Entries" : out.title;
    Skipped skipped;
    for (const json::Value &entry : list->items())
    {
        if (groups.entries >= kMaxSourceEntries)
        {
            skipped.truncated = true;
            break;
        }
        const std::string &reference = entry["url"].str();
        const std::string address = resolve_http(base_url, reference);
        if (address.empty())
        {
            const std::string scheme = scheme_of(reference);
            if (!scheme.empty() && scheme != "http" && scheme != "https")
                ++skipped.schemes[scheme];
            else
                ++skipped.bad;
            continue;
        }
        if (entry["drm"].boolean() || entry.has("license") || entry.has("licence"))
        {
            ++skipped.drm;
            continue;
        }
        if (extension_of(address) == "mpd")
        {
            ++skipped.dash;
            continue;
        }
        std::string group = entry["group"].str(entry["category"].str());
        MediaItem m = source_item(address, entry["title"].str(entry["name"].str()),
                                  entry["subtitle"].str(group.empty() ? out.title : group));
        m.description = entry["description"].str();
        for (const char *key : {"image", "logo", "thumbnail", "poster"})
            if (m.image_url.empty())
                m.image_url = resolve_http(base_url, entry[key].str());
        m.duration = std::max(0.0, entry["duration"].num());
        if (entry["live"].boolean())
            m.badge = "LIVE";
        else if (m.duration > 0.0)
            m.badge = format_clock(m.duration);
        const std::string type = lower(entry["type"].str());
        if (type == "playlist" || type == "feed" || type == "source" || type == "list")
            make_folder(m);
        else if (type == "hls")
            m.playable->kind = media::SourceKind::hls;
        else if (type == "ts")
            m.playable->kind = media::SourceKind::http_ts;
        else if (type == "file")
            m.playable->kind = media::SourceKind::http_file;
        else if (type == "auto")
            m.playable->kind = media::SourceKind::automatic;
        groups.add(group, std::move(m));
    }
    out.entries = groups.entries;
    out.shelves = std::move(groups.shelves);
    out.notice = skipped.describe();
    out.ok = out.entries > 0;
    if (!out.ok)
        out.error = out.notice.empty() ? "The feed contains no entries"
                                       : "No playable entries: " + out.notice;
    return out;
}

// ---------------------------------------------------------------------------
std::vector<SourceEntry> parse_source_list(std::string_view text, std::string *error)
{
    std::vector<SourceEntry> out;
    const auto parsed = json::parse(skip_bom(text));
    const json::Value &list = parsed.value.is_array() ? parsed.value : parsed.value["sources"];
    if (!parsed.ok || !list.is_array())
    {
        if (error)
            *error = parsed.ok ? "sources.json must contain a \"sources\" list"
                               : "sources.json: " + parsed.error;
        return out;
    }
    int skipped = 0;
    for (const auto &entry : list.items())
    {
        const std::string address = resolve_http({}, entry["url"].str());
        if (address.empty())
        {
            ++skipped;
            continue;
        }
        SourceEntry s;
        s.url = address;
        s.name = std::string(trim(entry["name"].str(entry["title"].str())));
        if (s.name.empty())
            s.name = host_of(address);
        out.push_back(std::move(s));
        if (out.size() >= kMaxSources)
            break;
    }
    if (skipped && error)
        *error = std::to_string(skipped) + " source" + (skipped == 1 ? "" : "s") +
                 " skipped (an http or https address is required)";
    return out;
}

std::vector<SourceEntry> parse_source_text(std::string_view text, std::string *error)
{
    std::vector<SourceEntry> out;
    int skipped = 0;
    text = skip_bom(text);
    std::size_t at = 0;
    while (at < text.size() && out.size() < kMaxSources)
    {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string_view line = trim(text.substr(at, end - at));
        at = end + 1;
        if (line.empty() || line.front() == '#')
            continue;
        const std::string lowered = lower(line);
        std::size_t start = lowered.find("https://");
        start = std::min(start, lowered.find("http://"));
        if (start == std::string::npos)
        {
            ++skipped;
            continue;
        }
        const std::string address = resolve_http({}, line.substr(start));
        if (address.empty())
        {
            ++skipped;
            continue;
        }
        std::string_view name = trim(line.substr(0, start));
        while (!name.empty() && (name.back() == '=' || name.back() == '|' || name.back() == ':' ||
                                 name.back() == '-' || name.back() == ','))
            name = trim(name.substr(0, name.size() - 1));
        SourceEntry s;
        s.url = address;
        s.name = name.empty() ? host_of(address) : std::string(name);
        out.push_back(std::move(s));
    }
    if (skipped && error)
        *error = std::to_string(skipped) + " line" + (skipped == 1 ? "" : "s") +
                 " in sources.txt skipped (an http or https address is required)";
    return out;
}

std::string dump_source_list(const std::vector<SourceEntry> &entries)
{
    json::Value root = json::Value::object();
    root.set("version", 1);
    json::Value list = json::Value::array();
    for (const auto &e : entries)
    {
        json::Value v = json::Value::object();
        v.set("name", e.name);
        v.set("url", e.url);
        list.push(v);
    }
    root.set("sources", list);
    return root.dump(true);
}

// ---------------------------------------------------------------------------
SourceProvider::SourceProvider(SourceEntry entry) : entry_{std::move(entry)}
{
}

ProviderInfo SourceProvider::info() const
{
    ProviderInfo info;
    info.id = "source";
    info.name = entry_.name;
    info.tagline = entry_.url.empty() ? std::string{} : url::redact(entry_.url);
    info.attribution = kSourcesNotice;
    info.capabilities = {
        {"Browse", Support::available,
         "Reads M3U lists, AKENO JSON feeds, HLS playlists and direct media addresses. Web pages "
         "are not searched for links."},
        {"Search", Support::available, "Filters the entries of this source by name."},
        {"Playback", Support::available,
         "HLS (MPEG-TS or fragmented MP4, AES-128), MPEG-TS, MP4 and MKV with H.264 or HEVC "
         "video. DRM-protected streams are not supported."},
    };
    return info;
}

SourceFormat SourceProvider::format() const
{
    std::lock_guard<std::mutex> guard(lock_);
    return format_;
}

ShelvesResult SourceProvider::home(const net::CancelFlag &cancel)
{
    ShelvesResult result;
    std::string body;
    SourceFormat format = SourceFormat::unknown;
    bool decided = false;
    net::Client client;
    net::Request request;
    request.url = entry_.url;
    request.max_bytes = kMaxListBytes;
    request.cancel = cancel;
    request.on_head = [](const net::Head &head) { return head.status >= 200 && head.status < 300; };
    request.on_data = [&](const std::uint8_t *data, std::size_t bytes)
    {
        body.append(reinterpret_cast<const char *>(data), bytes);
        if (!decided && body.size() >= 1024)
        {
            decided = true;
            format = classify_source({}, body);
            if (format == SourceFormat::media)
                return false; // a stream or file: no need to download it here
        }
        return body.size() <= kMaxListBytes;
    };
    const net::Response response = client.perform(request);
    if (cancel && cancel->load())
    {
        result.error = "cancelled";
        return result;
    }
    if (!decided || format != SourceFormat::media)
    {
        if (!response.ok())
        {
            result.error =
                "Could not load the source: " +
                (response.head.status >= 300 ? "HTTP " + std::to_string(response.head.status)
                                             : response.describe());
            return result;
        }
        format = classify_source(response.content_type, body);
    }

    SourceListing listing;
    switch (format)
    {
    case SourceFormat::channel_list:
        listing = parse_channel_list(
            body, response.final_url.empty() ? entry_.url : response.final_url, entry_.name);
        break;
    case SourceFormat::feed:
        listing = parse_feed(body, response.final_url.empty() ? entry_.url : response.final_url,
                             entry_.name);
        break;
    case SourceFormat::hls:
    case SourceFormat::media:
    {
        MediaItem m = source_item(entry_.url, entry_.name, format_name(format));
        m.playable->kind =
            format == SourceFormat::hls ? media::SourceKind::hls : media::SourceKind::automatic;
        m.description = "This source is a single stream. Select it to play.";
        listing.ok = true;
        listing.entries = 1;
        listing.shelves.push_back({entry_.name, {std::move(m)}, false});
        break;
    }
    case SourceFormat::unknown:
        listing.error = "This address is not a playlist, feed or stream (it may be a web page). "
                        "AKENO STREAM reads M3U lists, AKENO JSON feeds, HLS playlists and media "
                        "files; it does not search web pages for links.";
        break;
    }
    if (!listing.ok)
    {
        result.error = listing.error;
        return result;
    }
    std::vector<MediaItem> all;
    for (const auto &shelf : listing.shelves)
        all.insert(all.end(), shelf.items.begin(), shelf.items.end());
    {
        std::lock_guard<std::mutex> guard(lock_);
        all_ = std::move(all);
        format_ = format;
    }
    result.ok = true;
    result.shelves = std::move(listing.shelves);
    result.error = listing.notice; // shown as a note when ok
    return result;
}

ItemsResult SourceProvider::search(const std::string &query, const net::CancelFlag &cancel)
{
    (void)cancel;
    ItemsResult result;
    result.ok = true;
    const std::string needle = lower(trim(query));
    std::lock_guard<std::mutex> guard(lock_);
    for (const auto &item : all_)
    {
        if (needle.empty() || lower(item.title).find(needle) != std::string::npos ||
            lower(item.subtitle).find(needle) != std::string::npos)
            result.items.push_back(item);
        if (result.items.size() >= 200)
            break;
    }
    if (all_.empty())
        result.error = "Open the source first, then search it.";
    return result;
}
} // namespace akeno
