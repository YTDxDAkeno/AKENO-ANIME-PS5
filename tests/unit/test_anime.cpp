// AKENO STREAM PS5 - Anime episodes: catalogue format, providers, availability, library.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The episode system without the interface: the AKENO catalogue format and
// its validation, PeerTube channels as series, the media probe that refuses
// to call an HTTP 200 a video, the provider registry, search, the watchlist,
// progress per provider-qualified episode and subtitles.
#include "anime/catalog_format.hpp"
#include "anime/library.hpp"
#include "anime/model.hpp"
#include "anime/probe.hpp"
#include "anime/provider.hpp"
#include "anime/registry.hpp"
#include "anime/session.hpp"
#include "core/fs.hpp"
#include "media/subtitles.hpp"
#include "net/http.hpp"
#include "test_server.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

using namespace akeno;
using namespace akeno::anime;

namespace
{
std::string fixtures()
{
    const char *root = std::getenv("AKENO_SOURCE_ROOT");
    return std::string{root ? root : "."} + "/build/fixtures";
}

std::string fresh_dir(const char *name)
{
    char path[128];
    std::snprintf(path, sizeof(path), "/tmp/akeno-anime-%s-XXXXXX", name);
    return mkdtemp(path) ? path : "/tmp";
}

// A fictional series in two seasons, written in the order a person might:
// season 2 first, specials in between, episodes out of order.
constexpr char kCatalog[] = R"({
  "akenoCatalog": 1,
  "provider": {"id": "test-source", "name": "Test Source", "licence": "CC BY 4.0"},
  "series": [{
    "id": "signal",
    "title": "The Signal",
    "altTitles": ["Shingō no Kanata"],
    "description": "A fictional test series.",
    "poster": "art/poster.jpg",
    "genres": ["Sci-Fi", "Drama"],
    "year": 2026, "status": "finished", "score": 81, "anilistId": 4242,
    "updated": "2026-10-01",
    "seasons": [
      {"id": "s2", "number": 2, "title": "Season 2", "year": 2027, "episodes": [
        {"id": "s2e1", "number": 1, "title": "Return", "media": {"url": "media/s2e1.mp4"}}
      ]},
      {"id": "sp", "number": 0, "episodes": [
        {"id": "sp1", "number": 1, "title": "Recap", "media": {"url": "https://cdn.example/sp1.m3u8"}}
      ]},
      {"id": "s1", "number": 1, "episodes": [
        {"id": "e2", "number": 2, "media": {"url": "media/e2.mp4", "drm": "widevine"}},
        {"id": "e1", "number": 1, "title": "First Light", "thumbnail": "thumbs/e1.jpg", "duration": 1440,
         "media": {"url": "media/e1.mp4", "type": "mp4",
                   "audio": [{"language": "ja", "label": "Japanese"}],
                   "subtitles": [{"language": "en", "label": "English", "url": "subs/e1.en.vtt"},
                                 {"language": "de", "url": "subs/e1.de.ass"}]}},
        {"id": "e3", "number": 3, "title": "Members only", "media": {"url": "media/e3.mp4", "requiresAuth": true}},
        {"id": "e4", "number": 4, "title": "Elsewhere", "media": {"url": "media/e4.mp4", "regions": ["jp"]}},
        {"id": "e5", "number": 5, "title": "Announced"}
      ]}
    ]
  }]
})";

const Episode &ep(const Series &s, std::size_t season, std::size_t episode)
{
    return s.seasons.at(season).episodes.at(episode);
}

bool has_issue(const std::vector<CatalogIssue> &issues, const std::string &path_part,
               const std::string &message_part = {})
{
    for (const CatalogIssue &i : issues)
        if (i.path.find(path_part) != std::string::npos &&
            i.message.find(message_part) != std::string::npos)
            return true;
    return false;
}
} // namespace

