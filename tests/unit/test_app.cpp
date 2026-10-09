// AKENO STREAM PS5 - Providers, store, input and keyboard tests.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/diagnostics.hpp"
#include "app/input.hpp"
#include "app/store.hpp"
#include "canned_api.hpp"
#include "core/fs.hpp"
#include "providers/anilist.hpp"
#include "providers/crunchyroll.hpp"
#include "providers/open_catalog.hpp"
#include "providers/youtube.hpp"
#include "ui/keyboard.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <unistd.h>

using namespace akeno;

namespace
{
std::string temp_dir(const char *name)
{
    std::string dir = std::string{"/tmp/akeno-test-"} + name + "-" + std::to_string(::getpid());
    fs::make_directory(dir);
    for (const char *f : {"settings.json", "history.json", "favorites.json", "secrets.json",
                          "settings.json.corrupt"})
        fs::remove_file(fs::join(dir, f));
    return dir;
}

MediaItem video(const std::string &id)
{
    MediaItem m;
    m.provider = "open";
    m.id = id;
    m.title = "Title " + id;
    m.playable = Playable{media::SourceKind::hls, "https://example.com/" + id + ".m3u8"};
    return m;
}
} // namespace

TEST(AniList, ParsesHomeShelves)
{
    const ShelvesResult r = AniList::parse_home(test::anilist_home_response());
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(r.shelves.size(), 4u);
    EXPECT_EQ(r.shelves[0].title, "Trending Now");
    EXPECT_TRUE(r.shelves[0].portrait);
    const MediaItem &first = r.shelves[0].items[0];
    EXPECT_EQ(first.provider, "anilist");
    EXPECT_EQ(first.id, "16498");
    EXPECT_EQ(first.title, "Attack on Titan");
    EXPECT_EQ(first.subtitle, "Shingeki no Kyojin");
    EXPECT_EQ(first.accent, 0xe4a15du);
    EXPECT_NE(first.meta.find("25 episodes"), std::string::npos);
    EXPECT_NE(first.meta.find("Wit Studio"), std::string::npos);
    EXPECT_EQ(first.badge, "25 EP");
    EXPECT_FALSE(first.playable.has_value()); // catalogue only, never fake playback
    EXPECT_EQ(r.shelves[0].items[1].badge, "AIRING");
}

TEST(AniList, ParsesDetailsWithLegalLinks)
{
    const DetailsResult d = AniList::parse_details(test::anilist_details_response());
    ASSERT_TRUE(d.ok) << d.error;
    EXPECT_EQ(d.item.description.find("<i>"), std::string::npos);
    EXPECT_NE(d.item.description.find("Titans"), std::string::npos);
    EXPECT_NE(d.item.description.find("\xE2\x80\x94"), std::string::npos); // &mdash;
    ASSERT_GE(d.related.size(), 3u);
    EXPECT_EQ(d.related[0].title, "Trailer");
    EXPECT_EQ(d.related[0].items[0].external_url, "https://www.youtube.com/watch?v=LHtdKWJdif4");
    // Only STREAMING links appear as services.
    EXPECT_EQ(d.related[1].items.size(), 1u);
    EXPECT_EQ(d.related[1].items[0].title, "Crunchyroll");
    EXPECT_EQ(d.related[2].items.size(), 4u);
    for (const auto &shelf : d.related)
        for (const auto &item : shelf.items)
            EXPECT_FALSE(item.playable.has_value());
}

TEST(AniList, ReportsRateLimitsAndSeasons)
{
    const auto r = AniList::parse_home(
        R"({"errors":[{"message":"Too Many Requests.","status":429}],"data":null})");
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("limiting"), std::string::npos);
    EXPECT_EQ(AniList::season_for(1704067200).first, "WINTER"); // 2024-01-01
    EXPECT_EQ(AniList::season_for(1717200000).first, "SPRING"); // 2024-06-01
    EXPECT_EQ(AniList::season_for(1725148800).first, "SUMMER"); // 2024-09-01
    EXPECT_EQ(AniList::season_for(1727740800).first, "FALL");   // 2024-10-01
    EXPECT_EQ(AniList::season_for(1727740800).second, 2024);
    EXPECT_TRUE(AniList::season_for(0).first.empty());
}

