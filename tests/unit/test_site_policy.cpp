// AKENO STREAM PS5 - Websites investigated as providers: Anikoto, Miruro, AnimePahe.
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
//
// Miruro publishes its catalogue as schema.org data on its own pages, but its
// episodes come through a private, Cloudflare-protected interface from
// third-party hosts (docs/MIRURO.md). AKENO STREAM may add what the pages
// publish as a catalogue - series and details, never a Play button - says
// precisely why its episodes play only on the website, and never asks for
// anything but the pages a browser would open.
//
// AnimePahe's catalogue and episode lists sit behind DDoS-Guard's browser
// challenge and its videos are kwik.cx web players whose streams answer only
// that player's Referer (docs/ANIMEPAHE.md). It plays in the PS5 browser, so
// it is "Browser only": explained without a request to the site, offered in
// Websites, never added as a provider.
//
// The provider capability states: a check finds what an address offers; only
// an episode that played in AKENO's player on this device verifies one.
#include "anime/provider_store.hpp"
#include "anime/registry.hpp"
#include "anime/site_policy.hpp"
#include "anime/website.hpp"
#include "catalogue_site.hpp"
#include "core/fs.hpp"
#include "core/json.hpp"
#include "net/http.hpp"

#include <gtest/gtest.h>

#include <algorithm>
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

