// AKENO STREAM PS5 - PeerTube and Internet Archive providers.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/fs.hpp"
#include "media/player.hpp"
#include "providers/discover.hpp"
#include "software_sink.hpp"
#include "test_server.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <thread>

using namespace akeno;

namespace
{
std::string fixtures()
{
    const char *root = std::getenv("AKENO_SOURCE_ROOT");
    return std::string{root ? root : "."} + "/build/fixtures";
}

// Shape of a Sepia Search answer (search index entries carry absolute URLs).
constexpr char kSepia[] = R"({
  "total": 3,
  "data": [
    {"uuid": "9c9de5e8-0a1e-484a-b099-e80766180a6d", "shortUUID": "kkGMgK9ZtnKfYAgnEtQxbv",
     "name": "Sprite Fright", "description": "An open movie by Blender Studio",
     "duration": 629, "views": 120345, "isLive": false, "nsfw": false,
     "publishedAt": "2021-11-25T10:00:00.000Z",
     "url": "https://video.blender.org/w/kkGMgK9ZtnKfYAgnEtQxbv",
     "thumbnailUrl": "https://video.blender.org/lazy-static/thumbnails/a.jpg",
     "previewUrl": "https://video.blender.org/lazy-static/previews/a.jpg",
     "account": {"name": "blender", "displayName": "Blender", "host": "video.blender.org"},
     "channel": {"name": "studio", "displayName": "Blender Studio", "host": "video.blender.org"}},
    {"uuid": "11111111-2222-3333-4444-555555555555", "name": "Sensitive", "nsfw": true,
     "duration": 10, "url": "https://x.example/w/abc",
     "account": {"host": "x.example"}, "channel": {"host": "x.example"}},
    {"uuid": "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee", "name": "Live Talk", "isLive": true,
     "duration": 0, "url": "https://tube.example/w/xyz",
     "account": {"displayName": "Talks", "host": "tube.example"},
     "channel": {"displayName": "Talks", "host": "tube.example"}}
  ]
})";

// Shape of an instance listing (paths relative to the instance).
constexpr char kInstance[] = R"({
  "total": 1,
  "data": [
    {"uuid": "0b1d2c3e-1111-2222-3333-444455556666", "name": "Coffee Run", "duration": 184,
     "nsfw": false, "thumbnailPath": "/lazy-static/thumbnails/c.jpg",
     "previewPath": "/lazy-static/previews/c.jpg", "views": 9000,
     "publishedAt": "2020-05-29T08:00:00.000Z",
     "account": {"displayName": "Blender", "host": "video.blender.org"},
     "channel": {"displayName": "Blender Studio", "host": "video.blender.org"}}
  ]
})";

std::string video_details(bool hls, bool nsfw = false)
{
    std::string playlists =
        hls ? R"([{"id": 1, "type": 1, "playlistUrl": "https://video.blender.org/static/streaming-playlists/hls/9c9d/master.m3u8", "files": []}])"
            : "[]";
    return std::string{R"({"uuid": "9c9de5e8-0a1e-484a-b099-e80766180a6d", "name": "Sprite Fright",
      "description": "Full description with **markdown**.", "duration": 629, "nsfw": )"} +
           (nsfw ? "true" : "false") + R"(,
      "url": "https://video.blender.org/w/kkGMgK9ZtnKfYAgnEtQxbv",
      "category": {"id": 2, "label": "Films"}, "language": {"id": "en", "label": "English"},
      "licence": {"id": 1, "label": "Attribution"},
      "account": {"displayName": "Blender", "host": "video.blender.org"},
      "channel": {"displayName": "Blender Studio", "host": "video.blender.org"},
      "streamingPlaylists": )" +
           playlists + R"(,
      "files": [
        {"resolution": {"id": 1080, "label": "1080p"}, "fileUrl": "https://video.blender.org/static/web-videos/a-1080.mp4"},
        {"resolution": {"id": 720, "label": "720p"}, "fileUrl": "https://video.blender.org/static/web-videos/a-720.mp4"},
        {"resolution": {"id": 0, "label": "Audio"}, "fileUrl": "https://video.blender.org/static/web-videos/a-0.mp4"},
        {"resolution": {"id": 2160, "label": "2160p"}, "fileUrl": "https://video.blender.org/static/web-videos/a-2160.mp4"}
      ]})";
}

