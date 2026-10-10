// AKENO STREAM PS5 - Catalogue, episode and playback providers for anime.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "anime/provider.hpp"

#include "core/fs.hpp"
#include "core/json.hpp"
#include "core/url.hpp"
#include "providers/peertube.hpp"
#include "providers/provider.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace akeno::anime
{
namespace
{
constexpr std::size_t kMaxCatalogBytes = 8u * 1024u * 1024u;

// ---------------------------------------------------------------------------
// An AKENO catalogue: everything is in one document.
class CatalogueProvider final : public SourceProvider
{
  public:
    CatalogueProvider(ProviderConfig config, ProviderContext context)
        : config_{std::move(config)}, context_{std::move(context)}
    {
    }

    [[nodiscard]] const ProviderConfig &config() const override
    {
        return config_;
    }

    LoadedCatalog load(const net::CancelFlag &cancel) override
    {
        LoadedCatalog out;
        std::string text;
        if (!config_.address.empty() && config_.address.front() == '/')
        {
            const auto read = fs::read_text(config_.address, kMaxCatalogBytes);
            if (!read)
            {
                out.error = "The catalogue file " + fs::file_name(config_.address) +
                            " is not on the console.";
                return out;
            }
            text = *read;
        }
        else
        {
            const Fetched f = fetch_text(config_.address, cancel, config_.name, kMaxCatalogBytes);
            if (!f.ok)
            {
                out.error = f.error;
                return out;
            }
            text = f.body;
        }
        CatalogOptions options;
        options.provider_id = config_.id;
        options.base = config_.address;
        options.region = context_.region;
        ParsedCatalog parsed = parse_catalog(text, options);
        out.meta = parsed.provider;
        out.errors = std::move(parsed.errors);
        out.warnings = std::move(parsed.warnings);
        out.series = std::move(parsed.series);
        for (Series &s : out.series)
        {
            s.provider_id = config_.id;
            s.provider_name = config_.name.empty() ? parsed.provider.name : config_.name;
        }
        out.ok = !out.series.empty() || out.errors.empty();
        if (out.series.empty() && !out.errors.empty())
            out.error = out.errors.front().path.empty()
                            ? out.errors.front().message
                            : out.errors.front().path + ": " + out.errors.front().message;
        return out;
    }

    Resolution resolve(const Series &series, const Season &season, const Episode &episode,
                       const net::CancelFlag &cancel) override
    {
        (void)series;
        (void)season;
        (void)cancel;
        Resolution r;
        r.availability = episode.availability;
        r.detail = episode.availability_detail;
        if (!episode.media)
        {
            r.availability = Availability::episodes_indexed;
            r.detail = "The catalogue lists no video for this episode.";
            return r;
        }
        r.media = *episode.media;
        r.ok = episode.availability == Availability::unverified ||
               episode.availability == Availability::playable;
        return r;
    }

  private:
    ProviderConfig config_;
    ProviderContext context_;
};

// ---------------------------------------------------------------------------
// PeerTube: a channel's public playlists are its seasons (or one playlist is
// one season). The documented REST API lists them; each video's HLS or MP4
// file is looked up when it plays, exactly as Discover does.
std::string absolute(const std::string &host, const std::string &path)
{
    if (path.empty() || path.rfind("http", 0) == 0)
        return path;
    return "https://" + host + (path.front() == '/' ? "" : "/") + path;
}

std::string largest_image(const json::Value &list, const std::string &host)
{
    const json::Value *best = nullptr;
    for (const auto &i : list.items())
        if (!best || i["width"].integer() > (*best)["width"].integer())
            best = &i;
    if (!best)
        return {};
    const std::string address = (*best)["fileUrl"].str();
    return address.empty() ? absolute(host, (*best)["path"].str()) : address;
}

bool plain_segment(const std::string &s)
{
    return !s.empty() && s.size() <= 120 &&
           std::all_of(s.begin(), s.end(),
                       [](char c)
                       {
                           return std::isalnum(static_cast<unsigned char>(c)) || c == '-' ||
                                  c == '_' || c == '.' || c == '@';
                       });
}

class PeerTubeSeriesProvider final : public SourceProvider
{
  public:
    PeerTubeSeriesProvider(ProviderConfig config, ProviderContext context)
        : config_{std::move(config)}, context_{std::move(context)},
          address_{parse_peertube_address(config_.address)}
    {
    }

    [[nodiscard]] const ProviderConfig &config() const override
    {
        return config_;
    }

    LoadedCatalog load(const net::CancelFlag &cancel) override
    {
        LoadedCatalog out;
        if (!address_.ok)
        {
            out.error = "Not a PeerTube channel or playlist address.";
            return out;
        }
        const std::string api = "https://" + address_.host + "/api/v1/";
        Series series;
        series.provider_id = config_.id;
        series.provider_name = config_.name;
        series.anilist_id = config_.anilist_id;
        series.attribution = "PeerTube - " + address_.host;
        if (!address_.channel.empty())
        {
            const Fetched channel =
                fetch_text(api + "video-channels/" + address_.channel, cancel, address_.host);
            if (!channel.ok)
            {
                out.error = channel.status == 404 ? "The channel does not exist on " + address_.host
                                                  : channel.error;
                return out;
            }
            const auto c = json::parse(channel.body);
            if (!c.ok || !c.value.is_object() || c.value["displayName"].str().empty())
            {
                out.error = address_.host + " did not answer like a PeerTube instance.";
                return out;
            }
            series.id = slug(address_.channel + "-" + address_.host);
            series.title = c.value["displayName"].str();
            series.description = c.value["description"].str();
            series.poster = largest_image(c.value["avatars"], address_.host);
            series.banner = largest_image(c.value["banners"], address_.host);
            out.meta.name = series.title;
            const Fetched lists = fetch_text(
                api + "video-channels/" + address_.channel + "/video-playlists?" +
                    url::build_query({{"start", "0"}, {"count", "50"}, {"sort", "createdAt"}}),
                cancel, address_.host);
            if (lists.ok)
            {
                const auto l = json::parse(lists.body);
                int number = 0;
                for (const auto &p : l.value["data"].items())
                {
                    if (p["privacy"]["id"].integer(1) != 1 || p["type"]["id"].integer(1) != 1 ||
                        p["videosLength"].integer() <= 0)
                        continue;
                    if (cancel && cancel->load())
                        break;
                    Season s = playlist_season(p, ++number, cancel, &out);
                    if (!s.episodes.empty())
                        series.seasons.push_back(std::move(s));
                    if (series.seasons.size() >= kMaxSeasonsPerSeries)
                        break;
                }
            }
            if (series.seasons.empty())
            {
                // No public playlists: the channel's videos, oldest first.
                const Fetched videos =
                    fetch_text(api + "video-channels/" + address_.channel + "/videos?" +
                                   url::build_query({{"start", "0"},
                                                     {"count", "100"},
                                                     {"sort", "publishedAt"},
                                                     {"nsfw", "false"}}),
                               cancel, address_.host);
                if (!videos.ok)
                {
                    out.error = videos.error;
                    return out;
                }
                Season s;
                s.id = "videos";
                s.number = 1;
                s.title = "Videos";
                int n = 0;
                const auto parsed = json::parse(videos.body);
                for (const auto &v : parsed.value["data"].items())
                    if (auto e = episode_from(v, ++n))
                        s.episodes.push_back(std::move(*e));
                s.episode_count = static_cast<int>(s.episodes.size());
                if (!s.episodes.empty())
                    series.seasons.push_back(std::move(s));
                out.warnings.push_back(
                    {"", "The channel has no public playlists: its videos are listed as one "
                         "season in publishing order."});
            }
        }
        else
        {
            const Fetched list =
                fetch_text(api + "video-playlists/" + address_.playlist, cancel, address_.host);
            if (!list.ok)
            {
                out.error = list.status == 404 ? "The playlist does not exist on " + address_.host
                                               : list.error;
                return out;
            }
            const auto p = json::parse(list.body);
            if (!p.ok || p.value["displayName"].str().empty())
            {
                out.error = address_.host + " did not answer like a PeerTube instance.";
                return out;
            }
            series.id = slug(p.value["uuid"].str(address_.playlist));
            series.title = p.value["displayName"].str();
            series.description = p.value["description"].str();
            series.poster = absolute(address_.host, p.value["thumbnailPath"].str());
            out.meta.name = series.title;
            Season s = playlist_season(p.value, 1, cancel, &out);
            s.title = "Season 1";
            if (!s.episodes.empty())
                series.seasons.push_back(std::move(s));
        }
        if (series.seasons.empty())
        {
            out.error = "No public videos were found.";
            return out;
        }
        if (series.poster.empty() && !series.seasons.front().episodes.empty())
            series.poster = series.seasons.front().episodes.front().thumbnail;
        if (series.banner.empty())
            series.banner = series.poster;
        out.series.push_back(std::move(series));
        out.ok = true;
        return out;
    }

    Resolution resolve(const Series &series, const Season &season, const Episode &episode,
                       const net::CancelFlag &cancel) override
    {
        (void)series;
        (void)season;
        Resolution r;
        std::string uuid, host;
        if (!PeerTube::split_id(episode.source_ref, &uuid, &host))
        {
            r.availability = Availability::provider_unsupported;
            r.detail = "Unknown PeerTube video.";
            return r;
        }
        const Fetched f = fetch_text("https://" + host + "/api/v1/videos/" + uuid, cancel, host);
        if (!f.ok)
        {
            r.availability = f.status == 404                      ? Availability::missing
                             : f.status == 401 || f.status == 403 ? Availability::auth_required
                                                                  : Availability::network_error;
            r.detail = f.status == 404 ? "The video was removed from " + host + "." : f.error;
            return r;
        }
        MediaItem item;
        item.provider = "peertube";
        item.id = episode.source_ref;
        std::string error;
        if (!PeerTube::parse_video(f.body, context_.max_height, &item, &error) || !item.playable)
        {
            r.availability = Availability::missing;
            r.detail = error;
            return r;
        }
        r.media.url = item.playable->url;
        r.media.kind = item.playable->kind;
        r.media.container = item.playable->kind == media::SourceKind::hls ? "hls" : "mp4";
        // Captions published with the video (WebVTT).
        const Fetched captions =
            fetch_text("https://" + host + "/api/v1/videos/" + uuid + "/captions", cancel, host);
        const auto caption_list = json::parse(captions.ok ? captions.body : std::string{});
        if (captions.ok)
            for (const auto &c : caption_list.value["data"].items())
            {
                SubtitleTrack t;
                t.language = c["language"]["id"].str();
                t.label = c["language"]["label"].str(t.language);
                t.url = c["fileUrl"].str(absolute(host, c["captionPath"].str()));
                t.format = "vtt";
                if (!t.url.empty() && url::parse(t.url) && r.media.subtitles.size() < 16)
                    r.media.subtitles.push_back(std::move(t));
            }
        r.ok = true;
        r.availability = Availability::unverified;
        r.detail = "DRM-free video from " + host + ".";
        return r;
    }

  private:
    std::optional<Episode> episode_from(const json::Value &v, int number) const
    {
        if (!v.is_object() || v["nsfw"].boolean() || v["isLive"].boolean())
            return std::nullopt;
        const std::string uuid = v["uuid"].str();
        std::string host = v["account"]["host"].str(v["channel"]["host"].str(address_.host));
        if (uuid.empty() || !plain_segment(uuid) || !plain_segment(host))
            return std::nullopt;
        Episode e;
        e.id = uuid;
        e.number = number;
        e.title = v["name"].str();
        e.description = v["truncatedDescription"].str(v["description"].str());
        e.thumbnail = absolute(host, v["thumbnailPath"].str());
        e.duration = v["duration"].num();
        const std::string date = v["publishedAt"].str(v["originallyPublishedAt"].str());
        e.air_date = date.size() >= 10 ? date.substr(0, 10) : std::string{};
        e.source_ref = uuid + "@" + host;
        e.availability = Availability::unverified;
        e.availability_detail = "Looked up on " + host + " when it plays (HLS or MP4, no DRM).";
        return e;
    }

    Season playlist_season(const json::Value &p, int number, const net::CancelFlag &cancel,
                           LoadedCatalog *out) const
    {
        Season s;
        const std::string id = p["uuid"].str(p["shortUUID"].str(std::to_string(p["id"].integer())));
        s.id = slug(id).empty() ? "p" + std::to_string(number) : slug(id);
        s.number = number;
        s.title = p["displayName"].str("Season " + std::to_string(number));
        const std::string created = p["createdAt"].str();
        if (created.size() >= 4)
            s.year = std::atoi(created.substr(0, 4).c_str());
        const Fetched f =
            fetch_text("https://" + address_.host + "/api/v1/video-playlists/" + id + "/videos?" +
                           url::build_query({{"start", "0"}, {"count", "100"}}),
                       cancel, address_.host);
        if (!f.ok)
        {
            out->warnings.push_back({s.title, "could not be listed: " + f.error});
            return s;
        }
        std::vector<std::pair<long long, Episode>> elements;
        const auto parsed = json::parse(f.body);
        for (const auto &element : parsed.value["data"].items())
        {
            const long long position = element["position"].integer();
            if (auto e = episode_from(element["video"], static_cast<int>(position)))
                elements.emplace_back(position, std::move(*e));
        }
        std::stable_sort(elements.begin(), elements.end(),
                         [](const auto &a, const auto &b) { return a.first < b.first; });
        std::set<std::string> seen;
        int n = 0;
        for (auto &[position, e] : elements)
        {
            (void)position;
            if (!seen.insert(e.id).second)
                continue; // a video listed twice in one playlist
            e.number = ++n;
            s.episodes.push_back(std::move(e));
        }
        s.episode_count = static_cast<int>(p["videosLength"].integer(n));
        return s;
    }

    ProviderConfig config_;
    ProviderContext context_;
    PeerTubeAddress address_;
};
} // namespace

const char *provider_type_id(ProviderType t) noexcept
{
    switch (t)
    {
    case ProviderType::bundled:
        return "bundled";
    case ProviderType::catalog:
        return "catalog";
    case ProviderType::peertube:
        return "peertube";
    }
    return "catalog";
}

const char *provider_type_label(ProviderType t) noexcept
{
    switch (t)
    {
    case ProviderType::bundled:
        return "Bundled with AKENO STREAM";
    case ProviderType::catalog:
        return "AKENO catalogue (JSON)";
    case ProviderType::peertube:
        return "PeerTube channel or playlist";
    }
    return "";
}

ProviderType provider_type_from_id(std::string_view id) noexcept
{
    if (id == "bundled")
        return ProviderType::bundled;
    if (id == "peertube")
        return ProviderType::peertube;
    return ProviderType::catalog;
}

const char *source_class_label(SourceClass c) noexcept
{
    switch (c)
    {
    case SourceClass::unreachable:
        return "Not reachable";
    case SourceClass::web_page:
        return "Website (browser only)";
    case SourceClass::media_file:
        return "A single video";
    case SourceClass::playlist:
        return "Playlist without episode data";
    case SourceClass::metadata:
        return "Metadata only, no playable episodes";
    case SourceClass::documented_api:
        return "Documented API";
    case SourceClass::native_provider:
        return "Native streaming provider";
    case SourceClass::protected_source:
        return "Protected (DRM or sign-in)";
    }
    return "";
}

PeerTubeAddress parse_peertube_address(const std::string &address)
{
    PeerTubeAddress out;
    const auto parsed = url::parse(address);
    if (!parsed || !parsed->is_https() || parsed->host.empty())
        return out;
    std::vector<std::string> parts;
    std::string current;
    for (const char c : parsed->path)
    {
        if (c == '/')
        {
            if (!current.empty())
                parts.push_back(url::decode_component(current));
            current.clear();
        }
        else
            current += c;
    }
    if (!current.empty())
        parts.push_back(url::decode_component(current));
    const auto take = [&](std::size_t i) { return i < parts.size() ? parts[i] : std::string{}; };
    if ((take(0) == "c" || take(0) == "video-channels") && plain_segment(take(1)))
        out.channel = take(1);
    else if (take(0) == "w" && take(1) == "p" && plain_segment(take(2)))
        out.playlist = take(2);
    else if (take(0) == "videos" && take(1) == "watch" && take(2) == "playlist" &&
             plain_segment(take(3)))
        out.playlist = take(3);
    else if (take(0) == "video-playlists" && plain_segment(take(1)))
        out.playlist = take(1);
    out.host = parsed->authority;
    out.ok = !out.channel.empty() || !out.playlist.empty();
    return out;
}

std::unique_ptr<SourceProvider> make_provider(const ProviderConfig &config,
                                              const ProviderContext &context)
{
    if (config.type == ProviderType::peertube)
        return std::make_unique<PeerTubeSeriesProvider>(config, context);
    return std::make_unique<CatalogueProvider>(config, context);
}
} // namespace akeno::anime