// ---------------------------------------------------------------------------
// Miruro: a catalogue of what its pages publish; its episodes stay on the site.
namespace
{
// Serves the made-up catalogue at https://www.miruro.to; every other address
// is recorded and answered "offline".
class CatalogueTransport
{
  public:
    CatalogueTransport()
    {
        test::clear_catalogue_site_requests();
        net::set_test_transport(
            [this](const net::Request &request)
            {
                {
                    std::lock_guard<std::mutex> guard(lock_);
                    urls_.push_back(request.url);
                }
                if (auto r = test::serve_catalogue_site(request))
                    return *r;
                net::Response r;
                r.outcome = net::Outcome::network_error;
                r.error = "offline";
                return r;
            });
    }
    ~CatalogueTransport()
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
} // namespace

TEST(KnownSite, RecognisesMiruroOnItsOwnDomainsOnly)
{
    for (const char *address :
         {"https://www.miruro.to/", "miruro.to", "https://miruro.tv/watch?id=1", "miruro.bz",
          "https://www.miruro.ru/info/1", "https://miruro.com", "https://status.miruro.com/"})
    {
        const auto site = known_site(address);
        ASSERT_TRUE(site.has_value()) << address;
        EXPECT_EQ(site->site, "Miruro");
        EXPECT_EQ(site->playback, PlaybackClass::browser_only);
        EXPECT_EQ(site->document, "docs/MIRURO.md");
        EXPECT_EQ(site->reasons.size(), 4u);
        EXPECT_NE(site->browser_hint.find("Autoplay"), std::string::npos);
        // Read by the structured-data adapter only: never asked for PeerTube's API.
        EXPECT_EQ(site->adapters, std::vector<std::string>{"schema-org"});
        // Known, not refused: what its pages publish may be read.
        EXPECT_FALSE(unsupported_site(address).has_value()) << address;
    }
    for (const char *address :
         {"https://miruro.cx/", "https://notmiruro.to/", "https://miruro.to.example/",
          "https://example.com/?site=miruro.to", "https://anime.fixture.test/", "miruro", ""})
        EXPECT_FALSE(known_site(address).has_value()) << address;
}

TEST(KnownSite, MiruroIsAddedAsAWebsiteOnlyCatalogueWithoutPlayButtons)
{
    CatalogueTransport transport;
    ProviderConfig config;
    const ProviderReport r = check_source("https://www.miruro.to/", "", {}, {}, &config);
    EXPECT_EQ(r.adapter, "schema-org") << r.verdict;
    EXPECT_EQ(r.series, 3);
    EXPECT_EQ(r.episodes, 0);
    EXPECT_EQ(r.playable, 0);
    EXPECT_TRUE(r.importable) << r.verdict; // as a catalogue
    EXPECT_EQ(r.playback, PlaybackClass::browser_only);
    EXPECT_EQ(r.known_site, "Miruro");
    EXPECT_NE(r.verdict.find("without Play buttons"), std::string::npos) << r.verdict;
    EXPECT_NE(r.verdict.find("only on its website"), std::string::npos) << r.verdict;
    bool cloudflare = false, autoplay = false;
    for (const CatalogIssue &w : r.warnings)
    {
        cloudflare = cloudflare || w.message.find("Cloudflare") != std::string::npos;
        autoplay = autoplay || w.message.find("Autoplay") != std::string::npos;
    }
    EXPECT_TRUE(cloudflare);
    EXPECT_TRUE(autoplay);
    // Only the pages a browser opens: the homepage and series pages - never an
    // API, a stream or another host.
    for (const std::string &u : transport.urls())
    {
        EXPECT_EQ(u.rfind("https://www.miruro.to/", 0), 0u) << u;
        EXPECT_EQ(u.find("/api/"), std::string::npos) << u;
        EXPECT_EQ(u.find("pipe"), std::string::npos) << u;
    }
    EXPECT_EQ(config.type, ProviderType::website);
    EXPECT_EQ(config.playback, PlaybackClass::browser_only);

    // Added: saved with what the check found, and loaded again so.
    const std::string dir = fresh_dir("miruro");
    {
        Registry registry(dir, dir);
        registry.load();
        std::string why, added;
        ASSERT_TRUE(registry.add(config, &why, nullptr, &added)) << why;
        EXPECT_EQ(registry.playback_of(added), PlaybackClass::browser_only);
    }
    const auto saved =
        json::parse(fs::read_text(fs::join(dir, kWebsiteProvidersFile)).value_or(""));
    ASSERT_TRUE(saved.ok);
    EXPECT_EQ(saved.value["providers"][0]["catalogue"]["playback"].str(), "browser-only");
    Registry registry(dir, dir);
    registry.load();
    const ProviderConfig *c = registry.config(config.id);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->playback, PlaybackClass::browser_only);
    ProviderState state = Registry::load_provider(*c, {}, {}, 12);
    ASSERT_TRUE(state.loaded) << state.error;
    registry.install(std::move(state));
    // A series opened: no seasons, no episodes - nothing to play, nothing invented.
    const Series *one = registry.find_series(c->id, registry.state(c->id)->series[0].id);
    ASSERT_NE(one, nullptr);
    SiteSeries full = load_website_series(*c, *one, {}, {});
    ASSERT_TRUE(full.ok) << full.error;
    EXPECT_EQ(full.series.source_ref, "https://www.miruro.to/info/fixture-one");
    EXPECT_EQ(episode_total(full.series), 0);
    registry.install_series(c->id, std::move(full.series));
    EXPECT_TRUE(registry.available().empty() ||
                std::none_of(registry.available().begin(), registry.available().end(),
                             [&](const Series *s) { return s->provider_id == c->id; }));
    EXPECT_EQ(registry.playback_of(c->id), PlaybackClass::browser_only);
}

