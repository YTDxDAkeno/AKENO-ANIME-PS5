// AKENO STREAM PS5 - Website providers: a website read by one of AKENO's adapters.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "anime/website.hpp"

#include "anime/site_policy.hpp"
#include "core/url.hpp"

#include <algorithm>

namespace akeno::anime
{
namespace
{
std::string bare_host(std::string host)
{
    if (host.rfind("www.", 0) == 0)
        host.erase(0, 4);
    return host;
}

bool ends_with(const std::string &text, const std::string &tail)
{
    return text.size() > tail.size() &&
           text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
}

// A website provider loads, checks and plays through its adapter.
class WebsiteSourceProvider final : public SourceProvider
{
  public:
    WebsiteSourceProvider(ProviderConfig config, ProviderContext context)
        : config_{std::move(config)}, context_{std::move(context)},
          adapter_{find_website_adapter(config_.adapter)}
    {
    }

    [[nodiscard]] const ProviderConfig &config() const override
    {
        return config_;
    }

    LoadedCatalog load(const net::CancelFlag &cancel) override
    {
        LoadedCatalog out;
        if (!adapter_)
        {
            out.error = "The website adapter \"" + config_.adapter + "\" is not available.";
            return out;
        }
        SiteListing listing = load_website_page(config_, {}, context_, cancel);
        // A site whose first page lists only other sites' channels (PeerTube)
        // may have its own on the next pages.
        for (int extra = 0; listing.ok && listing.series.empty() && !listing.next_page.empty() &&
                            extra < 4 && !(cancel && cancel->load());
             ++extra)
        {
            SiteListing more = load_website_page(config_, listing.next_page, context_, cancel);
            listing.series = std::move(more.series);
            listing.next_page = more.ok ? more.next_page : std::string{};
        }
        if (!listing.ok)
        {
            out.error = listing.error.empty() ? "The site could not be read." : listing.error;
            return out;
        }
        out.series = std::move(listing.series);
        out.next_page = listing.next_page;
        out.total = listing.total;
        out.meta.name = config_.name;
        out.ok = true;
        if (out.series.empty())
        {
            out.ok = false;
            out.error = "The site lists no series or films right now.";
        }
        return out;
    }

    Resolution resolve(const Series &series, const Season &season, const Episode &episode,
                       const net::CancelFlag &cancel) override
    {
        if (!adapter_)
        {
            Resolution r;
            r.availability = Availability::provider_unsupported;
            r.detail = "The website adapter \"" + config_.adapter + "\" is not available.";
            return r;
        }
        return adapter_->resolve(config_, series, season, episode, context_, cancel);
    }

