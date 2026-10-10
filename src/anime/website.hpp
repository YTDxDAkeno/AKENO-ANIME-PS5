// AKENO STREAM PS5 - Website providers: a website read by one of AKENO's adapters.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// "Add Streaming Provider" takes a website's address. A website becomes a
// provider only when an adapter AKENO STREAM implements recognises it from
// what the site itself publishes - an HTTP 200, a page title or a video
// player are not enough. Each adapter reads one documented, public way of
// describing a catalogue:
//
//   peertube-instance  a PeerTube instance's documented REST API: its local
//                      channels are series, their playlists seasons, their
//                      videos episodes (HLS or MP4, no DRM).
//   schema-org         schema.org structured data (JSON-LD) the site embeds
//                      for search engines: ItemList, TVSeries, TVSeason,
//                      TVEpisode, Movie and VideoObject, with a SearchAction
//                      for search and rel="next" links for more pages.
//
// What an adapter never does: invent addresses (an episode plays only from a
// contentUrl the site names), read hidden or obfuscated players, take
// cookies or sign-ins, or pass DRM. Structured data describes a catalogue;
// it does not prove that a video plays. Every episode's resource is checked
// (anime/probe.hpp) before it is offered as playable, and anything else -
// a player page, an embed, a protected stream - is labelled and stays in the
// browser (Websites).
//
// Requests are bounded: pages up to 4 MiB, at most 5 redirects (never from
// https to http), JSON-LD blocks up to 1 MiB and 32 per page, 24 page loads
// for one series. Pages of the catalogue are read from the provider's own
// site only (its host or a subdomain); media files may live elsewhere (CDNs).
#pragma once