// ---------------------------------------------------------------------------
TEST(AnimeCatalog, ParsesSeriesSeasonsAndEpisodesInOrder)
{
    CatalogOptions options;
    options.base = "https://anime.example/lib/catalog.json";
    options.region = "US";
    const ParsedCatalog c = parse_catalog(kCatalog, options);
    ASSERT_TRUE(c.ok) << (c.errors.empty() ? "" : c.errors[0].path + ": " + c.errors[0].message);
    EXPECT_EQ(c.provider.id, "test-source");
    ASSERT_EQ(c.series.size(), 1u);
    const Series &s = c.series[0];
    EXPECT_EQ(s.title, "The Signal");
    EXPECT_EQ(s.anilist_id, 4242);
    EXPECT_EQ(s.poster, "https://anime.example/lib/art/poster.jpg");
    EXPECT_EQ(s.licence, "CC BY 4.0"); // inherited from the provider
    // Seasons by number, specials last; episodes by number.
    ASSERT_EQ(s.seasons.size(), 3u);
    EXPECT_EQ(s.seasons[0].id, "s1");
    EXPECT_EQ(s.seasons[0].title, "Season 1");
    EXPECT_EQ(s.seasons[1].id, "s2");
    EXPECT_EQ(s.seasons[2].id, "sp");
    EXPECT_EQ(s.seasons[2].title, "Specials");
    ASSERT_EQ(s.seasons[0].episodes.size(), 5u);
    EXPECT_EQ(ep(s, 0, 0).id, "e1");
    EXPECT_EQ(ep(s, 0, 1).id, "e2");

    const Episode &e1 = ep(s, 0, 0);
    EXPECT_EQ(e1.title, "First Light");
    EXPECT_EQ(e1.thumbnail, "https://anime.example/lib/thumbs/e1.jpg");
    ASSERT_TRUE(e1.media.has_value());
    EXPECT_EQ(e1.media->url, "https://anime.example/lib/media/e1.mp4");
    EXPECT_EQ(e1.media->kind, media::SourceKind::http_file);
    ASSERT_EQ(e1.media->subtitles.size(), 1u); // .ass is not supported
    EXPECT_EQ(e1.media->subtitles[0].url, "https://anime.example/lib/subs/e1.en.vtt");
    EXPECT_EQ(e1.media->audio.at(0).label, "Japanese");
    EXPECT_EQ(e1.availability, Availability::unverified);
    EXPECT_TRUE(has_issue(c.warnings, "subtitles[1]", "WebVTT"));

    // Never a title that was not given; restrictions become availability.
    EXPECT_TRUE(ep(s, 0, 1).title.empty());
    EXPECT_TRUE(has_issue(c.warnings, "episodes[0].title", "number only"));
    EXPECT_EQ(ep(s, 0, 1).availability, Availability::drm_unsupported);
    EXPECT_EQ(ep(s, 0, 2).availability, Availability::auth_required);
    EXPECT_EQ(ep(s, 0, 3).availability, Availability::region_unavailable);
    EXPECT_EQ(ep(s, 0, 4).availability, Availability::episodes_indexed);
    EXPECT_FALSE(ep(s, 0, 4).media.has_value());
    EXPECT_EQ(ep(s, 2, 0).media->kind, media::SourceKind::hls);
    EXPECT_EQ(episode_label(s.seasons[2], ep(s, 2, 0)), "Special 1");
    EXPECT_EQ(episode_label(s.seasons[0], ep(s, 0, 2)), "S1 E3");
    EXPECT_EQ(playable_episodes(s), 3); // e1, s2e1, sp1 (not yet checked)
    EXPECT_EQ(episode_total(s), 7);
}

TEST(AnimeCatalog, RejectsMalformedCatalogues)
{
    CatalogOptions web;
    web.base = "https://anime.example/catalog.json";
    EXPECT_FALSE(parse_catalog("not json", web).ok);
    EXPECT_TRUE(has_issue(parse_catalog("{\"series\": []}", web).errors, "", "akenoCatalog"));
    EXPECT_TRUE(has_issue(
        parse_catalog(R"({"akenoCatalog": 2, "provider": {"name": "x"}, "series": []})", web)
            .errors,
        "akenoCatalog", "version 2"));
    EXPECT_TRUE(has_issue(
        parse_catalog(R"({"akenoCatalog": 1, "provider": {"name": "x"}, "series": {}})", web)
            .errors,
        "series", "list"));
    const auto with = [&web](const std::string &episodes, const std::string &extra_series = "")
    {
        return parse_catalog(
            R"({"akenoCatalog": 1, "provider": {"name": "x"}, "series": [{"id": "a", "title": "A", "seasons": [{"id": "s1", "episodes": [)" +
                episodes + "]}]}" + extra_series + "]}",
            web);
    };
    EXPECT_TRUE(has_issue(with(R"({"id": "e1"}, {"id": "e1"})").errors, "episodes[1].id", "twice"));
    EXPECT_TRUE(has_issue(with(R"({"id": "e1", "media": {}})").errors, "media.url", "required"));
    EXPECT_TRUE(has_issue(with(R"({"id": "e1", "media": {"url": "a.mp4", "type": "flv"}})").errors,
                          "media.type", "not a supported"));
    EXPECT_TRUE(has_issue(with(R"j({"id": "e1", "media": {"url": "javascript:alert(1)"}})j").errors,
                          "media.url", "only http"));
    // A web catalogue never names files on the console: "/path" is on its own host.
    const ParsedCatalog rooted = with(R"({"id": "e1", "media": {"url": "/data/a.mp4"}})");
    ASSERT_TRUE(rooted.ok);
    EXPECT_EQ(rooted.series[0].seasons[0].episodes[0].media->url,
              "https://anime.example/data/a.mp4");
    EXPECT_TRUE(has_issue(with(R"({"id": "e/1"})").errors, "episodes[0].id", "letters"));
    EXPECT_TRUE(has_issue(
        with(R"({"id": "e1"})", R"(, {"id": "a", "title": "Again", "seasons": [{"id": "s1"}]})")
            .errors,
        "series[1].id", "twice"));
    // A series without a title or seasons is an error, not an empty row.
    EXPECT_TRUE(has_issue(
        parse_catalog(
            R"({"akenoCatalog": 1, "provider": {"name": "x"}, "series": [{"id": "a", "seasons": []}]})",
            web)
            .errors,
        "series[0]", ""));
    EXPECT_FALSE(looks_like_catalog("<html>"));
    EXPECT_TRUE(looks_like_catalog(" {\"akenoCatalog\": 1}"));
}

