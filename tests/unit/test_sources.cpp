// AKENO STREAM PS5 - User sources: formats, loading and storage.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/store.hpp"
#include "core/fs.hpp"
#include "media/player.hpp"
#include "providers/sources.hpp"
#include "software_sink.hpp"
#include "test_server.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <thread>
#include <unistd.h>

using namespace akeno;

namespace
{
std::string fixtures()
{
    const char *root = std::getenv("AKENO_SOURCE_ROOT");
    return std::string{root ? root : "."} + "/build/fixtures";
}

const MediaItem *find(const std::vector<Shelf> &shelves, const std::string &title)
{
    for (const auto &shelf : shelves)
        for (const auto &item : shelf.items)
            if (item.title == title)
                return &item;
    return nullptr;
}

constexpr char kList[] = "\xEF\xBB\xBF#EXTM3U x-tvg-url=\"https://guide.example/epg.xml\"\r\n"
                         "#EXTINF:-1 tvg-id=\"news.one\" tvg-logo=\"logos/news.png\" "
                         "group-title=\"News, Weather\",News One\r\n"
                         "#EXTVLCOPT:http-user-agent=Something\r\n"
                         "https://cdn.example/news/one.m3u8\r\n"
                         "#EXTINF:-1 group-title=\"Movies\",Open Film\r\n"
                         "films/open.mp4\r\n"
                         "#EXTINF:3600,Lecture (1 hour)\r\n"
                         "#EXTGRP:Talks\r\n"
                         "http://media.example/talks/lecture.ts\r\n"
                         "#EXTINF:-1 group-title=\"News, Weather\",Radio Stream\r\n"
                         "rtmp://live.example/app/stream\r\n"
                         "#KODIPROP:inputstream.adaptive.license_key=https://lic.example/\r\n"
                         "#EXTINF:-1 group-title=\"Movies\",Locked Film\r\n"
                         "https://cdn.example/locked.mpd\r\n"
                         "#EXTINF:-1,More Lists\r\n"
                         "https://lists.example/more.m3u\r\n";
} // namespace

TEST(Sources, ClassifiesWhatAnAddressServes)
{
    EXPECT_EQ(classify_source("", "#EXTM3U\n#EXTINF:-1,A\nhttp://a/b.m3u8\n"),
              SourceFormat::channel_list);
    EXPECT_EQ(classify_source("", "\xEF\xBB\xBF #EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nv.m3u8\n"),
              SourceFormat::hls);
    EXPECT_EQ(classify_source("", "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXTINF:6,\na.ts\n"),
              SourceFormat::hls);
    EXPECT_EQ(classify_source("application/json", "  {\"title\": \"x\", \"items\": []}"),
              SourceFormat::feed);
    EXPECT_EQ(classify_source("", "[{\"title\":\"a\",\"url\":\"https://a/b\"}]"),
              SourceFormat::feed);
    EXPECT_EQ(classify_source("", "https://a.example/one.m3u8\nhttps://a.example/two.ts\n"),
              SourceFormat::channel_list);
    EXPECT_EQ(classify_source("text/html", "<!DOCTYPE html><html><body>Videos</body></html>"),
              SourceFormat::unknown);
    std::string ts(376, '\0');
    ts[0] = ts[188] = 0x47;
    EXPECT_EQ(classify_source("", ts), SourceFormat::media);
    EXPECT_EQ(classify_source("video/mp4", std::string("\0\0\0\x20"
                                                       "ftypisom",
                                                       12)),
              SourceFormat::media);
    EXPECT_EQ(classify_source("", ""), SourceFormat::unknown);
}

TEST(Sources, GuessesThePlayerKindFromTheAddress)
{
    EXPECT_EQ(guess_source_kind("https://a/x/index.m3u8?token=1"), media::SourceKind::hls);
    EXPECT_EQ(guess_source_kind("https://a/live.ts"), media::SourceKind::http_ts);
    EXPECT_EQ(guess_source_kind("https://a/film.MKV"), media::SourceKind::http_file);
    EXPECT_EQ(guess_source_kind("https://a/film.mp4#t=10"), media::SourceKind::http_file);
    EXPECT_EQ(guess_source_kind("https://a/play?id=7"), media::SourceKind::automatic);
    EXPECT_EQ(guess_source_kind("https://a.example/dir.v2/stream"), media::SourceKind::automatic);
}

