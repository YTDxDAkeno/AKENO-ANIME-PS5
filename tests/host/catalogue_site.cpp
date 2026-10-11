// AKENO STREAM PS5 - An offline catalogue-only website at a known site's address.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalogue_site.hpp"

#include "core/url.hpp"

#include <map>
#include <mutex>

namespace akeno::test
{
namespace
{
std::mutex g_lock;
std::vector<std::string> g_requests;

const std::map<std::string, std::string> &pages()
{
    static const std::map<std::string, std::string> list = {
        {"/", R"(<!doctype html><html><head><meta charset="utf-8"><title>Test Catalogue</title>
<script type="application/ld+json">
{"@context": "https://schema.org", "@graph": [
  {"@type": "WebSite", "name": "Test Catalogue", "url": "/"},
  {"@type": "ItemList", "name": "Made-up series", "itemListElement": [
    {"@type": "ListItem", "position": 1, "item": {"@type": "TVSeries", "name": "Fixture Series One",
      "url": "/info/fixture-one", "image": "/art/one.jpg", "description": "Invented for a test."}},
    {"@type": "ListItem", "position": 2, "item": {"@type": "TVSeries", "name": "Fixture Series Two",
      "url": "/info/fixture-two"}},
    {"@type": "ListItem", "position": 3, "item": {"@type": "TVSeries", "name": "Fixture Series Three",
      "url": "/info/fixture-three"}}
  ]}
]}
</script></head><body><div id="root"></div></body></html>)"},
        {"/info/fixture-one", R"(<!doctype html><html><head><meta charset="utf-8">
<script type="application/ld+json">
{"@context": "https://schema.org", "@type": "TVSeries", "name": "Fixture Series One",
 "url": "/info/fixture-one", "description": "Invented for a test.", "genre": ["Test"]}
</script></head><body><div id="root"></div></body></html>)"},
        {"/info/fixture-two", R"(<!doctype html><html><head><meta charset="utf-8">
<script type="application/ld+json">
{"@context": "https://schema.org", "@type": "TVSeries", "name": "Fixture Series Two",
 "url": "/info/fixture-two"}
</script></head><body><div id="root"></div></body></html>)"},
        {"/info/fixture-three", R"(<!doctype html><html><head><meta charset="utf-8">
<script type="application/ld+json">
{"@context": "https://schema.org", "@type": "TVSeries", "name": "Fixture Series Three",
 "url": "/info/fixture-three"}
</script></head><body><div id="root"></div></body></html>)"},
    };
    return list;
}
} // namespace

std::optional<net::Response> serve_catalogue_site(const net::Request &request)
{
    if (request.url.rfind(kCatalogueSite, 0) != 0)
        return std::nullopt;
    const auto u = url::parse(request.url);
    net::Response r;
    r.final_url = request.url;
    if (!u)
    {
        r.outcome = net::Outcome::invalid_url;
        return r;
    }
    {
        std::lock_guard<std::mutex> guard(g_lock);
        g_requests.push_back(u->path + (u->has_query ? "?" + u->query : std::string{}));
    }
    const auto page = pages().find(u->path.empty() ? "/" : u->path);
    if (page == pages().end())
    {
        r.outcome = net::Outcome::http_error;
        r.status = 404;
        return r;
    }
    r.outcome = net::Outcome::ok;
    r.status = 200;
    r.content_type = "text/html; charset=utf-8";
    r.body = page->second;
    return r;
}

std::vector<std::string> catalogue_site_requests()
{
    std::lock_guard<std::mutex> guard(g_lock);
    return g_requests;
}

void clear_catalogue_site_requests()
{
    std::lock_guard<std::mutex> guard(g_lock);
    g_requests.clear();
}
} // namespace akeno::test