TEST(AnimeCatalog, ResolvesFilesNextToALocalCatalogue)
{
    std::string why;
    const std::string base = "/app0/assets/anime/sample/catalog.json";
    EXPECT_EQ(resolve_reference(base, "media/s1e1.mp4", &why),
              "/app0/assets/anime/sample/media/s1e1.mp4");
    EXPECT_EQ(resolve_reference(base, "/mnt/usb0/anime/e1.mkv", &why), "/mnt/usb0/anime/e1.mkv");
    EXPECT_EQ(resolve_reference(base, "../../../../etc/passwd", &why), "");
    EXPECT_EQ(resolve_reference(base, "file:///etc/passwd", &why), "");
    EXPECT_EQ(resolve_reference("https://a.example/x/c.json", "//evil.example/v.mp4", &why), "");
    EXPECT_EQ(resolve_reference("https://a.example/x/c.json", "../v.mp4", &why),
              "https://a.example/v.mp4");
    EXPECT_EQ(media_kind("/app0/media/a.mp4", ""), media::SourceKind::local_file);
    EXPECT_EQ(media_kind("/app0/media/a.ts", ""), media::SourceKind::local_file);
    EXPECT_EQ(media_kind("https://a.example/a.ts?x=1", ""), media::SourceKind::http_ts);
    EXPECT_EQ(media_kind("https://a.example/master", "hls"), media::SourceKind::hls);
    EXPECT_EQ(slug("My Anime Source!"), "my-anime-source");
}

TEST(AnimeModel, SearchesTitlesWithoutLinkingByThem)
{
    Series s;
    s.title = "Frieren: Beyond Journey's End";
    s.alt_titles = {"Sōsō no Furīren"};
    EXPECT_EQ(normalize_title("Sōsō no Furīren"), "soso no furiren");
    EXPECT_EQ(normalize_title("  Café  à la Mode! "), "cafe a la mode");
    EXPECT_GT(title_match(s, "frieren"), 0);
    EXPECT_GT(title_match(s, "journey frier"), 0); // words in any order, partial
    EXPECT_GT(title_match(s, "soso furiren"), 0);  // alternative title
    EXPECT_EQ(title_match(s, "naruto"), 0);
    EXPECT_GT(title_match(s, "Frieren: Beyond Journey's End"), title_match(s, "frieren"));
}

TEST(AnimeModel, NextEpisodeCrossesSeasons)
{
    CatalogOptions options;
    options.base = "https://anime.example/c.json";
    const Series s = parse_catalog(kCatalog, options).series.at(0);
    auto next = next_episode(s, {0, 4});
    ASSERT_TRUE(next);
    EXPECT_EQ(next->season, 1u);
    EXPECT_EQ(next->episode, 0u);
    EXPECT_FALSE(next_episode(s, {2, 0}));
    auto prev = previous_episode(s, {1, 0});
    ASSERT_TRUE(prev);
    EXPECT_EQ(prev->season, 0u);
    EXPECT_EQ(prev->episode, 4u);
    EXPECT_FALSE(previous_episode(s, {0, 0}));
    ASSERT_TRUE(find_episode(s, "s2", "s2e1"));
    EXPECT_FALSE(find_episode(s, "s2", "e1"));

    const PlaybackSession session = make_session(s, {0, 0}, *ep(s, 0, 0).media);
    EXPECT_EQ(session.key(), "test-source/signal/s1/e1");
    ASSERT_TRUE(session.next);
    EXPECT_EQ(session.next_label, "S1 E2"); // no title: never invented
    EXPECT_FALSE(session.previous);
    ASSERT_EQ(session.subtitles.size(), 1u);
    const MediaItem item = session_item(session, s);
    EXPECT_EQ(item.key(), "anime:test-source/signal/s1/e1");
    EXPECT_EQ(item.subtitle, "S1 E1 - First Light");
    ASSERT_TRUE(item.playable);
    EXPECT_EQ(item.playable->url, "https://anime.example/media/e1.mp4");
    std::string p, se, sn, e;
    ASSERT_TRUE(split_episode_key(session.key(), &p, &se, &sn, &e));
    EXPECT_EQ(sn, "s1");
    EXPECT_FALSE(split_episode_key("a/b/c", &p, &se, &sn, &e));
}