TEST(Sources, ReadsM3uListsWithGroupsLogosAndSkippedEntries)
{
    const SourceListing l = parse_channel_list(kList, "https://lists.example/tv/all.m3u", "My TV");
    ASSERT_TRUE(l.ok) << l.error;
    EXPECT_EQ(l.entries, 4u);
    ASSERT_EQ(l.shelves.size(), 4u);
    EXPECT_EQ(l.shelves[0].title, "News, Weather");
    EXPECT_EQ(l.shelves[1].title, "Movies");
    EXPECT_EQ(l.shelves[2].title, "Talks");
    EXPECT_EQ(l.shelves[3].title, "My TV"); // no group: the source name

    const MediaItem *news = find(l.shelves, "News One");
    ASSERT_NE(news, nullptr);
    EXPECT_EQ(news->provider, "source");
    EXPECT_EQ(news->badge, ""); // -1 is an unknown length, not necessarily live
    EXPECT_EQ(news->image_url, "https://lists.example/tv/logos/news.png");
    ASSERT_TRUE(news->playable);
    EXPECT_EQ(news->playable->kind, media::SourceKind::hls);
    EXPECT_EQ(news->playable->url, "https://cdn.example/news/one.m3u8");

    const MediaItem *film = find(l.shelves, "Open Film");
    ASSERT_NE(film, nullptr);
    EXPECT_EQ(film->playable->url, "https://lists.example/tv/films/open.mp4");
    EXPECT_EQ(film->playable->kind, media::SourceKind::http_file);

    const MediaItem *lecture = find(l.shelves, "Lecture (1 hour)");
    ASSERT_NE(lecture, nullptr);
    EXPECT_EQ(lecture->badge, "1:00:00");
    EXPECT_EQ(lecture->subtitle, "Talks");

    const MediaItem *more = find(l.shelves, "More Lists");
    ASSERT_NE(more, nullptr);
    EXPECT_EQ(more->kind, ItemKind::folder);
    EXPECT_FALSE(more->playable);

    EXPECT_EQ(find(l.shelves, "Locked Film"), nullptr);
    EXPECT_EQ(find(l.shelves, "Radio Stream"), nullptr);
    EXPECT_NE(l.notice.find("1 DRM-protected entry skipped"), std::string::npos) << l.notice;
    EXPECT_NE(l.notice.find("unsupported protocols (rtmp)"), std::string::npos) << l.notice;

    const SourceListing dash = parse_channel_list(
        "#EXTM3U\n#EXTINF:-1,D\nhttps://a/m.mpd\n#EXTINF:-1,H\nhttps://a/h.m3u8\n", "https://a/",
        "D");
    ASSERT_TRUE(dash.ok);
    EXPECT_EQ(dash.entries, 1u);
    EXPECT_NE(dash.notice.find("DASH"), std::string::npos) << dash.notice;
}

TEST(Sources, ReadsPlainAddressListsAndRejectsEmptyOnes)
{
    const SourceListing plain =
        parse_channel_list("https://a.example/one.m3u8\n\nhttps://a.example/films/two.mp4\n",
                           "https://a.example/list.txt", "Plain");
    ASSERT_TRUE(plain.ok);
    EXPECT_EQ(plain.entries, 2u);
    EXPECT_NE(find(plain.shelves, "one.m3u8"), nullptr);
    EXPECT_NE(find(plain.shelves, "two.mp4"), nullptr);

    const SourceListing empty = parse_channel_list("#EXTM3U\n", "https://a/l.m3u", "Empty");
    EXPECT_FALSE(empty.ok);
    EXPECT_FALSE(empty.error.empty());
    const SourceListing unusable =
        parse_channel_list("#EXTM3U\n#EXTINF:-1,A\nudp://239.0.0.1:1234\n", "https://a/l.m3u", "U");
    EXPECT_FALSE(unusable.ok);
    EXPECT_NE(unusable.error.find("udp"), std::string::npos) << unusable.error;
}

