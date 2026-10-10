// AKENO STREAM PS5 - Website adapter: a PeerTube instance through its documented REST API.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The homepage of a PeerTube instance (https://video.blender.org) becomes a
// provider when GET /api/v1/config answers with the instance's name and
// server version. Its catalogue is the instance's own (local) channels from
// GET /api/v1/video-channels; search is GET /api/v1/search/video-channels
// limited to the instance. A channel opens like a PeerTube channel provider:
// its public playlists are seasons and their videos episodes, played from
// the HLS or MP4 file the API names (anime/provider.cpp). Channels other
// instances federate are left out: they belong to those instances.
#include "anime/website.hpp"

#include "anime/catalog_format.hpp"
#include "core/json.hpp"
#include "core/url.hpp"

#include <algorithm>
#include <cctype>

namespace akeno::anime
{
namespace
{
constexpr int kPageSize = 50; // the API allows up to 100

bool plain_handle(const std::string &s)
{
    return !s.empty() && s.size() <= 120 &&
           std::all_of(s.begin(), s.end(),
                       [](char c) {
                           return std::isalnum(static_cast<unsigned char>(c)) || c == '-' ||
                                  c == '_' || c == '.';
                       });
}

std::string absolute(const std::string &host, const std::string &path)
{
    if (path.empty() || path.rfind("https://", 0) == 0)
        return path;
    if (path.rfind("http", 0) == 0)
        return {};
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

std::string host_of(const ProviderConfig &config)
{
    const auto it = config.settings.find("host");
    if (it != config.settings.end())
        return it->second;
    const auto u = url::parse(config.address);
    return u ? u->authority : std::string{};
}

// A PeerTube channel provider for one channel of the instance.
ProviderConfig channel_config(const ProviderConfig &config, const std::string &host,
                              const std::string &handle)
{
    ProviderConfig c;
    c.id = config.id;
    c.name = config.name;
    c.type = ProviderType::peertube;
    c.address = "https://" + host + "/c/" + handle;
    return c;
}

SiteListing channels(const ProviderConfig &config, const std::string &request,
                     const std::string &start_text, const net::CancelFlag &cancel)
{
    SiteListing out;
    const std::string host = host_of(config);
    const SitePage page = fetch_site_page(request, cancel, "application/json", 2u * 1024u * 1024u);
    if (!page.ok)
    {
        out.error = page.error;
        out.error_kind = page.error_kind;
        return out;
    }
    const auto parsed = json::parse(page.body);
    if (!parsed.ok || !parsed.value["data"].is_array())
    {
        out.error = host + " did not answer like a PeerTube instance.";
        out.error_kind = SiteError::unsupported;
        return out;
    }
    for (const json::Value &ch : parsed.value["data"].items())
    {
        const std::string &handle = ch["name"].str();
        const std::string &owner = ch["host"].str();
        const bool local = ch.has("isLocal") ? ch["isLocal"].boolean() : owner == host;
        if (!local || !plain_handle(handle) || ch["displayName"].str().empty())
            continue;
        Series s;
        s.id = slug(handle + "-" + host);
        s.title = ch["displayName"].str();
        s.description = ch["description"].str();
        s.poster = largest_image(ch["avatars"], host);
        s.banner = largest_image(ch["banners"], host);
        const std::string created = ch["createdAt"].str();
        if (created.size() >= 4)
            s.year = std::atoi(created.substr(0, 4).c_str());
        s.attribution = "PeerTube - " + host;
        s.source_ref = handle;
        s.details_pending = true;
        if (!s.id.empty())
            out.series.push_back(std::move(s));
    }
    const long long start = std::atoll(start_text.c_str());
    const long long total = parsed.value["total"].integer(-1);
    out.total = -1; // the total counts other instances' channels too
    if (total > start + kPageSize)
        out.next_page = std::to_string(start + kPageSize);
    out.ok = true;
    return out;
}

class PeerTubeInstanceAdapter final : public WebsiteAdapter
{
  public:
    [[nodiscard]] const char *id() const noexcept override
    {
        return "peertube-instance";
    }
    [[nodiscard]] const char *label() const noexcept override
    {
        return "PeerTube instance";
    }
    [[nodiscard]] int version() const noexcept override
    {
        return 1;
    }

    SiteDetection detect(const SitePage &page, const net::CancelFlag &cancel) const override
    {
        SiteDetection d;
        const auto u = url::parse(page.url);
        if (!u || !u->is_https())
        {
            d.why_not = "PeerTube instances are read over https only.";
            return d;
        }
        // The instance's own pages; a channel or playlist address is a
        // PeerTube channel provider instead (anime/provider.cpp).
        const std::string &path = u->path;
        const bool instance_page = path == "/" || path.rfind("/videos", 0) == 0 ||
                                   path.rfind("/home", 0) == 0 || path.rfind("/about", 0) == 0;
        const bool mentions = page.body.find("PeerTube") != std::string::npos ||
                              page.body.find("peertube") != std::string::npos;
        if (!instance_page && !mentions)
        {
            d.why_not = "Not a PeerTube instance page.";
            return d;
        }
        const SitePage config = fetch_site_page("https://" + u->authority + "/api/v1/config",
                                                cancel, "application/json", 512u * 1024u);
        if (!config.ok)
        {
            d.why_not = "No PeerTube API at this address.";
            return d;
        }
        const auto parsed = json::parse(config.body);
        const std::string name = parsed.ok ? parsed.value["instance"]["name"].str() : "";
        const std::string version = parsed.ok ? parsed.value["serverVersion"].str() : "";
        if (name.empty() || version.empty())
        {
            d.why_not = "/api/v1/config did not answer like a PeerTube instance.";
            return d;
        }
        d.recognised = true;
        d.site_name = name;
        d.homepage = "https://" + u->authority + "/";
        d.format = "PeerTube " + version + " instance (documented REST API)";
        d.settings["host"] = u->authority;
        d.evidence.push_back("/api/v1/config: PeerTube " + version + ", \"" + name + "\"");
        d.capabilities = capabilities({});
        return d;
    }

    [[nodiscard]] SiteCapabilities capabilities(const ProviderConfig &) const override
    {
        SiteCapabilities c;
        c.catalogue = c.search = c.pagination = c.series_details = c.seasons = c.episodes = true;
        return c;
    }

    SiteListing catalogue(const ProviderConfig &config, const std::string &page,
                          const ProviderContext &, const net::CancelFlag &cancel) const override
    {
        const std::string start = page.empty() ? "0" : page;
        return channels(config,
                        "https://" + host_of(config) + "/api/v1/video-channels?" +
                            url::build_query({{"start", start},
                                              {"count", std::to_string(kPageSize)},
                                              {"sort", "-createdAt"}}),
                        start, cancel);
    }

    SiteListing search(const ProviderConfig &config, const std::string &query,
                       const std::string &page, const ProviderContext &,
                       const net::CancelFlag &cancel) const override
    {
        const std::string start = page.empty() ? "0" : page;
        const std::string host = host_of(config);
        return channels(config,
                        "https://" + host + "/api/v1/search/video-channels?" +
                            url::build_query({{"search", query},
                                              {"start", start},
                                              {"count", std::to_string(kPageSize)},
                                              {"searchTarget", "local"},
                                              {"host", host}}),
                        start, cancel);
    }

    SiteSeries series(const ProviderConfig &config, const Series &entry,
                      const ProviderContext &context, const net::CancelFlag &cancel) const override
    {
        SiteSeries out;
        const std::string host = host_of(config);
        if (!plain_handle(entry.source_ref))
        {
            out.error = "Unknown channel.";
            out.error_kind = SiteError::unsupported;
            return out;
        }
        const auto provider =
            make_provider(channel_config(config, host, entry.source_ref), context);
        LoadedCatalog loaded = provider->load(cancel);
        if (!loaded.ok || loaded.series.empty())
        {
            out.error = loaded.error.empty() ? "The channel has no public videos." : loaded.error;
            out.error_kind = SiteError::unsupported;
            return out;
        }
        out.series = std::move(loaded.series.front());
        out.warnings = std::move(loaded.warnings);
        out.ok = true;
        return out;
    }

    Resolution resolve(const ProviderConfig &config, const Series &series, const Season &season,
                       const Episode &episode, const ProviderContext &context,
                       const net::CancelFlag &cancel) const override
    {
        const std::string handle = plain_handle(series.source_ref) ? series.source_ref : "channel";
        return make_provider(channel_config(config, host_of(config), handle), context)
            ->resolve(series, season, episode, cancel);
    }
};
} // namespace

const WebsiteAdapter &peertube_instance_adapter()
{
    static const PeerTubeInstanceAdapter adapter;
    return adapter;
}
} // namespace akeno::anime