// ---------------------------------------------------------------------------
TEST(AnimeProbe, JudgesTheFirstBytesNotTheStatus)
{
    std::string mp4("\0\0\0\x18"
                    "ftypisom\0\0\0\0isomiso2",
                    24);
    EXPECT_EQ(judge_media(mp4, "video/mp4").availability, Availability::playable);
    EXPECT_EQ(judge_media(mp4 + std::string("....moov....encv....", 20), "").availability,
              Availability::drm_unsupported);
    std::string ts(376, '\0');
    ts[0] = ts[188] = 0x47;
    EXPECT_EQ(judge_media(ts, "").availability, Availability::playable);
    EXPECT_EQ(judge_media(std::string("\x1A\x45\xDF\xA3", 4) + "....", "").container, "Matroska");
    EXPECT_EQ(judge_media("#EXTM3U\n#EXTINF:2,\nseg.ts\n", "").availability,
              Availability::playable);
    EXPECT_EQ(
        judge_media("#EXTM3U\n#EXT-X-KEY:METHOD=SAMPLE-AES,URI=\"skd://k\"\n#EXTINF:2,\na.ts\n", "")
            .availability,
        Availability::drm_unsupported);
    EXPECT_EQ(
        judge_media("#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"k.bin\"\n#EXTINF:2,\na.ts\n", "")
            .availability,
        Availability::playable); // standard HLS encryption, not DRM
    EXPECT_EQ(
        judge_media("<!DOCTYPE html><html><body>Episode 1</body></html>", "text/html").availability,
        Availability::provider_unsupported);
    EXPECT_EQ(judge_media("{\"error\": \"login\"}", "application/json").availability,
              Availability::provider_unsupported);
}

TEST(AnimeProbe, ChecksResourcesOnTheNetworkAndOnStorage)
{
    test::TestServer server(fixtures());
    server.override_body("page.html", "<!doctype html><title>Watch</title>");
    server.fail("members.mp4", 403);
    MediaResource m;
    m.url = server.url("h264-aac.mp4");
    EXPECT_EQ(probe_media(m, {}).availability, Availability::playable);
    m.url = server.url("hls/master.m3u8");
    EXPECT_EQ(probe_media(m, {}).availability, Availability::playable);
    m.url = server.url("page.html");
    EXPECT_EQ(probe_media(m, {}).availability, Availability::provider_unsupported);
    m.url = server.url("members.mp4");
    EXPECT_EQ(probe_media(m, {}).availability, Availability::auth_required);
    m.url = server.url("gone.mp4");
    EXPECT_EQ(probe_media(m, {}).availability, Availability::missing);
    m.url = "http://127.0.0.1:1/nothing.mp4";
    EXPECT_EQ(probe_media(m, {}).availability, Availability::network_error);
    m.url = fixtures() + "/h264-aac.mp4";
    EXPECT_EQ(probe_media(m, {}).availability, Availability::playable);
    m.url = fixtures() + "/missing.mp4";
    EXPECT_EQ(probe_media(m, {}).availability, Availability::missing);
    m.drm = "widevine";
    EXPECT_EQ(probe_media(m, {}).availability, Availability::drm_unsupported);
}

// ---------------------------------------------------------------------------
namespace
{
std::string served_catalog(const test::TestServer &server, bool drm_only = false)
{
    const std::string media = drm_only ? R"("drm": "playready")" : R"("type": "mp4")";
    return R"({"akenoCatalog": 1, "provider": {"name": "Served"}, "series": [
      {"id": "pilot", "title": "Pilot Show", "genres": ["Comedy"], "year": 2025, "anilistId": 77,
       "updated": "2026-01-02",
       "seasons": [{"id": "s1", "episodes": [
         {"id": "e1", "number": 1, "title": "One", "media": {"url": ")" +
           server.url("h264-aac.mp4") + "\", " + media + R"(}},
         {"id": "e2", "number": 2, "title": "Two", "media": {"url": "page.html"}}]}]},
      {"id": "docs", "title": "Catalogue Only", "seasons": [{"id": "s1", "episodes": [{"id": "e1", "title": "No video"}]}]}
    ]})";
}
} // namespace

