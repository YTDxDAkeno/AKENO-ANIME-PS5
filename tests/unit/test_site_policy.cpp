// AKENO STREAM PS5 - Websites that cannot become providers: Anikoto.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Anikoto offers no documented or authorized API, its catalogue is reachable
// only by scraping pages and private endpoints it blocks for automated
// clients, and its episodes are third-party web players (docs/ANIKOTO.md).
// These tests check that AKENO STREAM says so precisely, never sends a
// request to any Anikoto domain - not while detecting, not for an entry
// written into the provider file by hand - and leaves every existing
// provider (catalogues, PeerTube, website providers) exactly as it was.
#include "anime/provider_store.hpp"
#include "anime/registry.hpp"
#include "anime/site_policy.hpp"
#include "anime/website.hpp"
#include "core/fs.hpp"
#include "net/http.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

using namespace akeno;
using namespace akeno::anime;

namespace
{
std::string fresh_dir(const char *name)
{
    char path[128];
    std::snprintf(path, sizeof(path), "/tmp/akeno-policy-%s-XXXXXX", name);
    return mkdtemp(path) ? path : "/tmp";
}

// Every request is recorded and answered "offline".
class RecordingTransport
{
  public:
    RecordingTransport()
    {
        net::set_test_transport(
            [this](const net::Request &request)
            {
                {
                    std::lock_guard<std::mutex> guard(lock_);
                    urls_.push_back(request.url);
                }
                net::Response r;
                r.outcome = net::Outcome::network_error;
                r.error = "offline";
                return r;
            });
    }
    ~RecordingTransport()
    {
        net::set_test_transport({});
    }
    [[nodiscard]] std::vector<std::string> urls()
    {
        std::lock_guard<std::mutex> guard(lock_);
        return urls_;
    }

  private:
    std::mutex lock_;
    std::vector<std::string> urls_;
};

ProviderConfig anikoto_website()
{
    ProviderConfig c;
    c.type = ProviderType::website;
    c.name = "Anikoto";
    c.address = "https://anikototv.to/";
    c.original_url = "https://anikototv.to/";
    c.adapter = "schema-org";
    c.settings["catalogue"] = "https://anikototv.to/home";
    return c;
}
} // namespace

TEST(SitePolicy, RecognisesAnikotoAndItsMirrorsOnly)
{
    for (const char *address :
         {"https://anikototv.to/", "anikototv.to", "anikototv.to/watch/one-piece-odmau/ep-1",
          "https://www.anikototv.to/home", "http://anikoto.tv",
          "https://anikoto.net/filter?keyword=x", "https://anikoto.com.im/", "https://anikoto.cz",
          "https://anikototv.se", "https://anikotoapi.site/series/1"})
    {
        const auto policy = unsupported_site(address);
        ASSERT_TRUE(policy.has_value()) << address;
        EXPECT_EQ(policy->site, "Anikoto");
        EXPECT_EQ(policy->document, "docs/ANIKOTO.md");
        EXPECT_EQ(policy->reasons.size(), 4u);
    }
    for (const char *address :
         {"https://video.blender.org/", "https://anikotofans.example/",
          "https://koto.example/anikoto", "https://example.com/?site=anikototv.to",
          "https://anime.fixture.test/", "anikoto", ""})
        EXPECT_FALSE(unsupported_site(address).has_value()) << address;
}

TEST(SitePolicy, DetectionExplainsWithoutContactingTheSite)
{
    RecordingTransport transport;
    ProviderConfig config;
    const ProviderReport r =
        check_source("https://anikototv.to/watch/one-piece-odmau", "", {}, {}, &config);
    EXPECT_TRUE(transport.urls().empty()) << transport.urls().front();
    EXPECT_FALSE(r.importable);
    EXPECT_EQ(r.source, SourceClass::web_page);
    EXPECT_EQ(r.format, "Anikoto (website)");
    EXPECT_NE(r.verdict.find("no documented or authorized API"), std::string::npos) << r.verdict;
    EXPECT_NE(r.verdict.find("Nothing was sent to the site"), std::string::npos) << r.verdict;
    EXPECT_EQ(r.playable, 0);
    EXPECT_TRUE(r.adapter.empty()); // no adapter claimed it
    ASSERT_EQ(r.warnings.size(), 5u);
    EXPECT_NE(r.warnings[1].message.find("Referer/Origin"), std::string::npos);
    EXPECT_TRUE(config.address.empty()); // nothing to add
}