  private:
    ProviderConfig config_;
    ProviderContext context_;
    const WebsiteAdapter *adapter_;
};

// What the provider says about a resource decides before any byte is read:
// a sign-in, DRM or another region is never played.
void apply_restrictions(Series &series, const ProviderContext &context)
{
    for (Season &season : series.seasons)
        for (Episode &e : season.episodes)
        {
            if (!e.media)
                continue;
            const MediaResource &m = *e.media;
            if (!m.drm.empty())
            {
                e.availability = Availability::drm_unsupported;
                e.availability_detail = "The site marks this video as protected (" + m.drm + ").";
            }
            else if (m.requires_auth)
            {
                e.availability = Availability::auth_required;
                e.availability_detail = "The site marks this video as for subscribers or "
                                        "signed-in viewers only.";
            }
            else if (!m.regions.empty() && !context.region.empty() &&
                     std::find(m.regions.begin(), m.regions.end(), context.region) ==
                         m.regions.end())
            {
                e.availability = Availability::region_unavailable;
                e.availability_detail =
                    "Offered only in " + std::to_string(m.regions.size()) + " other region(s).";
            }
        }
}

void stamp(Series &series, const ProviderConfig &config)
{
    series.provider_id = config.id;
    series.provider_name = config.name;
}
} // namespace

const char *site_error_label(SiteError e) noexcept
{
    switch (e)
    {
    case SiteError::none:
        return "OK";
    case SiteError::network:
        return "Not reachable";
    case SiteError::not_found:
        return "Not found";
    case SiteError::auth_required:
        return "Sign-in required";
    case SiteError::rate_limited:
        return "Too many requests";
    case SiteError::server:
        return "Server error";
    case SiteError::unsupported:
        return "Not usable";
    case SiteError::too_large:
        return "Too large";
    }
    return "";
}

SitePage fetch_site_page(const std::string &address, const net::CancelFlag &cancel,
                         const char *accept, std::size_t max_bytes)
{
    SitePage out;
    const auto parsed = url::parse(address);
    if (!parsed || !parsed->is_http())
    {
        out.error_kind = SiteError::unsupported;
        out.error = "Not an http(s) address.";
        return out;
    }
    net::Client client;
    net::Request request;
    request.url = address;
    request.headers = {std::string{"Accept: "} + accept};
    request.cancel = cancel;
    request.max_bytes = max_bytes;
    request.total_timeout_ms = 25000;
    request.follow_redirects = true; // at most 5, never from https to http (net/http.cpp)
    const net::Response r = client.perform(request);
    out.status = r.status;
    out.url = r.final_url.empty() ? address : r.final_url;
    out.content_type = r.content_type;
    out.ok = r.ok();
    if (out.ok)
    {
        out.body = r.body;
        return out;
    }
    const std::string host = parsed->host;
    switch (r.outcome)
    {
    case net::Outcome::http_error:
        out.error_kind = r.status == 404 || r.status == 410   ? SiteError::not_found
                         : r.status == 401 || r.status == 403 ? SiteError::auth_required
                         : r.status == 429                    ? SiteError::rate_limited
                         : r.status >= 500                    ? SiteError::server
                                                              : SiteError::unsupported;
        out.error = host + " answered HTTP " + std::to_string(r.status) + ".";
        break;
    case net::Outcome::too_large:
        out.error_kind = SiteError::too_large;
        out.error = "The page from " + host + " is larger than AKENO STREAM reads.";
        break;
    case net::Outcome::cancelled:
        out.error_kind = SiteError::network;
        out.error = "Cancelled.";
        break;
    default:
        out.error_kind = SiteError::network;
        out.error = "Could not reach " + host + " (" + r.describe() + ").";
        break;
    }
    return out;
}

bool same_site(std::string_view address, std::string_view site)
{
    const auto a = url::parse(address);
    const auto b = url::parse(site);
    if (!a || !b)
        return false;
    // A port other than the scheme's default is part of the site (a test or
    // home server); http and https on their default ports are one site.
    const auto custom_port = [](const url::Url &u)
    { return u.port > 0 && u.port != (u.is_https() ? 443 : 80) ? u.port : 0; };
    if (custom_port(*a) != custom_port(*b))
        return false;
    const std::string ha = bare_host(a->host), hb = bare_host(b->host);
    return ha == hb || ends_with(ha, "." + hb) || ends_with(hb, "." + ha);
}

std::vector<std::string> capability_lines(const SiteCapabilities &c)
{
    std::vector<std::string> out;
    if (c.catalogue)
        out.emplace_back("Catalogue browsing");
    if (c.search)
        out.emplace_back("Search on the site");
    if (c.pagination)
        out.emplace_back("More pages");
    if (c.series_details)
        out.emplace_back("Series details");
    if (c.seasons)
        out.emplace_back("Seasons");
    if (c.episodes)
        out.emplace_back("Episode lists");
    if (c.native_playback)
        out.emplace_back("Native playback (checked)");
    return out;
}

const std::vector<const WebsiteAdapter *> &website_adapters()
{
    // A documented API first: it is the stronger evidence.
    static const std::vector<const WebsiteAdapter *> kAdapters = {&peertube_instance_adapter(),
                                                                  &schema_org_adapter()};
    return kAdapters;
}

const WebsiteAdapter *find_website_adapter(std::string_view id)
{
    for (const WebsiteAdapter *a : website_adapters())
        if (id == a->id())
            return a;
    return nullptr;
}

int website_adapter_version(const std::string &adapter_id) noexcept
{
    const WebsiteAdapter *a = find_website_adapter(adapter_id);
    return a ? a->version() : 0;
}

SiteDetection detect_website(const SitePage &page, const net::CancelFlag &cancel)
{
    SiteDetection closest;
    for (const WebsiteAdapter *a : website_adapters())
    {
        if (cancel && cancel->load())
            break;
        SiteDetection d = a->detect(page, cancel);
        if (d.recognised)
        {
            d.adapter = a->id();
            return d;
        }
        // The reason to show: from the adapter that found something, else the last one.
        if (closest.evidence.empty() || !d.evidence.empty())
            closest = std::move(d);
    }
    closest.recognised = false;
    if (closest.why_not.empty())
        closest.why_not = "No adapter recognises this website.";
    return closest;
}

std::unique_ptr<SourceProvider> make_website_provider(const ProviderConfig &config,
                                                      const ProviderContext &context)
{
    return std::make_unique<WebsiteSourceProvider>(config, context);
}

SiteSeries load_website_series(const ProviderConfig &config, const Series &entry,
                               const ProviderContext &context, const net::CancelFlag &cancel)
{
    SiteSeries out;
    if (const auto policy = unsupported_site(config.address))
    {
        out.error = policy->verdict;
        out.error_kind = SiteError::unsupported;
        return out;
    }
    const WebsiteAdapter *adapter = find_website_adapter(config.adapter);
    if (!adapter)
    {
        out.error = "The website adapter \"" + config.adapter + "\" is not available.";
        out.error_kind = SiteError::unsupported;
        return out;
    }
    out = adapter->series(config, entry, context, cancel);
    if (!out.ok)
        return out;
    // The entry's identity stays: watch history and the watchlist use it.
    out.series.id = entry.id;
    out.series.source_ref = entry.source_ref;
    if (out.series.title.empty())
        out.series.title = entry.title;
    if (out.series.poster.empty())
        out.series.poster = entry.poster;
    if (out.series.banner.empty())
        out.series.banner = entry.banner.empty() ? out.series.poster : entry.banner;
    if (out.series.description.empty())
        out.series.description = entry.description;
    if (!out.series.year)
        out.series.year = entry.year;
    out.series.details_pending = false;
    stamp(out.series, config);
    apply_restrictions(out.series, context);
    return out;
}

SiteListing load_website_page(const ProviderConfig &config, const std::string &page,
                              const ProviderContext &context, const net::CancelFlag &cancel)
{
    SiteListing out;
    if (const auto policy = unsupported_site(config.address))
    {
        out.error = policy->verdict;
        out.error_kind = SiteError::unsupported;
        return out;
    }
    const WebsiteAdapter *adapter = find_website_adapter(config.adapter);
    if (!adapter)
    {
        out.error = "The website adapter \"" + config.adapter + "\" is not available.";
        out.error_kind = SiteError::unsupported;
        return out;
    }
    out = adapter->catalogue(config, page, context, cancel);
    for (Series &s : out.series)
    {
        stamp(s, config);
        apply_restrictions(s, context);
    }
    return out;
}

SiteListing search_website(const ProviderConfig &config, const std::string &query,
                           const ProviderContext &context, const net::CancelFlag &cancel)
{
    SiteListing out;
    if (const auto policy = unsupported_site(config.address))
    {
        out.error = policy->verdict;
        out.error_kind = SiteError::unsupported;
        return out;
    }
    const WebsiteAdapter *adapter = find_website_adapter(config.adapter);
    if (!adapter)
    {
        out.error = "The website adapter \"" + config.adapter + "\" is not available.";
        out.error_kind = SiteError::unsupported;
        return out;
    }
    out = adapter->search(config, query, {}, context, cancel);
    for (Series &s : out.series)
    {
        stamp(s, config);
        apply_restrictions(s, context);
    }
    return out;
}
} // namespace akeno::anime