TEST(AnimeRegistry, LoadsAProviderAndMeasuresAvailability)
{
    test::TestServer server(fixtures());
    server.override_body("page.html", "<!doctype html><p>not a video</p>");
    server.override_body("catalog.json", served_catalog(server));
    ProviderConfig config;
    config.id = "served";
    config.name = "Served";
    config.type = ProviderType::catalog;
    config.address = server.url("catalog.json");
    ProviderState state = Registry::load_provider(config, {}, {}, 10);
    ASSERT_TRUE(state.loaded) << state.error;
    ASSERT_EQ(state.series.size(), 2u);
    EXPECT_EQ(state.checked, 2);
    const Series &pilot = state.series[0];
    EXPECT_EQ(ep(pilot, 0, 0).availability, Availability::playable);
    EXPECT_EQ(ep(pilot, 0, 1).availability, Availability::provider_unsupported);

    const std::string dir = fresh_dir("registry");
    Registry registry(dir, "/nonexistent-app");
    registry.load();
    std::string why;
    ASSERT_TRUE(registry.add(config, &why)) << why;
    registry.install(std::move(state));
    EXPECT_EQ(registry.available().size(), 1u); // only a checked, playable series
    EXPECT_EQ(registry.available()[0]->title, "Pilot Show");
    EXPECT_EQ(registry.series_for_anilist(77).size(), 1u);
    EXPECT_TRUE(registry.series_for_anilist(78).empty());
    EXPECT_EQ(registry.recently_updated(5).size(), 1u);

    // Playback found the episode gone: the series leaves Available to Watch.
    registry.set_availability("served", "pilot", "s1", "e1", Availability::missing, "gone");
    EXPECT_TRUE(registry.available().empty());
}

TEST(AnimeRegistry, ChecksWhatASourceReallyIs)
{
    test::TestServer server(fixtures());
    server.override_body("page.html", "<!doctype html><html><body>Anime site</body></html>");
    server.override_body("catalog.json", served_catalog(server));
    server.override_body("drm.json", served_catalog(server, true));
    server.override_body(
        "meta.json",
        R"({"akenoCatalog": 1, "provider": {"name": "Meta"}, "series": [{"id": "a", "title": "A", "seasons": [{"id": "s1", "episodes": [{"id": "e1", "title": "x"}]}]}]})");
    server.override_body("list.m3u", "#EXTM3U\n#EXTINF:-1,Channel\nhttps://example.com/a.m3u8\n");
    server.fail("private.json", 401);
    const auto check = [&](const std::string &path, ProviderConfig *config = nullptr)
    { return check_source(server.url(path), "Test", {}, {}, config); };

    ProviderConfig config;
    const ProviderReport good = check("catalog.json", &config);
    EXPECT_EQ(good.source, SourceClass::native_provider);
    EXPECT_TRUE(good.importable);
    EXPECT_EQ(good.format, "AKENO catalogue v1");
    EXPECT_EQ(good.series, 2);
    EXPECT_EQ(good.episodes, 3);
    EXPECT_EQ(good.playable, 1);
    EXPECT_EQ(config.type, ProviderType::catalog);
    EXPECT_EQ(config.address, server.url("catalog.json"));

    const ProviderReport page = check("page.html");
    EXPECT_EQ(page.source, SourceClass::web_page); // HTTP 200 is not a provider
    EXPECT_FALSE(page.importable);
    EXPECT_EQ(check("list.m3u").source, SourceClass::playlist);
    EXPECT_EQ(check("h264-aac.mp4").source, SourceClass::media_file);
    EXPECT_EQ(check("drm.json").source, SourceClass::protected_source);
    EXPECT_FALSE(check("drm.json").importable);
    EXPECT_EQ(check("meta.json").source, SourceClass::metadata);
    EXPECT_EQ(check("private.json").source, SourceClass::protected_source);
    EXPECT_EQ(check_source("http://127.0.0.1:1/x.json", "", {}, {}, nullptr).source,
              SourceClass::unreachable);
    EXPECT_EQ(check_source("ftp://example.com/x", "", {}, {}, nullptr).source,
              SourceClass::unreachable);
}