std::string archive_search(const std::string &id)
{
    return R"({"responseHeader": {"status": 0}, "response": {"numFound": 3, "start": 0, "docs": [
      {"identifier": ")" +
           id + R"(", "title": "Night of the Living Dead", "year": 1968,
       "creator": "George A. Romero", "description": ["A <b>classic</b> horror film."], "downloads": 2345678},
      {"identifier": "bad/identifier", "title": "Rejected"},
      {"identifier": "Superman_Mechanical_Monsters", "title": "Superman - The Mechanical Monsters",
       "year": "1941", "downloads": 50000}
    ]}})";
}

std::string archive_metadata(const std::string &id)
{
    return R"({"server": "ia800300.us.archive.org", "dir": "/1/items/x",
      "metadata": {"identifier": ")" +
           id + R"(", "title": "Night of the Living Dead",
        "description": "Directed by George A. Romero.<br/>Public domain.", "year": "1968",
        "subject": "Horror; Zombies; Feature Film", "collection": ["feature_films", "moviesandfilms"],
        "runtime": "01:35:00"},
      "files": [
        {"name": "night.ogv", "format": "Ogg Video", "height": "480"},
        {"name": "night_512kb.mp4", "format": "512Kb MPEG4", "height": "240"},
        {"name": "Night of the Living Dead.mp4", "format": "h.264", "height": "480", "length": "5737.4"},
        {"name": "hd/Night of the Living Dead HD.mp4", "format": "h.264 IA", "height": "1080", "length": "5737.4"},
        {"name": "night.jpg", "format": "JPEG"}
      ]})";
}

media::PlayerStatus play_to_end(const Playable &playable, std::shared_ptr<test::SinkRecord> record)
{
    auto frames = std::make_shared<media::FrameStore>(320, 180);
    media::PlayerConfig config;
    config.frames = frames;
    config.make_sink = [frames, record] { return test::make_software_sink(frames, record); };
    media::Player player(config);
    media::PlayRequest request;
    request.url = playable.url;
    request.kind = playable.kind;
    player.play(request);
    media::PlayerStatus s;
    for (int i = 0; i < 1500; ++i)
    {
        s = player.status();
        if (s.state == media::PlayerState::ended || s.state == media::PlayerState::error)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return s;
}
} // namespace