TEST(AniList, EndToEndThroughTheTransportSeam)
{
    net::set_test_transport(test::canned_transport);
    AniList anilist;
    const auto home = anilist.home({});
    EXPECT_TRUE(home.ok) << home.error;
    const auto search = anilist.search("frieren", {});
    ASSERT_TRUE(search.ok) << search.error;
    EXPECT_EQ(search.items.front().title, "Frieren: Beyond Journey's End");
    net::set_test_transport({});
}

TEST(YouTube, ParsesVideosAndSearch)
{
    const std::uint64_t now = 1760000000; // 2025-10-09
    const auto videos = YouTube::parse_videos(test::youtube_videos_response(), now);
    ASSERT_EQ(videos.size(), 5u);
    EXPECT_EQ(videos[0].id, "aqz-KE-bpKQ");
    EXPECT_EQ(videos[0].badge, "10:35");
    EXPECT_DOUBLE_EQ(videos[0].duration, 635.0);
    EXPECT_EQ(videos[0].external_url, "https://www.youtube.com/watch?v=aqz-KE-bpKQ");
    EXPECT_FALSE(videos[0].playable.has_value()); // official players only
    EXPECT_NE(videos[0].meta.find("18.0M views"), std::string::npos);
    EXPECT_NE(videos[0].meta.find("years ago"), std::string::npos);
    const auto results = YouTube::parse_search(test::youtube_search_response(), now);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[1].kind, ItemKind::channel);
    EXPECT_EQ(results[1].id, "channel:UCSMOQeBJ2RAnuFungnQOxLg");
}

TEST(YouTube, ExplainsApiErrors)
{
    EXPECT_NE(
        YouTube::parse_error(test::youtube_error_response("quotaExceeded", 403), 403).find("quota"),
        std::string::npos);
    EXPECT_NE(YouTube::parse_error(test::youtube_error_response("keyInvalid", 400), 400)
                  .find("not valid"),
              std::string::npos);
    EXPECT_NE(YouTube::parse_error(test::youtube_error_response("accessNotConfigured", 403), 403)
                  .find("not enabled"),
              std::string::npos);
    EXPECT_NE(YouTube::parse_error("garbage", 500).find("500"), std::string::npos);
}

TEST(YouTube, RequiresAKeyAndNeverSendsOneElsewhere)
{
    std::string key;
    YouTube yt([&] { return key; });
    EXPECT_FALSE(yt.configured());
    const auto none = yt.home({});
    EXPECT_FALSE(none.ok);
    EXPECT_NE(none.error.find("API key"), std::string::npos);
    EXPECT_EQ(yt.info().capabilities[0].support, Support::needs_setup);
    EXPECT_EQ(yt.info().capabilities[1].support, Support::unavailable);
    key = "AIzaSyD-test-key-0123456789abcdefghijkl";
    std::string seen_url;
    net::set_test_transport(
        [&](const net::Request &r)
        {
            seen_url = r.url;
            return test::canned_transport(r);
        });
    const auto home = yt.home({});
    net::set_test_transport({});
    ASSERT_TRUE(home.ok) << home.error;
    EXPECT_EQ(seen_url.rfind("https://www.googleapis.com/youtube/v3/", 0), 0u);
    EXPECT_NE(seen_url.find("key="), std::string::npos);
}

TEST(Crunchyroll, StatesItsLimitsWithoutALoginForm)
{
    Crunchyroll cr;
    const auto info = cr.info();
    int unavailable = 0;
    for (const auto &c : info.capabilities)
        unavailable += c.support == Support::unavailable;
    EXPECT_GE(unavailable, 3);
    const auto home = cr.home({});
    ASSERT_TRUE(home.ok);
    for (const auto &item : home.shelves[0].items)
        EXPECT_FALSE(item.playable.has_value());
}

TEST(OpenCatalog, UserStreamsAreValidated)
{
    std::string error;
    const auto items = parse_user_streams(
        R"([{"title":"My Channel","url":"https://tv.example/live/index.m3u8","live":true},
            {"title":"No URL"}, {"url":"https://x/y.m3u8"}, {"title":"Bad","url":"ftp://x"},
            {"title":"TS file","url":"https://cdn.example/file.ts","type":"ts"}])",
        &error);
    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(items[0].badge, "LIVE");
    EXPECT_EQ(items[0].subtitle, "tv.example");
    EXPECT_EQ(items[1].playable->kind, media::SourceKind::http_ts);
    EXPECT_NE(error.find("3 entries skipped"), std::string::npos);
    EXPECT_TRUE(parse_user_streams("{}", &error).empty());
    EXPECT_FALSE(error.empty());
    for (const auto &m : OpenCatalog::public_streams())
    {
        ASSERT_TRUE(m.playable.has_value()) << m.title;
        EXPECT_EQ(m.playable->url.rfind("https://", 0), 0u) << m.title;
        EXPECT_FALSE(m.attribution.empty()) << m.title;
    }
}