TEST(AnimeRegistry, KeepsProvidersAcrossRestarts)
{
    const std::string dir = fresh_dir("providers");
    {
        Registry r(dir, dir);
        r.load();
        ASSERT_EQ(r.configs().size(), 1u); // the bundled sample series
        EXPECT_TRUE(r.configs()[0].builtin);
        std::string why;
        ProviderConfig a;
        a.name = "My Source";
        a.address = "https://a.example/catalog.json";
        ASSERT_TRUE(r.add(a, &why)) << why;
        ProviderConfig dup = a;
        dup.name = "Again";
        EXPECT_FALSE(r.add(dup, &why));
        EXPECT_NE(why.find("already"), std::string::npos);
        ProviderConfig b;
        b.name = "My Source"; // same name: a distinct id
        b.type = ProviderType::peertube;
        b.address = "https://tube.example/c/show";
        ASSERT_TRUE(r.add(b, &why));
        ProviderConfig local;
        local.address = "/data/file.json";
        EXPECT_FALSE(r.add(local, &why)); // files are only built in
        EXPECT_FALSE(r.remove(Registry::kSampleId));
    }
    // The user's own library catalogue in the install folder is built in.
    ASSERT_TRUE(fs::write_atomic(fs::join(dir, "anime-catalog.json"), "{}"));
    Registry again(dir, dir);
    again.load();
    ASSERT_EQ(again.configs().size(), 4u);
    EXPECT_EQ(again.configs()[1].id, Registry::kPersonalId);
    EXPECT_EQ(again.configs()[2].id, "my-source");
    EXPECT_EQ(again.configs()[3].id, "my-source-2");
    EXPECT_EQ(again.configs()[3].type, ProviderType::peertube);
    EXPECT_TRUE(again.remove("my-source"));
    Registry third(dir, dir);
    third.load();
    EXPECT_EQ(third.configs().size(), 3u);

    ASSERT_TRUE(fs::write_atomic(fs::join(dir, "anime-providers.json"), "{damaged"));
    Registry damaged(dir, dir);
    damaged.load();
    EXPECT_FALSE(damaged.last_error().empty());
    EXPECT_TRUE(fs::exists(fs::join(dir, "anime-providers.json.corrupt")));
}

TEST(AnimeRegistry, SearchFiltersAndDuplicateTitles)
{
    const std::string dir = fresh_dir("search");
    Registry r(dir, dir);
    r.load();
    const auto install = [&r](const std::string &id, const std::string &json)
    {
        ProviderState state;
        state.config.id = id;
        state.loaded = true;
        CatalogOptions options;
        options.provider_id = id;
        options.base = "https://" + id + ".example/c.json";
        state.series = parse_catalog(json, options).series;
        r.install(std::move(state));
    };
    // Two providers both offer a series called "Signal": both are listed, never merged.
    install(Registry::kSampleId, kCatalog);
    install(Registry::kSampleId, kCatalog); // a refresh replaces, it does not duplicate
    EXPECT_EQ(r.all_series().size(), 1u);
    SearchFilters any;
    ASSERT_EQ(r.search("signal", any).size(), 1u);
    EXPECT_EQ(r.search("kanata", any).size(), 1u); // alternative title
    SearchFilters year;
    year.year = 2027; // a season's year
    EXPECT_EQ(r.search("", year).size(), 1u);
    year.year = 1999;
    EXPECT_TRUE(r.search("", year).empty());
    SearchFilters genre;
    genre.genre = "drama";
    EXPECT_EQ(r.search("", genre).size(), 1u);
    SearchFilters status;
    status.status = "releasing";
    EXPECT_TRUE(r.search("signal", status).empty());
    SearchFilters catalogue;
    catalogue.availability = SearchFilters::Availability::catalogue;
    EXPECT_TRUE(r.search("signal", catalogue).empty());
    EXPECT_EQ(r.suggestions("sig", 5), std::vector<std::string>{"The Signal"});
    EXPECT_TRUE(r.available().empty()); // listed, but no episode checked yet
}

// ---------------------------------------------------------------------------
TEST(AnimeLibrary, WatchlistSearchesAndProgress)
{
    const std::string dir = fresh_dir("library");
    {
        Library lib(dir);
        lib.load();
        WatchlistEntry e{"test-source", "signal", 4242, "The Signal", "", 0};
        EXPECT_TRUE(lib.toggle_watchlist(e));
        WatchlistEntry anilist_only{"", "", 99, "Catalogue Title", "", 0};
        EXPECT_TRUE(lib.toggle_watchlist(anilist_only));
        lib.remember_search("frieren");
        lib.remember_search("  Frieren ");
        lib.remember_search("signal");
    }
    Library again(dir);
    again.load();
    ASSERT_EQ(again.watchlist().size(), 2u);
    EXPECT_TRUE(again.in_watchlist({"test-source", "signal", 0, "", "", 0}));
    EXPECT_EQ(again.searches(), (std::vector<std::string>{"signal", "Frieren"}));
    EXPECT_FALSE(again.toggle_watchlist({"", "", 99, "", "", 0}));
    EXPECT_EQ(again.watchlist().size(), 1u);

    // Progress per provider-qualified episode, through the watch history.
    CatalogOptions options;
    options.base = "https://anime.example/c.json";
    options.region = "US"; // E4 is offered only in Japan
    const Series s = parse_catalog(kCatalog, options).series.at(0);
    Store store(dir);
    store.load();
    EXPECT_EQ(next_up(s, store)->ref.episode, 0u); // first playable episode
    const MediaItem e1 = session_item(make_session(s, {0, 0}, *ep(s, 0, 0).media), s);
    store.record_progress(e1, 300.0, 1440.0);
    auto up = next_up(s, store);
    ASSERT_TRUE(up);
    EXPECT_TRUE(up->resume);
    EXPECT_DOUBLE_EQ(up->position, 300.0);
    EXPECT_TRUE(progress_of(store, "test-source/signal/s1/e1").started);
    EXPECT_FALSE(progress_of(store, "test-source/signal/s1/e1").watched);
    store.record_progress(e1, 1440.0, 1440.0); // finished
    up = next_up(s, store);
    ASSERT_TRUE(up);
    EXPECT_FALSE(up->resume);
    // E2 (DRM), E3 (sign-in), E4 (region) and E5 (no video) cannot play: S2 E1 is next.
    EXPECT_EQ(up->ref.season, 1u);
    EXPECT_EQ(up->ref.episode, 0u);
    EXPECT_TRUE(progress_of(store, "test-source/signal/s1/e1").watched);
    // The same episode id in another provider is another episode.
    EXPECT_FALSE(progress_of(store, "other/signal/s1/e1").started);
}