TEST(PlaybackClass, ComesFromWhatWasCheckedOnly)
{
    ProviderReport r;
    r.source = SourceClass::native_provider;
    r.series = 2;
    EXPECT_EQ(classify_playback(r), PlaybackClass::catalogue_only); // no video named
    r.with_media = 4;
    EXPECT_EQ(classify_playback(r), PlaybackClass::unknown); // named, not checked yet
    r.probed = 2;
    r.playable = 2;
    EXPECT_EQ(classify_playback(r), PlaybackClass::native);
    r.drm = 1;
    EXPECT_EQ(classify_playback(r), PlaybackClass::partial);
    r.playable = 0;
    r.probed = 0;
    EXPECT_EQ(classify_playback(r), PlaybackClass::protected_media); // DRM
    r.drm = 0;
    r.auth = 1;
    EXPECT_EQ(classify_playback(r), PlaybackClass::auth_required); // a sign-in, no DRM
    r.auth = 0;
    r.region = 1;
    EXPECT_EQ(classify_playback(r), PlaybackClass::unsupported); // another region only
    r.region = 0;
    r.probed = r.network = 2;
    EXPECT_EQ(classify_playback(r), PlaybackClass::unavailable); // no answer just now
    r.network = 0;
    r.missing = 2;
    EXPECT_EQ(classify_playback(r), PlaybackClass::unsupported);
    ProviderReport page;
    page.source = SourceClass::web_page; // HTTP 200 is not playback
    EXPECT_EQ(classify_playback(page), PlaybackClass::unsupported);
    ProviderReport silent;
    silent.source = SourceClass::unreachable;
    EXPECT_EQ(classify_playback(silent), PlaybackClass::unavailable);
    // A check never verifies: only an episode playing in AKENO's player does.
    EXPECT_NE(classify_playback(r), PlaybackClass::verified);
    EXPECT_FALSE(plays_natively(PlaybackClass::catalogue_only));
    EXPECT_FALSE(plays_natively(PlaybackClass::browser_only));
    EXPECT_FALSE(plays_natively(PlaybackClass::protected_media));
    EXPECT_FALSE(plays_natively(PlaybackClass::auth_required));
    EXPECT_FALSE(plays_natively(PlaybackClass::unavailable));
    EXPECT_FALSE(plays_natively(PlaybackClass::unsupported));
    EXPECT_TRUE(plays_natively(PlaybackClass::partial));
    EXPECT_TRUE(plays_natively(PlaybackClass::verified));
    // The required states, each with its own words.
    EXPECT_STREQ(playback_class_label(PlaybackClass::verified), "Native playback verified");
    EXPECT_STREQ(playback_class_label(PlaybackClass::native),
                 "Native playback available - not hardware-tested");
    EXPECT_STREQ(playback_class_label(PlaybackClass::catalogue_only), "Catalogue only");
    EXPECT_STREQ(playback_class_label(PlaybackClass::browser_only), "Browser only (PS5 browser)");
    EXPECT_STREQ(playback_class_label(PlaybackClass::auth_required), "Authentication required");
    EXPECT_STREQ(playback_class_label(PlaybackClass::protected_media), "DRM protected");
    EXPECT_STREQ(playback_class_label(PlaybackClass::unsupported), "Unsupported");
    EXPECT_STREQ(playback_class_label(PlaybackClass::unavailable), "Temporarily unavailable");
    // rc.1 saved "protected" for DRM: the id is unchanged.
    EXPECT_STREQ(playback_class_id(PlaybackClass::protected_media), "protected");
    for (const PlaybackClass c :
         {PlaybackClass::unknown, PlaybackClass::native, PlaybackClass::partial,
          PlaybackClass::catalogue_only, PlaybackClass::browser_only,
          PlaybackClass::protected_media, PlaybackClass::unsupported, PlaybackClass::verified,
          PlaybackClass::auth_required, PlaybackClass::unavailable})
    {
        EXPECT_EQ(playback_class_from_id(playback_class_id(c)), c);
        EXPECT_NE(std::string{playback_class_label(c)}, "");
        EXPECT_NE(std::string{playback_class_detail(c)}, "");
    }
    EXPECT_FALSE(playback_class_from_id("plays-everything").has_value());
}