TEST(Store, SettingsRoundTripAndClamp)
{
    const std::string dir = temp_dir("settings");
    {
        Store store(dir);
        store.load();
        Settings s = store.settings();
        s.max_height = 2000;
        s.volume = 140;
        s.youtube_region = "JP";
        store.update_settings(s);
    }
    Store again(dir);
    again.load();
    EXPECT_EQ(again.settings().max_height, 1080);
    EXPECT_EQ(again.settings().volume, 100);
    EXPECT_EQ(again.settings().youtube_region, "JP");
    const auto text = fs::read_text(fs::join(dir, "settings.json"));
    ASSERT_TRUE(text);
    EXPECT_NE(text->find("\"version\": 1"), std::string::npos);
}

TEST(Store, ResumeRules)
{
    const std::string dir = temp_dir("history");
    Store store(dir);
    store.load();
    store.record_progress(video("a"), 120.0, 600.0);
    EXPECT_DOUBLE_EQ(store.resume_position("open:a"), 120.0);
    store.record_progress(video("b"), 5.0, 600.0); // too early to resume
    EXPECT_DOUBLE_EQ(store.resume_position("open:b"), 0.0);
    store.record_progress(video("c"), 590.0, 600.0); // essentially finished
    EXPECT_DOUBLE_EQ(store.resume_position("open:c"), 0.0);
    const auto cont = store.continue_watching();
    ASSERT_EQ(cont.size(), 1u);
    EXPECT_EQ(cont[0].item.id, "a");
    store.record_progress(video("a"), 300.0, 600.0);
    EXPECT_EQ(store.history().front().item.id, "a"); // most recent first
    Store reload(dir);
    reload.load();
    EXPECT_DOUBLE_EQ(reload.resume_position("open:a"), 300.0);
    EXPECT_EQ(reload.history().size(), 3u);
    reload.clear_history();
    EXPECT_TRUE(reload.continue_watching().empty());
}

TEST(Store, FavoritesAndSecrets)
{
    const std::string dir = temp_dir("fav");
    Store store(dir);
    store.load();
    EXPECT_TRUE(store.toggle_favorite(video("x")));
    EXPECT_TRUE(store.is_favorite("open:x"));
    EXPECT_FALSE(store.toggle_favorite(video("x")));
    EXPECT_FALSE(store.is_favorite("open:x"));
    store.toggle_favorite(video("y"));
    EXPECT_FALSE(plausible_youtube_key("short"));
    EXPECT_FALSE(plausible_youtube_key("BIzaSyD-test-key-0123456789abcdefghijk"));
    const std::string key = "AIzaSyD-test-key-0123456789abcdefghijkl";
    ASSERT_TRUE(plausible_youtube_key(key));
    store.set_youtube_api_key(key);
    // The key lives only in secrets.json, never in settings.
    EXPECT_EQ(fs::read_text(fs::join(dir, "settings.json")).value_or("").find("AIza"),
              std::string::npos);
    Store reload(dir);
    reload.load();
    EXPECT_EQ(reload.youtube_api_key(), key);
    EXPECT_TRUE(reload.is_favorite("open:y"));
}

TEST(Store, RecoversFromDamagedFiles)
{
    const std::string dir = temp_dir("corrupt");
    fs::write_atomic(fs::join(dir, "settings.json"), "{ this is not json");
    Store store(dir);
    store.load();
    EXPECT_EQ(store.settings().max_height, 1080);
    EXPECT_FALSE(store.last_error().empty());
    EXPECT_TRUE(fs::exists(fs::join(dir, "settings.json.corrupt")));
}