// ---------------------------------------------------------------------------
TEST(Subtitles, ReadsWebVttAndSubRip)
{
    const auto vtt = media::parse_subtitles(
        "WEBVTT\n\nNOTE a comment\n\n1\n00:00:01.000 --> 00:00:03.500 line:90%\n"
        "<i>Hello</i> &amp; welcome\n\n00:02.000 --> 00:04.000\nSecond {\\an8}line\n");
    ASSERT_TRUE(vtt.ok) << vtt.error;
    ASSERT_EQ(vtt.cues.size(), 2u);
    EXPECT_EQ(vtt.cues[0].text, "Hello & welcome");
    EXPECT_DOUBLE_EQ(vtt.cues[0].end, 3.5);
    EXPECT_EQ(media::cue_text_at(vtt.cues, 2.5), "Hello & welcome\nSecond line");
    EXPECT_EQ(media::cue_text_at(vtt.cues, 3.9), "Second line");
    EXPECT_EQ(media::cue_text_at(vtt.cues, 5.0), "");
    const auto srt = media::parse_subtitles(
        "1\r\n00:00:01,000 --> 00:00:02,000\r\nOne\r\ntwo\r\n\r\n2\r\n01:00:00,000 --> "
        "01:00:01,000\r\nLate\r\n",
        "srt");
    ASSERT_EQ(srt.cues.size(), 2u);
    EXPECT_EQ(srt.cues[0].text, "One\ntwo");
    EXPECT_DOUBLE_EQ(srt.cues[1].start, 3600.0);
    EXPECT_FALSE(media::parse_subtitles("garbage", "vtt").ok);
    EXPECT_FALSE(media::parse_subtitles("WEBVTT\n\n00:05.000 --> 00:01.000\nbackwards\n").ok);
    EXPECT_LT(media::parse_cue_time("1:2:3:4"), 0.0);
    EXPECT_DOUBLE_EQ(media::parse_cue_time("01:02.5"), 62.5);
}

// ---------------------------------------------------------------------------
namespace
{
// A PeerTube instance with a channel whose two public playlists are seasons.
net::Response peertube_instance(const net::Request &request)
{
    net::Response r;
    r.outcome = net::Outcome::ok;
    r.status = 200;
    const std::string &u = request.url;
    const std::string video =
        R"({"uuid": "%s", "name": "%n", "duration": 300, "nsfw": false, "isLive": false,
        "thumbnailPath": "/lazy-static/thumbnails/%s.jpg", "publishedAt": "2025-03-01T00:00:00Z",
        "account": {"host": "tube.example"}, "channel": {"host": "tube.example"}})";
    const auto v = [&video](const std::string &uuid, const std::string &name)
    {
        std::string out = video;
        for (std::size_t p; (p = out.find("%s")) != std::string::npos;)
            out.replace(p, 2, uuid);
        out.replace(out.find("%n"), 2, name);
        return out;
    };
    if (u == "https://tube.example/api/v1/video-channels/openanime")
        r.body = R"({"displayName": "Open Anime", "description": "CC BY episodes",
                     "avatars": [{"path": "/lazy-static/avatars/a.png", "width": 48},
                                 {"path": "/lazy-static/avatars/b.png", "width": 600}], "banners": []})";
    else if (u.rfind("https://tube.example/api/v1/video-channels/openanime/video-playlists", 0) ==
             0)
        r.body = R"({"total": 3, "data": [
          {"uuid": "p1", "displayName": "Season One", "privacy": {"id": 1}, "type": {"id": 1}, "videosLength": 2, "createdAt": "2024-01-01"},
          {"uuid": "wl", "displayName": "Watch later", "privacy": {"id": 3}, "type": {"id": 2}, "videosLength": 9},
          {"uuid": "p2", "displayName": "Season Two", "privacy": {"id": 1}, "type": {"id": 1}, "videosLength": 1, "createdAt": "2025-01-01"}]})";
    else if (u.rfind("https://tube.example/api/v1/video-playlists/p1/videos", 0) == 0)
        r.body = R"({"total": 3, "data": [{"position": 2, "video": )" + v("v2", "The Second") +
                 R"(}, {"position": 1, "video": )" + v("v1", "The First") +
                 R"(}, {"position": 3, "video": null}]})";
    else if (u.rfind("https://tube.example/api/v1/video-playlists/p2/videos", 0) == 0)
        r.body =
            R"({"total": 1, "data": [{"position": 1, "video": )" + v("v3", "Next Season") + "}]}";
    else if (u == "https://tube.example/api/v1/videos/v1")
        r.body = R"({"uuid": "v1", "name": "The First", "url": "https://tube.example/w/v1",
          "streamingPlaylists": [{"playlistUrl": "https://tube.example/static/hls/v1/master.m3u8"}], "files": []})";
    else if (u == "https://tube.example/api/v1/videos/v1/captions")
        r.body = R"({"total": 1, "data": [{"language": {"id": "en", "label": "English"},
                     "captionPath": "/lazy-static/video-captions/v1-en.vtt"}]})";
    else
    {
        r.outcome = net::Outcome::http_error;
        r.status = 404;
    }
    return r;
}
} // namespace