TEST(Sources, CapsHugeLists)
{
    std::string text = "#EXTM3U\n";
    for (int i = 0; i < 6000; ++i)
        text += "#EXTINF:-1 group-title=\"G" + std::to_string(i % 100) + "\",Channel " +
                std::to_string(i) + "\nhttps://c.example/" + std::to_string(i) + ".m3u8\n";
    const SourceListing l = parse_channel_list(text, "https://c.example/l.m3u", "Big");
    ASSERT_TRUE(l.ok);
    EXPECT_EQ(l.entries, kMaxSourceEntries);
    EXPECT_EQ(l.shelves.size(), kMaxSourceGroups + 1); // the rest goes to "More"
    EXPECT_EQ(l.shelves.back().title, "More");
    EXPECT_NE(l.notice.find("only the first"), std::string::npos);
}

TEST(Sources, ReadsAkenoJsonFeeds)
{
    const char feed[] = R"({
  "title": "Club Recordings",
  "description": "Our own uploads",
  "items": [
    {"title": "Final", "url": "videos/final.mp4", "group": "2026", "image": "art/final.jpg",
     "duration": 754, "description": "The final match"},
    {"title": "Stage", "url": "https://live.example/stage/index.m3u8", "live": true},
    {"title": "Archive", "url": "https://club.example/archive.json", "type": "feed"},
    {"title": "Raw", "url": "https://club.example/raw", "type": "ts"},
    {"title": "Protected", "url": "https://club.example/p.m3u8", "drm": true},
    {"title": "Broken", "url": "ftp://club.example/x.mp4"},
    {"title": "No address"}
  ]
})";
    const SourceListing l = parse_feed(feed, "https://club.example/feeds/main.json", "Club");
    ASSERT_TRUE(l.ok) << l.error;
    EXPECT_EQ(l.title, "Club Recordings");
    EXPECT_EQ(l.entries, 4u);
    const MediaItem *final_match = find(l.shelves, "Final");
    ASSERT_NE(final_match, nullptr);
    EXPECT_EQ(final_match->playable->url, "https://club.example/feeds/videos/final.mp4");
    EXPECT_EQ(final_match->image_url, "https://club.example/feeds/art/final.jpg");
    EXPECT_EQ(final_match->badge, "12:34");
    EXPECT_EQ(final_match->subtitle, "2026");
    EXPECT_EQ(find(l.shelves, "Stage")->badge, "LIVE");
    EXPECT_EQ(find(l.shelves, "Archive")->kind, ItemKind::folder);
    EXPECT_EQ(find(l.shelves, "Raw")->playable->kind, media::SourceKind::http_ts);
    EXPECT_EQ(find(l.shelves, "Protected"), nullptr);
    EXPECT_NE(l.notice.find("DRM"), std::string::npos) << l.notice;
    EXPECT_NE(l.notice.find("ftp"), std::string::npos) << l.notice;

    // The streams.json list format works as a feed too.
    const SourceListing list =
        parse_feed(R"([{"title":"A","url":"https://a.example/a.m3u8"}])", "https://a/", "L");
    ASSERT_TRUE(list.ok);
    EXPECT_EQ(list.title, "L");
    EXPECT_FALSE(parse_feed("{\"items\": 3}", "https://a/", "x").ok);
    EXPECT_FALSE(parse_feed("{not json", "https://a/", "x").ok);
}