#include "anime/provider.hpp"
#include "core/json.hpp"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace akeno::anime
{
// Why a request to a website failed, for the provider's screens.
enum class SiteError : std::uint8_t
{
    none,
    network,       // no answer, DNS, TLS, timeout
    not_found,     // HTTP 404 / 410
    auth_required, // HTTP 401 / 403, or a sign-in page
    rate_limited,  // HTTP 429
    server,        // HTTP 5xx
    unsupported,   // an answer the adapter cannot use (not HTML, no data)
    too_large,
};
const char *site_error_label(SiteError e) noexcept;

// One page of a website, fetched.
struct SitePage
{
    bool ok = false;
    long status = 0;
    std::string url; // after redirects
    std::string content_type;
    std::string body;
    SiteError error_kind = SiteError::none;
    std::string error; // one sentence when not ok
};
inline constexpr std::size_t kMaxSitePageBytes = 4u * 1024u * 1024u;
// GET with redirects (at most 5, never https to http), bounded in size.
SitePage fetch_site_page(const std::string &address, const net::CancelFlag &cancel,
                         const char *accept = "text/html,application/xhtml+xml;q=0.9,*/*;q=0.5",
                         std::size_t max_bytes = kMaxSitePageBytes);

// Pages of the catalogue are read from the provider's own site only: the same
// host, a leading "www." aside, or a subdomain of it.
bool same_site(std::string_view address, std::string_view site);

struct SiteCapabilities
{
    bool catalogue = false;       // lists the site's series and films
    bool search = false;          // searches on the site itself
    bool pagination = false;      // more catalogue pages
    bool series_details = false;  // descriptions and artwork per series
    bool seasons = false;         // seasons per series
    bool episodes = false;        // episode lists
    bool native_playback = false; // names video files AKENO may check and play
};
// One line per capability, for the Add Provider screen and Provider Home.
std::vector<std::string> capability_lines(const SiteCapabilities &c);

struct SiteDetection
{
    bool recognised = false;
    std::string adapter; // its id
    std::string format;  // "PeerTube 7.2 instance (documented REST API)"
    std::string site_name;
    std::string homepage; // where the address led, normalised
    std::map<std::string, std::string> settings;
    SiteCapabilities capabilities;
    std::vector<std::string> evidence; // what was found, one line each
    std::string why_not;               // when not recognised: the closest reason
};

// One page of a catalogue or of search results. Series are listed with what
// the page says about them; details_pending marks those whose seasons and
// episodes load when they are opened.
struct SiteListing
{
    bool ok = false;
    std::vector<Series> series;
    std::string next_page; // empty: the last page
    int total = -1;        // when the site says so
    SiteError error_kind = SiteError::none;
    std::string error;
};

struct SiteSeries
{
    bool ok = false;
    Series series; // with its seasons and episodes
    std::vector<CatalogIssue> warnings;
    SiteError error_kind = SiteError::none;
    std::string error;
};

class WebsiteAdapter
{
  public:
    virtual ~WebsiteAdapter() = default;
    [[nodiscard]] virtual const char *id() const noexcept = 0;
    [[nodiscard]] virtual const char *label() const noexcept = 0;
    [[nodiscard]] virtual int version() const noexcept = 0;
    // Recognises a site from its fetched page. Must find positive evidence
    // that the site publishes what this adapter reads.
    virtual SiteDetection detect(const SitePage &page, const net::CancelFlag &cancel) const = 0;
    [[nodiscard]] virtual SiteCapabilities capabilities(const ProviderConfig &config) const = 0;
    // page: empty for the first page, else a next_page from an earlier listing.
    virtual SiteListing catalogue(const ProviderConfig &config, const std::string &page,
                                  const ProviderContext &context,
                                  const net::CancelFlag &cancel) const = 0;
    virtual SiteListing search(const ProviderConfig &config, const std::string &query,
                               const std::string &page, const ProviderContext &context,
                               const net::CancelFlag &cancel) const = 0;
    // A catalogue entry's seasons and episodes.
    virtual SiteSeries series(const ProviderConfig &config, const Series &entry,
                              const ProviderContext &context,
                              const net::CancelFlag &cancel) const = 0;
    // The resource for one episode, or why there is none.
    virtual Resolution resolve(const ProviderConfig &config, const Series &series,
                               const Season &season, const Episode &episode,
                               const ProviderContext &context,
                               const net::CancelFlag &cancel) const = 0;
};

// Every adapter, in the order they are tried.
const std::vector<const WebsiteAdapter *> &website_adapters();
const WebsiteAdapter *find_website_adapter(std::string_view id);
// The settings format version this build reads for a website adapter; 0 for
// an adapter this build does not have (its providers are kept, not loaded).
int website_adapter_version(const std::string &adapter_id) noexcept;

// Tries every adapter on a fetched page; the first that recognises it wins.
SiteDetection detect_website(const SitePage &page, const net::CancelFlag &cancel);

// The episode and playback provider for a website provider (its adapter).
std::unique_ptr<SourceProvider> make_website_provider(const ProviderConfig &config,
                                                      const ProviderContext &context);
// Loads a catalogue entry's seasons and episodes (worker thread); the series
// keeps its provider id and name.
SiteSeries load_website_series(const ProviderConfig &config, const Series &entry,
                               const ProviderContext &context, const net::CancelFlag &cancel);
// One more catalogue page, or search results (worker thread).
SiteListing load_website_page(const ProviderConfig &config, const std::string &page,
                              const ProviderContext &context, const net::CancelFlag &cancel);
SiteListing search_website(const ProviderConfig &config, const std::string &query,
                           const ProviderContext &context, const net::CancelFlag &cancel);

// The adapters (anime/website_schema_org.cpp, anime/website_peertube.cpp).
const WebsiteAdapter &schema_org_adapter();
const WebsiteAdapter &peertube_instance_adapter();

// schema.org helpers (exposed for tests).
namespace schema
{
// Every JSON-LD block of an HTML page (<script type="application/ld+json">).
std::vector<json::Value> json_ld_blocks(std::string_view html);
// The page's <link rel="next" href=...>, resolved; empty when there is none.
std::string next_link(std::string_view html, const std::string &page_url);
// True when a node's @type includes this schema.org type ("TVSeries").
bool is_type(const json::Value &node, std::string_view type);
} // namespace schema
} // namespace akeno::anime