TEST(AnimePeerTube, ChannelPlaylistsBecomeSeasons)
{
    EXPECT_EQ(parse_peertube_address("https://tube.example/c/openanime/videos").channel,
              "openanime");
    EXPECT_EQ(parse_peertube_address("https://tube.example/video-channels/openanime").channel,
              "openanime");
    EXPECT_EQ(parse_peertube_address("https://tube.example/w/p/abc123").playlist, "abc123");
    EXPECT_EQ(parse_peertube_address("https://tube.example/videos/watch/playlist/uuid-1").playlist,
              "uuid-1");
    EXPECT_FALSE(parse_peertube_address("https://tube.example/w/abc").ok);
    EXPECT_FALSE(parse_peertube_address("http://tube.example/c/x").ok); // https only

    net::set_test_transport(peertube_instance);
    ProviderConfig config;
    config.id = "openanime";
    config.name = "Open Anime";
    config.type = ProviderType::peertube;
    config.address = "https://tube.example/c/openanime";
    config.anilist_id = 555;
    const auto provider = make_provider(config, {});
    const LoadedCatalog loaded = provider->load({});
    ASSERT_TRUE(loaded.ok) << loaded.error;
    ASSERT_EQ(loaded.series.size(), 1u);
    const Series &s = loaded.series[0];
    EXPECT_EQ(s.title, "Open Anime");
    EXPECT_EQ(s.anilist_id, 555);
    EXPECT_EQ(s.poster, "https://tube.example/lazy-static/avatars/b.png");
    ASSERT_EQ(s.seasons.size(), 2u); // the private "Watch later" list is not a season
    EXPECT_EQ(s.seasons[0].title, "Season One");
    ASSERT_EQ(s.seasons[0].episodes.size(), 2u); // the deleted element is skipped
    EXPECT_EQ(ep(s, 0, 0).title, "The First");   // ordered by playlist position
    EXPECT_EQ(ep(s, 0, 0).source_ref, "v1@tube.example");
    EXPECT_EQ(ep(s, 0, 0).thumbnail, "https://tube.example/lazy-static/thumbnails/v1.jpg");
    EXPECT_EQ(ep(s, 0, 0).availability, Availability::unverified);

    const Resolution r = provider->resolve(s, s.seasons[0], ep(s, 0, 0), {});
    ASSERT_TRUE(r.ok) << r.detail;
    EXPECT_EQ(r.media.kind, media::SourceKind::hls);
    EXPECT_EQ(r.media.url, "https://tube.example/static/hls/v1/master.m3u8");
    ASSERT_EQ(r.media.subtitles.size(), 1u);
    EXPECT_EQ(r.media.subtitles[0].url,
              "https://tube.example/lazy-static/video-captions/v1-en.vtt");
    const Resolution gone = provider->resolve(s, s.seasons[0], ep(s, 0, 1), {});
    EXPECT_FALSE(gone.ok);
    EXPECT_EQ(gone.availability, Availability::missing);

    ProviderConfig missing = config;
    missing.address = "https://tube.example/c/nobody";
    EXPECT_FALSE(make_provider(missing, {})->load({}).ok);
    net::set_test_transport({});
}