TEST(Diagnostics, ReportsNeverContainKeys)
{
    Diagnostics d;
    d.record_error(1000, "youtube",
                   "GET "
                   "https://www.googleapis.com/youtube/v3/"
                   "search?q=x&key=AIzaSyD-test-key-0123456789abcdefghijkl failed");
    DiagnosticSnapshot s;
    s.app_version = "1.0.0";
    s.player.error = "token AIzaSyD-test-key-0123456789abcdefghijkl leaked?";
    const std::string report = d.build_report(s);
    EXPECT_EQ(report.find("AIzaSyD"), std::string::npos);
    EXPECT_NE(report.find("key=REDACTED"), std::string::npos);
    EXPECT_NE(report.find("AKENO STREAM DIAGNOSTICS"), std::string::npos);
    std::string path, error;
    const std::string dir = temp_dir("diag");
    ASSERT_TRUE(Diagnostics::export_report(dir, report, 1760000000, &path, &error)) << error;
    EXPECT_NE(path.find("akeno-diagnostics-2025"), std::string::npos);
    EXPECT_TRUE(fs::exists(path));
}

TEST(Providers, TextHelpers)
{
    EXPECT_EQ(strip_html("A<br>B &amp; C <i>D</i>"), "A\nB & C D");
    EXPECT_DOUBLE_EQ(parse_iso8601_duration("PT1H2M3S"), 3723.0);
    EXPECT_DOUBLE_EQ(parse_iso8601_duration("PT45S"), 45.0);
    EXPECT_DOUBLE_EQ(parse_iso8601_duration("P1DT1S"), 86401.0);
    EXPECT_DOUBLE_EQ(parse_iso8601_duration("garbage"), 0.0);
    EXPECT_DOUBLE_EQ(parse_iso8601_duration("PT5"), 0.0);
    EXPECT_EQ(format_clock(3723), "1:02:03");
    EXPECT_EQ(format_clock(63), "1:03");
    EXPECT_EQ(format_clock(-5), "0:00");
    EXPECT_EQ(format_count(1234567), "1.2M");
    EXPECT_EQ(format_count(999), "999");
}

TEST(Input, EdgesRepeatsAndInterception)
{
    input::Mapper mapper;
    std::vector<input::Event> events;
    mapper.feed({input::bits::cross, 128, 128, true}, 0, events);
    mapper.feed({input::bits::cross, 128, 128, true}, 16, events); // held: no new press
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].button, input::Button::cross);
    events.clear();
    mapper.feed({input::bits::down, 128, 128, true}, 100, events);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_FALSE(events[0].repeat);
    mapper.tick(300, events);
    EXPECT_EQ(events.size(), 1u); // before the repeat delay
    mapper.tick(100 + input::Mapper::kRepeatDelayMs, events);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_TRUE(events[1].repeat);
    events.clear();
    // Left stick acts as a d-pad beyond the dead zone.
    mapper.feed({0, 128, 128, true}, 2000, events);
    mapper.feed({0, 250, 128, true}, 2016, events);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].button, input::Button::right);
    events.clear();
    // While the system UI intercepts input nothing fires, and nothing fires on return.
    mapper.feed({input::bits::circle | input::bits::intercepted, 128, 128, true}, 3000, events);
    mapper.feed({input::bits::circle, 128, 128, true}, 3016, events);
    EXPECT_TRUE(events.empty());
    // Disconnect resets state.
    mapper.feed({0, 128, 128, false}, 4000, events);
    EXPECT_FALSE(mapper.connected());
}

TEST(Keyboard, TypesEditsAndSubmits)
{
    ui::Keyboard kb;
    kb.open("Search", "", 5);
    EXPECT_TRUE(kb.active());
    // Row 1 starts at 'q'.
    EXPECT_EQ(kb.handle(input::Button::cross), ui::Keyboard::Result::changed);
    EXPECT_EQ(kb.text(), "q");
    kb.handle(input::Button::l1); // shift
    kb.handle(input::Button::right);
    kb.handle(input::Button::cross);
    EXPECT_EQ(kb.text(), "qW");
    kb.handle(input::Button::square); // delete
    EXPECT_EQ(kb.text(), "q");
    for (int i = 0; i < 10; ++i)
        kb.handle(input::Button::cross);
    EXPECT_EQ(kb.text().size(), 5u); // length limit
    EXPECT_EQ(kb.handle(input::Button::options), ui::Keyboard::Result::submitted);
    EXPECT_FALSE(kb.active());
    kb.open("Key", "abc", 39, true);
    EXPECT_EQ(kb.handle(input::Button::circle), ui::Keyboard::Result::cancelled);
}