TEST(PeerTube, ParsesSearchIndexResults)
{
    std::string error;
    const auto items = PeerTube::parse_search(kSepia, &error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_EQ(items.size(), 2u); // the sensitive one is dropped
    EXPECT_EQ(items[0].provider, "peertube");
    EXPECT_EQ(items[0].id, "9c9de5e8-0a1e-484a-b099-e80766180a6d@video.blender.org");
    EXPECT_EQ(items[0].title, "Sprite Fright");
    EXPECT_EQ(items[0].subtitle, "Blender Studio - video.blender.org");
    EXPECT_EQ(items[0].badge, "10:29");
    EXPECT_EQ(items[0].image_url, "https://video.blender.org/lazy-static/thumbnails/a.jpg");
    EXPECT_EQ(items[0].external_url, "https://video.blender.org/w/kkGMgK9ZtnKfYAgnEtQxbv");
    EXPECT_NE(items[0].meta.find("120.3K views"), std::string::npos) << items[0].meta;
    EXPECT_FALSE(items[0].playable); // resolved by details()
    EXPECT_EQ(items[1].badge, "LIVE");

    const auto local = PeerTube::parse_search(kInstance, &error);
    ASSERT_EQ(local.size(), 1u);
    EXPECT_EQ(local[0].image_url, "https://video.blender.org/lazy-static/thumbnails/c.jpg");
    EXPECT_EQ(local[0].banner_url, "https://video.blender.org/lazy-static/previews/c.jpg");

    EXPECT_TRUE(PeerTube::parse_search("<html>", &error).empty());
    EXPECT_FALSE(error.empty());
}

TEST(PeerTube, PicksHlsThenTheBestFileWithinTheQualityLimit)
{
    MediaItem item;
    item.provider = "peertube";
    item.id = "9c9de5e8-0a1e-484a-b099-e80766180a6d@video.blender.org";
    std::string error;
    MediaItem hls = item;
    ASSERT_TRUE(PeerTube::parse_video(video_details(true), 1080, &hls, &error)) << error;
    ASSERT_TRUE(hls.playable);
    EXPECT_EQ(hls.playable->kind, media::SourceKind::hls);
    EXPECT_NE(hls.playable->url.find("master.m3u8"), std::string::npos);
    EXPECT_EQ(hls.id, item.id);
    EXPECT_EQ(hls.description, "Full description with **markdown**.");
    ASSERT_EQ(hls.genres.size(), 3u);
    EXPECT_EQ(hls.genres[0], "Films");

    MediaItem file = item;
    ASSERT_TRUE(PeerTube::parse_video(video_details(false), 720, &file, &error)) << error;
    EXPECT_EQ(file.playable->kind, media::SourceKind::http_file);
    EXPECT_EQ(file.playable->url, "https://video.blender.org/static/web-videos/a-720.mp4");
    ASSERT_TRUE(PeerTube::parse_video(video_details(false), 480, &file, &error));
    EXPECT_EQ(file.playable->url, "https://video.blender.org/static/web-videos/a-720.mp4")
        << "nothing fits: the smallest video file";

    MediaItem sensitive = item;
    EXPECT_FALSE(PeerTube::parse_video(video_details(true, true), 1080, &sensitive, &error));
    EXPECT_NE(error.find("sensitive"), std::string::npos);
    MediaItem empty = item;
    EXPECT_FALSE(PeerTube::parse_video(
        R"({"uuid": "u", "name": "x", "files": [], "streamingPlaylists": []})", 1080, &empty,
        &error));
    EXPECT_NE(error.find("no playable file"), std::string::npos) << error;
}

TEST(PeerTube, AcceptsOnlyPlainHostsInIds)
{
    std::string uuid, host;
    EXPECT_TRUE(PeerTube::split_id("abc-123@video.example.org", &uuid, &host));
    EXPECT_EQ(uuid, "abc-123");
    EXPECT_EQ(host, "video.example.org");
    EXPECT_FALSE(PeerTube::split_id("abc@evil.example/../x", &uuid, &host));
    EXPECT_FALSE(PeerTube::split_id("abc@user@host", &uuid, &host));
    EXPECT_FALSE(PeerTube::split_id("no-host", &uuid, &host));
    EXPECT_FALSE(PeerTube::split_id("a/b@host.example", &uuid, &host));
}

TEST(InternetArchive, ParsesSearchResults)
{
    std::string error;
    const auto items = InternetArchive::parse_search(archive_search("night_of_the_living_dead"),
                                                     "https://archive.org", &error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_EQ(items.size(), 2u); // the malformed identifier is dropped
    EXPECT_EQ(items[0].provider, "archive");
    EXPECT_EQ(items[0].id, "night_of_the_living_dead");
    EXPECT_EQ(items[0].subtitle, "1968 - George A. Romero");
    EXPECT_EQ(items[0].description, "A classic horror film.");
    EXPECT_EQ(items[0].image_url, "https://archive.org/services/img/night_of_the_living_dead");
    EXPECT_EQ(items[0].external_url, "https://archive.org/details/night_of_the_living_dead");
    EXPECT_EQ(items[1].subtitle, "1941");
}

TEST(InternetArchive, PicksAnH264FileWithinTheQualityLimit)
{
    MediaItem item;
    item.provider = "archive";
    item.id = "night_of_the_living_dead";
    std::string error;
    ASSERT_TRUE(InternetArchive::parse_metadata(archive_metadata(item.id), "https://archive.org",
                                                720, &item, &error))
        << error;
    ASSERT_TRUE(item.playable);
    EXPECT_EQ(item.playable->kind, media::SourceKind::http_file);
    EXPECT_EQ(item.playable->url, "https://archive.org/download/night_of_the_living_dead/"
                                  "Night%20of%20the%20Living%20Dead.mp4");
    EXPECT_NEAR(item.duration, 5737.4, 0.01);
    EXPECT_EQ(item.meta, "1968 - 1:35:37");
    EXPECT_EQ(item.description, "Directed by George A. Romero.\nPublic domain.");
    ASSERT_EQ(item.genres.size(), 3u);
    EXPECT_EQ(item.genres[1], "Zombies");
    EXPECT_NE(item.attribution.find("public domain"), std::string::npos);

    MediaItem hd = item;
    ASSERT_TRUE(InternetArchive::parse_metadata(archive_metadata(item.id), "https://archive.org",
                                                1080, &hd, &error));
    EXPECT_EQ(hd.playable->url, "https://archive.org/download/night_of_the_living_dead/hd/"
                                "Night%20of%20the%20Living%20Dead%20HD.mp4");

    MediaItem none = item;
    none.playable.reset();
    EXPECT_FALSE(InternetArchive::parse_metadata(
        R"({"metadata": {"identifier": "x", "runtime": "10:00"},
            "files": [{"name": "a.ogv", "format": "Ogg Video"}, {"name": "b_512kb.mp4", "format": "512Kb MPEG4"}]})",
        "https://archive.org", 1080, &none, &error));
    EXPECT_NE(error.find("no MP4"), std::string::npos);
    EXPECT_FALSE(none.playable);
}