TEST(SitePolicy, AnimePaheIsBrowserOnlyAndNeverContacted)
{
    for (const char *address : {"https://animepahe.pw/anime", "animepahe.pw",
                                "https://animepahe.com/", "https://www.animepahe.org/anime/1a2b",
                                "http://animepahe.ru", "https://animepahe.si/play/x/y"})
    {
        const auto policy = unsupported_site(address);
        ASSERT_TRUE(policy.has_value()) << address;
        EXPECT_EQ(policy->site, "AnimePahe");
        EXPECT_EQ(policy->document, "docs/ANIMEPAHE.md");
        EXPECT_EQ(policy->playback, PlaybackClass::browser_only);
        EXPECT_FALSE(policy->browser_hint.empty());
    }
    // Look-alike domains are other sites, and the player host is not AnimePahe.
    for (const char *address : {"https://animepahe.ch/", "https://animepahe.pw.example/",
                                "https://kwik.cx/e/abc", "https://notanimepahe.pw/"})
        EXPECT_FALSE(unsupported_site(address).has_value()) << address;

    RecordingTransport transport;
    ProviderConfig config;
    const ProviderReport r = check_source("https://animepahe.pw/anime", "", {}, {}, &config);
    EXPECT_TRUE(transport.urls().empty()) << transport.urls().front();
    EXPECT_FALSE(r.importable);
    EXPECT_EQ(r.playback, PlaybackClass::browser_only);
    EXPECT_EQ(r.known_site, "AnimePahe");
    EXPECT_NE(r.verdict.find("DDoS-Guard"), std::string::npos) << r.verdict;
    EXPECT_NE(r.verdict.find("Nothing was sent to the site"), std::string::npos) << r.verdict;
    ASSERT_EQ(r.warnings.size(), 5u);
    EXPECT_NE(r.warnings[1].message.find("Referer"), std::string::npos);
    EXPECT_TRUE(config.address.empty()); // nothing to add

    // Adding it anyway is refused, and nothing is written.
    const std::string dir = fresh_dir("animepahe");
    Registry registry(dir, dir);
    registry.load();
    ProviderConfig hand = anikoto_website();
    hand.name = "AnimePahe";
    hand.address = hand.original_url = "https://animepahe.pw/";
    ProviderState state = Registry::load_provider(hand, {}, {}, 12);
    EXPECT_FALSE(state.loaded);
    EXPECT_FALSE(state.unreachable); // a verdict, not a network failure
    EXPECT_TRUE(transport.urls().empty());
}

TEST(PlaybackClass, OnlyAnEpisodePlayedHereVerifiesAProvider)
{
    const std::string dir = fresh_dir("verified");
    ProviderConfig web;
    web.type = ProviderType::website;
    web.name = "Open Fixture";
    web.address = web.original_url = "https://anime.fixture.test/";
    web.adapter = "schema-org";
    web.playback = PlaybackClass::native; // what the last check found
    std::string added;
    {
        Registry registry(dir, dir);
        registry.load();
        std::string why;
        ASSERT_TRUE(registry.add(web, &why, nullptr, &added)) << why;
        EXPECT_EQ(registry.playback_of(added), PlaybackClass::native);
        EXPECT_EQ(registry.native_playback(added), nullptr);
        // A record without a time, or for a provider that does not exist, is ignored.
        registry.record_native_playback(added, {});
        registry.record_native_playback("web_nobody", NativePlayback{1, "x", "y", "", "", 1, 1});
        EXPECT_EQ(registry.native_playback(added), nullptr);
        EXPECT_FALSE(fs::exists(fs::join(dir, kNativePlaybackFile)));

        NativePlayback n;
        n.at = 1791734400;
        n.episode = "The Signal - S1 E1 - Lights";
        n.decoder = "PS5 hardware decoder (Videodec2)";
        n.video = "h264 640x360";
        n.audio = "aac 48000 Hz 2 ch";
        n.frames = 96;
        n.audio_frames = 90;
        registry.record_native_playback(added, n);
        EXPECT_EQ(registry.playback_of(added), PlaybackClass::verified);
        // "verified" is derived, never saved as what a check found.
        registry.set_playback(added, PlaybackClass::verified);
        EXPECT_EQ(registry.config(added)->playback, PlaybackClass::native);
    }
    const auto saved =
        json::parse(fs::read_text(fs::join(dir, kWebsiteProvidersFile)).value_or(""));
    ASSERT_TRUE(saved.ok);
    EXPECT_EQ(saved.value["providers"][0]["catalogue"]["playback"].str(), "native");

    // After a restart: still verified, with what was measured.
    Registry registry(dir, dir);
    registry.load();
    ASSERT_NE(registry.native_playback(added), nullptr);
    EXPECT_EQ(registry.native_playback(added)->decoder, "PS5 hardware decoder (Videodec2)");
    EXPECT_EQ(registry.native_playback(added)->frames, 96u);
    EXPECT_EQ(registry.playback_of(added), PlaybackClass::verified);
    // A later check that finds DRM wins over an old record.
    registry.set_playback(added, PlaybackClass::protected_media);
    EXPECT_EQ(registry.playback_of(added), PlaybackClass::protected_media);
    // Removing the provider removes its record.
    ASSERT_TRUE(registry.remove(added));
    EXPECT_EQ(registry.native_playback(added), nullptr);
    const auto records =
        json::parse(fs::read_text(fs::join(dir, kNativePlaybackFile)).value_or(""));
    ASSERT_TRUE(records.ok);
    EXPECT_FALSE(records.value["providers"].has(added));
}