TEST(Sources, SourceFilesRoundTrip)
{
    std::string error;
    const auto json_list = parse_source_list(
        R"({"sources":[{"name":"TV","url":"https://tv.example/list.m3u"},
                       {"url":"http://feed.example/f.json"},{"name":"bad","url":"file:///etc"}]})",
        &error);
    ASSERT_EQ(json_list.size(), 2u);
    EXPECT_EQ(json_list[0].name, "TV");
    EXPECT_EQ(json_list[1].name, "feed.example");
    EXPECT_NE(error.find("1 source skipped"), std::string::npos) << error;

    error.clear();
    const auto text_list = parse_source_text("# my sources\n"
                                             "TV Channels = https://tv.example/list.m3u?x=1&y=2\n"
                                             "https://feed.example/f.json\n"
                                             "Club | https://club.example/feed.json\n"
                                             "just text\n",
                                             &error);
    ASSERT_EQ(text_list.size(), 3u);
    EXPECT_EQ(text_list[0].name, "TV Channels");
    EXPECT_EQ(text_list[0].url, "https://tv.example/list.m3u?x=1&y=2");
    EXPECT_EQ(text_list[1].name, "feed.example");
    EXPECT_EQ(text_list[2].name, "Club");
    EXPECT_NE(error.find("1 line"), std::string::npos) << error;

    const auto again = parse_source_list(dump_source_list(json_list), nullptr);
    ASSERT_EQ(again.size(), 2u);
    EXPECT_EQ(again[0].url, json_list[0].url);
    EXPECT_EQ(again[1].name, json_list[1].name);
}

TEST(Sources, StoreKeepsSourcesAcrossRestarts)
{
    const std::string dir = "/tmp/akeno-test-sources-" + std::to_string(::getpid());
    fs::make_directory(dir);
    fs::remove_file(fs::join(dir, "sources.json"));
    fs::remove_file(fs::join(dir, "settings.json"));
    {
        Store store(dir);
        store.load();
        EXPECT_TRUE(store.sources().empty());
        EXPECT_FALSE(store.settings().sources_notice_accepted);
        EXPECT_TRUE(store.add_source({"TV", "https://tv.example/list.m3u", false}));
        EXPECT_TRUE(store.add_source({"Feed", "https://feed.example/f.json", false}));
        EXPECT_FALSE(store.add_source({"Again", "https://tv.example/list.m3u", false}));
        Settings s = store.settings();
        s.sources_notice_accepted = true;
        store.update_settings(s);
    }
    {
        Store store(dir);
        store.load();
        ASSERT_EQ(store.sources().size(), 2u);
        EXPECT_EQ(store.sources()[0].name, "TV");
        EXPECT_TRUE(store.settings().sources_notice_accepted);
        store.remove_source("https://tv.example/list.m3u");
    }
    Store store(dir);
    store.load();
    ASSERT_EQ(store.sources().size(), 1u);
    EXPECT_EQ(store.sources()[0].name, "Feed");
}