TEST(InternetArchive, SearchStaysInTheCuratedCollections)
{
    const std::string q = InternetArchive::search_query("  Nosferatu (1922) OR *:* ");
    EXPECT_EQ(q.find("*:*"), std::string::npos);
    EXPECT_NE(q.find("title:(Nosferatu  1922  OR"), std::string::npos) << q;
    EXPECT_NE(q.find("collection:(feature_films)"), std::string::npos);
    EXPECT_NE(q.find("year:[1900 TO 1963]"), std::string::npos);
    EXPECT_TRUE(InternetArchive::search_query(" ()*: ").empty());
}

TEST(Discover, InterleavesBothSources)
{
    std::vector<MediaItem> a(3), b(1);
    for (int i = 0; i < 3; ++i)
        a[static_cast<std::size_t>(i)].id = "a" + std::to_string(i);
    b[0].id = "b0";
    const auto out = Discover::interleave(a, b);
    ASSERT_EQ(out.size(), 4u);
    EXPECT_EQ(out[0].id, "a0");
    EXPECT_EQ(out[1].id, "b0");
    EXPECT_EQ(out[2].id, "a1");
    EXPECT_EQ(out[3].id, "a2");
}

TEST(InternetArchive, SearchDetailsAndPlaybackOverHttp)
{
    test::TestServer server(fixtures());
    const std::string base = server.url("");
    const std::string root = base.substr(0, base.size() - 1); // without the trailing slash
    server.override_body("advancedsearch.php", archive_search("test_film"));
    std::string metadata = archive_metadata("test_film");
    server.override_body("metadata/test_film", metadata);
    auto clip = fs::read_bytes(fixtures() + "/h264-aac.mp4");
    ASSERT_TRUE(clip);
    server.override_body("download/test_film/Night%20of%20the%20Living%20Dead.mp4",
                         std::string(clip->begin(), clip->end()));

    InternetArchive archive(root);
    archive.set_max_height(720);
    const ItemsResult found = archive.search("living dead", net::make_cancel_flag());
    ASSERT_TRUE(found.ok) << found.error;
    ASSERT_FALSE(found.items.empty());
    EXPECT_EQ(found.items[0].id, "test_film");
    const DetailsResult details = archive.details(found.items[0], net::make_cancel_flag());
    ASSERT_TRUE(details.ok) << details.error;
    ASSERT_TRUE(details.item.playable);

    auto record = std::make_shared<test::SinkRecord>();
    const auto s = play_to_end(*details.item.playable, record);
    EXPECT_EQ(s.state, media::PlayerState::ended) << s.error;
    EXPECT_EQ(record->video_units.load(), 75u);
    EXPECT_GT(record->audio_units.load(), 50u);

    const ShelvesResult home = archive.home(net::make_cancel_flag());
    ASSERT_TRUE(home.ok) << home.error;
    EXPECT_EQ(home.shelves.size(), 5u);
    EXPECT_EQ(home.shelves[0].title, "Feature Films");
    EXPECT_TRUE(home.shelves[0].portrait);
}

TEST(PeerTube, BrowsesTheSearchIndexOverHttp)
{
    test::TestServer server(fixtures());
    const std::string base = server.url("");
    server.override_body("api/v1/search/videos", kSepia);
    PeerTube peertube(base.substr(0, base.size() - 1), false);
    const ItemsResult found = peertube.search("sprite", net::make_cancel_flag());
    ASSERT_TRUE(found.ok) << found.error;
    ASSERT_EQ(found.items.size(), 2u);
    // Rows come from the index (instance rows are off: they would need the internet).
    const ShelvesResult home = peertube.home(net::make_cancel_flag());
    ASSERT_TRUE(home.ok) << home.error;
    EXPECT_EQ(home.shelves.size(), 5u);
    EXPECT_EQ(home.shelves[0].title, "Latest on PeerTube");
}