TEST(PlaybackClass, AProviderThatDidNotAnswerIsTemporarilyUnavailable)
{
    RecordingTransport offline;
    const std::string dir = fresh_dir("unavailable");
    ProviderConfig web;
    web.type = ProviderType::website;
    web.name = "Open Fixture";
    web.address = web.original_url = "https://anime.fixture.test/";
    web.adapter = "schema-org";
    web.playback = PlaybackClass::native;
    Registry registry(dir, dir);
    registry.load();
    std::string why, added;
    ASSERT_TRUE(registry.add(web, &why, nullptr, &added)) << why;
    ProviderState state = Registry::load_provider(*registry.config(added), {}, {}, 12);
    EXPECT_FALSE(state.loaded);
    EXPECT_TRUE(state.unreachable) << state.error;
    registry.install(std::move(state));
    EXPECT_EQ(registry.playback_of(added), PlaybackClass::unavailable);
    EXPECT_FALSE(plays_natively(registry.playback_of(added))); // no Play buttons meanwhile
    // What the last full check found is kept for when it answers again.
    EXPECT_EQ(registry.config(added)->playback, PlaybackClass::native);
}

TEST(PlaybackClass, AValueFromANewerVersionIsKept)
{
    const std::string dir = fresh_dir("newer-class");
    ASSERT_TRUE(fs::write_atomic(fs::join(dir, kWebsiteProvidersFile), R"({
      "format": "akeno-website-providers", "version": 1, "minReaderVersion": 1,
      "providers": [{"id": "web_fixture", "name": "Fixture", "url": "https://anime.fixture.test/",
                     "adapter": "schema-org",
                     "catalogue": {"series": 4, "episodes": 9, "checked": 5,
                                   "playback": "plays-on-the-moon", "note": "kept"}}]})"));
    Registry r(dir, dir);
    r.load();
    const ProviderConfig *c = r.config("web_fixture");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->playback, PlaybackClass::unknown); // not understood: "not checked yet"
    // Saving for another reason keeps the value this version does not know.
    ASSERT_TRUE(r.set_enabled("web_fixture", false));
    const auto saved =
        json::parse(fs::read_text(fs::join(dir, kWebsiteProvidersFile)).value_or(""));
    ASSERT_TRUE(saved.ok);
    const json::Value &summary = saved.value["providers"][0]["catalogue"];
    EXPECT_EQ(summary["playback"].str(), "plays-on-the-moon");
    EXPECT_EQ(summary["note"].str(), "kept");
    EXPECT_EQ(summary["series"].integer(), 4);
    // A check that found something replaces it.
    r.set_playback("web_fixture", PlaybackClass::catalogue_only);
    const auto again =
        json::parse(fs::read_text(fs::join(dir, kWebsiteProvidersFile)).value_or(""));
    EXPECT_EQ(again.value["providers"][0]["catalogue"]["playback"].str(), "catalogue-only");
}