TEST(Sources, LoadsAndSearchesASourceOverHttp)
{
    test::TestServer server(fixtures());
    server.override_body("lists/tv.m3u", "#EXTM3U\n"
                                         "#EXTINF:-1 group-title=\"Test\",Ladder\n"
                                         "../hls/master.m3u8\n"
                                         "#EXTINF:8,Single Rendition\n"
                                         "../hls/v720/index.m3u8\n"
                                         "#EXTINF:-1 group-title=\"Files\",Clip\n"
                                         "../h264-aac.mp4\n");
    SourceProvider provider({"Test list", server.url("lists/tv.m3u"), false});
    const ShelvesResult r = provider.home(net::make_cancel_flag());
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(provider.format(), SourceFormat::channel_list);
    ASSERT_EQ(r.shelves.size(), 3u);
    EXPECT_EQ(r.shelves[0].title, "Test");
    const MediaItem *clip = find(r.shelves, "Clip");
    ASSERT_NE(clip, nullptr);
    EXPECT_EQ(clip->playable->url, server.url("h264-aac.mp4"));

    const ItemsResult found = provider.search("LADD", net::make_cancel_flag());
    ASSERT_TRUE(found.ok);
    ASSERT_EQ(found.items.size(), 1u);
    EXPECT_EQ(found.items[0].title, "Ladder");
    EXPECT_EQ(provider.search("", net::make_cancel_flag()).items.size(), 3u);

    // The entries play.
    auto frames = std::make_shared<media::FrameStore>(320, 180);
    auto record = std::make_shared<test::SinkRecord>();
    media::PlayerConfig config;
    config.frames = frames;
    config.make_sink = [frames, record] { return test::make_software_sink(frames, record); };
    media::Player player(config);
    media::PlayRequest request;
    request.url = clip->playable->url;
    request.kind = clip->playable->kind;
    player.play(request);
    media::PlayerStatus s;
    for (int i = 0; i < 1500; ++i)
    {
        s = player.status();
        if (s.state == media::PlayerState::ended || s.state == media::PlayerState::error)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_EQ(s.state, media::PlayerState::ended) << s.error;
    EXPECT_EQ(record->video_units.load(), 75u);
}

TEST(Sources, SingleStreamsAndUnreadableAddresses)
{
    test::TestServer server(fixtures());
    {
        SourceProvider hls({"Ladder", server.url("hls/master.m3u8"), false});
        const ShelvesResult r = hls.home(net::make_cancel_flag());
        ASSERT_TRUE(r.ok) << r.error;
        EXPECT_EQ(hls.format(), SourceFormat::hls);
        ASSERT_EQ(r.shelves.size(), 1u);
        ASSERT_EQ(r.shelves[0].items.size(), 1u);
        EXPECT_EQ(r.shelves[0].items[0].playable->kind, media::SourceKind::hls);
    }
    {
        SourceProvider file({"Clip", server.url("h264-aac-720p.ts"), false});
        const ShelvesResult r = file.home(net::make_cancel_flag());
        ASSERT_TRUE(r.ok) << r.error;
        EXPECT_EQ(file.format(), SourceFormat::media);
        EXPECT_EQ(r.shelves[0].items[0].playable->kind, media::SourceKind::automatic);
    }
    {
        server.override_body("page.html", "<!DOCTYPE html><html><body>Watch here</body></html>");
        SourceProvider page({"Page", server.url("page.html"), false});
        const ShelvesResult r = page.home(net::make_cancel_flag());
        EXPECT_FALSE(r.ok);
        EXPECT_NE(r.error.find("web page"), std::string::npos) << r.error;
    }
    {
        SourceProvider missing({"Missing", server.url("nothing.m3u"), false});
        const ShelvesResult r = missing.home(net::make_cancel_flag());
        EXPECT_FALSE(r.ok);
        EXPECT_NE(r.error.find("404"), std::string::npos) << r.error;
    }
}

TEST(Sources, PlayerExplainsChannelListsGivenAsStreams)
{
    test::TestServer server(fixtures());
    server.override_body("tv.m3u8",
                         "#EXTM3U\n#EXTINF:-1 tvg-logo=\"x.png\",One\nhls/master.m3u8\n");
    auto frames = std::make_shared<media::FrameStore>(320, 180);
    auto record = std::make_shared<test::SinkRecord>();
    media::PlayerConfig config;
    config.frames = frames;
    config.make_sink = [frames, record] { return test::make_software_sink(frames, record); };
    media::Player player(config);
    media::PlayRequest request;
    request.url = server.url("tv.m3u8");
    player.play(request);
    media::PlayerStatus s;
    for (int i = 0; i < 500; ++i)
    {
        s = player.status();
        if (s.state == media::PlayerState::error || s.state == media::PlayerState::ended)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_EQ(s.state, media::PlayerState::error);
    EXPECT_NE(s.error.find("Sources"), std::string::npos) << s.error;

    // Automatic detection names web pages and DASH manifests.
    server.override_body("page", "<!DOCTYPE html><html><body>Video</body></html>");
    server.override_body("manifest", "<?xml version=\"1.0\"?>\n<MPD xmlns=\"urn:mpeg:dash\"/>");
    for (const auto &[path, expected] :
         {std::pair{"page", "web page"}, std::pair{"manifest", "DASH"}})
    {
        request.url = server.url(path);
        request.kind = media::SourceKind::automatic;
        player.play(request);
        for (int i = 0; i < 500; ++i)
        {
            s = player.status();
            if (s.state == media::PlayerState::error || s.state == media::PlayerState::ended)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ASSERT_EQ(s.state, media::PlayerState::error) << path;
        EXPECT_NE(s.error.find(expected), std::string::npos) << s.error;
    }
}