TEST(SitePolicy, CannotBeAddedAndExistingProvidersStayAsTheyWere)
{
    RecordingTransport transport;
    const std::string dir = fresh_dir("add");
    {
        Registry r(dir, dir);
        r.load();
        std::string why;
        ProviderConfig catalogue;
        catalogue.name = "Open Catalogue";
        catalogue.address = "https://catalog.example/anime.json";
        ASSERT_TRUE(r.add(catalogue, &why)) << why;
        ProviderConfig tube;
        tube.name = "Open Tube";
        tube.type = ProviderType::peertube;
        tube.address = "https://tube.example/c/openanime";
        ASSERT_TRUE(r.add(tube, &why)) << why;
        ProviderConfig site;
        site.type = ProviderType::website;
        site.name = "Fixture";
        site.address = site.original_url = "https://anime.fixture.test/";
        site.adapter = "schema-org";
        ASSERT_TRUE(r.add(site, &why)) << why;
    }
    const std::string catalogs = fs::read_text(fs::join(dir, kProvidersFile)).value_or("");
    const std::string websites = fs::read_text(fs::join(dir, kWebsiteProvidersFile)).value_or("");

    Registry r(dir, dir);
    r.load();
    std::string why;
    EXPECT_FALSE(r.add(anikoto_website(), &why));
    EXPECT_NE(why.find("Anikoto cannot be added"), std::string::npos) << why;
    ProviderConfig as_catalogue;
    as_catalogue.name = "Anikoto catalogue";
    as_catalogue.address = "https://anikoto.net/catalog.json";
    EXPECT_FALSE(r.add(as_catalogue, &why));
    EXPECT_EQ(r.configs().size(), 4u); // sample, catalogue, PeerTube, fixture site
    EXPECT_EQ(fs::read_text(fs::join(dir, kProvidersFile)).value_or(""), catalogs);
    EXPECT_EQ(fs::read_text(fs::join(dir, kWebsiteProvidersFile)).value_or(""), websites);
    EXPECT_TRUE(transport.urls().empty());
}

TEST(SitePolicy, AnEntryWrittenByHandIsKeptButNeverContacted)
{
    RecordingTransport transport;
    const std::string dir = fresh_dir("hand");
    ASSERT_TRUE(fs::write_atomic(fs::join(dir, kWebsiteProvidersFile), R"({
      "format": "akeno-website-providers", "version": 1, "minReaderVersion": 1,
      "providers": [{"id": "web_anikoto", "name": "Anikoto", "url": "https://anikototv.to/",
                     "adapter": "schema-org", "settings": {"catalogue": "https://anikototv.to/home"}}]})"));
    const std::string before = fs::read_text(fs::join(dir, kWebsiteProvidersFile)).value_or("");
    Registry r(dir, dir);
    r.load();
    const ProviderConfig *c = r.config("web_anikoto");
    ASSERT_NE(c, nullptr); // kept: AKENO never deletes what the user saved
    const ProviderState state = Registry::load_provider(*c, {}, {}, 12);
    EXPECT_FALSE(state.loaded);
    EXPECT_NE(state.error.find("Anikoto cannot be added"), std::string::npos) << state.error;
    Series entry;
    entry.id = "one-piece";
    entry.source_ref = "https://anikototv.to/watch/one-piece-odmau";
    entry.details_pending = true;
    EXPECT_FALSE(load_website_series(*c, entry, {}, {}).ok);
    EXPECT_FALSE(load_website_page(*c, "https://anikototv.to/home?page=2", {}, {}).ok);
    EXPECT_FALSE(search_website(*c, "one piece", {}, {}).ok);
    EXPECT_TRUE(transport.urls().empty()) << transport.urls().front();
    // Loading wrote nothing.
    EXPECT_EQ(fs::read_text(fs::join(dir, kWebsiteProvidersFile)).value_or(""), before);
}
