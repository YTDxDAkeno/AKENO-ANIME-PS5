// AKENO STREAM PS5 - Playback engine integration tests (host).
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drives the real Player (HTTP client, HLS parser, vendored MPEG-TS demuxer,
// FFmpeg remuxer, frame store) against a local HTTP server and generated
// fixtures. The console's hardware decoder is replaced by a software decoder
// that checks every submitted access unit is decodable.
#include "media/player.hpp"
#include "net/http.hpp"
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

struct Rig
{
    std::shared_ptr<media::FrameStore> frames = std::make_shared<media::FrameStore>(320, 180);
    std::shared_ptr<test::SinkRecord> record = std::make_shared<test::SinkRecord>();
    std::unique_ptr<media::Player> player;

    Rig()
    {
        net::Client::global_init();
        media::PlayerConfig config;
        config.frames = frames;
        auto f = frames;
        auto r = record;
        config.make_sink = [f, r] { return test::make_software_sink(f, r); };
        config.segment_retries = 1;
        player = std::make_unique<media::Player>(config);
    }

    media::PlayerStatus wait_done(int seconds = 30)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        media::PlayerStatus s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            s = player->status();
            if (s.state == media::PlayerState::ended || s.state == media::PlayerState::error ||
                s.state == media::PlayerState::stopped)
                return s;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        ADD_FAILURE() << "timed out in state " << media::state_name(s.state);
        return s;
    }
};
} // namespace

TEST(Player, PlaysHlsVodToTheEnd)
{
    test::TestServer server(fixtures());
    Rig rig;
    media::PlayRequest request;
    request.url = server.url("hls/master.m3u8");
    request.title = "Fixture";
    rig.player->play(request);
    const auto s = rig.wait_done();
    ASSERT_EQ(s.state, media::PlayerState::ended) << s.error;
    EXPECT_EQ(s.variant, "1280x720 @ 2.5 Mbps");
    EXPECT_EQ(s.variant_count, 2);
    EXPECT_EQ(s.segments_total, 4);
    EXPECT_EQ(s.segments_loaded, 4);
    EXPECT_FALSE(s.live);
    EXPECT_NEAR(s.duration, 8.0, 0.2);
    EXPECT_EQ(s.video_codec, "H.264");
    EXPECT_EQ(s.audio_codec, "AAC");
    EXPECT_EQ(s.width, 1280);
    EXPECT_EQ(s.height, 720);
    EXPECT_EQ(rig.record->opens.load(), 1);
    EXPECT_EQ(rig.record->video_units.load(), 200u); // 8 s at 25 fps
    EXPECT_GE(rig.record->frames.load(), 199u);
    EXPECT_EQ(rig.record->decode_errors.load(), 0u);
    EXPECT_GT(rig.record->audio_units.load(), 300u); // ~375 AAC frames
    EXPECT_EQ(rig.record->audio_type.load(), 0x0fu);
    EXPECT_GE(rig.record->drains.load(), 1);
    EXPECT_GT(rig.frames->serial(), 0u);
    EXPECT_NEAR(s.position, 8.0, 0.5);
}

TEST(Player, HonoursTheQualityCap)
{
    test::TestServer server(fixtures());
    Rig rig;
    media::PlayRequest request;
    request.url = server.url("hls/master.m3u8");
    request.max_height = 480;
    rig.player->play(request);
    const auto s = rig.wait_done();
    ASSERT_EQ(s.state, media::PlayerState::ended) << s.error;
    EXPECT_EQ(s.width, 640);
    EXPECT_EQ(s.height, 360);
}

TEST(Player, ResumesFromAPosition)
{
    test::TestServer server(fixtures());
    Rig rig;
    media::PlayRequest request;
    request.url = server.url("hls/v720/index.m3u8");
    request.start_seconds = 4.5;
    rig.player->play(request);
    const auto s = rig.wait_done();
    ASSERT_EQ(s.state, media::PlayerState::ended) << s.error;
    // Starts at the segment containing 4.5 s (segment 2 at 4.0 s).
    EXPECT_EQ(rig.record->video_units.load(), 100u);
    EXPECT_EQ(s.segments_loaded, 2);
}

TEST(Player, SeekingRestartsTheSessionAtTheTarget)
{
    test::TestServer server(fixtures());
    Rig rig;
    media::PlayRequest request;
    request.url = server.url("hls/v720/index.m3u8");
    rig.player->play(request);
    // Wait until it is playing, then jump near the end.
    for (int i = 0; i < 500 && rig.player->status().state != media::PlayerState::playing &&
                    rig.player->status().state != media::PlayerState::ended;
         ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    rig.player->seek_to(6.2);
    const auto s = rig.wait_done();
    EXPECT_EQ(s.state, media::PlayerState::ended) << s.error;
    EXPECT_GE(rig.record->sinks_created.load(), 1);
}

TEST(Player, PlaysLocalContainersThroughTheRemuxer)
{
    for (const char *name : {"h264-aac.mp4", "h264-aac.mkv", "hevc-aac.ts", "h264-ac3.ts"})
    {
        Rig rig;
        media::PlayRequest request;
        request.url = fixtures() + "/" + name;
        request.kind = media::SourceKind::local_file;
        rig.player->play(request);
        const auto s = rig.wait_done();
        ASSERT_EQ(s.state, media::PlayerState::ended) << name << ": " << s.error;
        EXPECT_EQ(rig.record->video_units.load(), 75u) << name; // 3 s at 25 fps
        EXPECT_GE(rig.record->frames.load(), 74u) << name;
        EXPECT_GT(rig.record->audio_units.load(), 50u) << name;
        EXPECT_NEAR(s.duration, 3.0, 0.2) << name;
        EXPECT_TRUE(s.seekable) << name;
    }
}

TEST(Player, Mp3AudioIsReportedAsUnsupportedButVideoPlays)
{
    Rig rig;
    media::PlayRequest request;
    request.url = fixtures() + "/h264-mp3.ts";
    request.kind = media::SourceKind::local_file;
    rig.player->play(request);
    const auto s = rig.wait_done();
    ASSERT_EQ(s.state, media::PlayerState::ended) << s.error;
    EXPECT_EQ(rig.record->video_units.load(), 75u);
    EXPECT_EQ(rig.record->audio_units.load(), 0u);
    EXPECT_NE(s.notice.find("mp3"), std::string::npos) << s.notice;
}

TEST(Player, LocalSeekReportsTheRealKeyframePosition)
{
    Rig rig;
    media::PlayRequest request;
    request.url = fixtures() + "/h264-aac-720p.ts";
    request.kind = media::SourceKind::local_file;
    request.start_seconds = 3.0;
    rig.player->play(request);
    const auto s = rig.wait_done();
    ASSERT_EQ(s.state, media::PlayerState::ended) << s.error;
    // GOP of 50 frames: the seek lands on the keyframe at 2.0 s, so 4 s remain.
    EXPECT_EQ(rig.record->video_units.load(), 100u);
}

TEST(Player, StreamsASingleTransportStreamOverHttp)
{
    test::TestServer server(fixtures());
    Rig rig;
    media::PlayRequest request;
    request.url = server.url("h264-aac-720p.ts");
    request.kind = media::SourceKind::http_ts;
    rig.player->play(request);
    const auto s = rig.wait_done();
    ASSERT_EQ(s.state, media::PlayerState::ended) << s.error;
    EXPECT_EQ(rig.record->video_units.load(), 150u);
}

TEST(Player, StopIsPromptAndClean)
{
    test::TestServer server(fixtures());
    Rig rig;
    media::PlayRequest request;
    request.url = server.url("hls/master.m3u8");
    rig.player->play(request);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto started = std::chrono::steady_clock::now();
    rig.player->stop();
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(3));
    const auto s = rig.player->status();
    EXPECT_TRUE(s.state == media::PlayerState::stopped || s.state == media::PlayerState::ended)
        << media::state_name(s.state);
    EXPECT_EQ(rig.record->opens.load(), rig.record->closes.load());
    // Replaying after a stop works.
    rig.player->play(request);
    EXPECT_EQ(rig.wait_done().state, media::PlayerState::ended);
}

TEST(Player, ReportsMissingPlaylists)
{
    test::TestServer server(fixtures());
    Rig rig;
    media::PlayRequest request;
    request.url = server.url("hls/missing.m3u8");
    rig.player->play(request);
    const auto s = rig.wait_done();
    ASSERT_EQ(s.state, media::PlayerState::error);
    EXPECT_NE(s.error.find("HTTP 404"), std::string::npos) << s.error;
    EXPECT_EQ(rig.record->sinks_created.load(), 0);
}

TEST(Player, RejectsSegmentsThatAreNotTransportStreams)
{
    test::TestServer server(fixtures());
    server.override_body("hls/v720/seg000.ts",
                         "<html><body>Access denied</body></html>" + std::string(400, ' '));
    Rig rig;
    media::PlayRequest request;
    request.url = server.url("hls/v720/index.m3u8");
    rig.player->play(request);
    const auto s = rig.wait_done();
    ASSERT_EQ(s.state, media::PlayerState::error);
    EXPECT_NE(s.error.find("not MPEG-TS"), std::string::npos) << s.error;
}

TEST(Player, RefusesDrmStreamsWithAReason)
{
    test::TestServer server(fixtures());
    server.override_body("drm.m3u8", "#EXTM3U\n#EXT-X-TARGETDURATION:2\n"
                                     "#EXT-X-KEY:METHOD=SAMPLE-AES,URI=\"skd://k\","
                                     "KEYFORMAT=\"com.apple.streamingkeydelivery\"\n"
                                     "#EXTINF:2,\nhls/v720/seg000.ts\n#EXT-X-ENDLIST\n");
    Rig rig;
    media::PlayRequest request;
    request.url = server.url("drm.m3u8");
    rig.player->play(request);
    const auto s = rig.wait_done();
    ASSERT_EQ(s.state, media::PlayerState::error);
    EXPECT_NE(s.error.find("DRM"), std::string::npos) << s.error;
    EXPECT_EQ(rig.record->sinks_created.load(), 0);
}

TEST(Player, PlaysFragmentedMp4Hls)
{
    test::TestServer server(fixtures());
    Rig rig;
    media::PlayRequest request;
    request.url = server.url("hls/fmp4/index.m3u8");
    rig.player->play(request);
    const auto s = rig.wait_done();
    ASSERT_EQ(s.state, media::PlayerState::ended) << s.error;
    EXPECT_EQ(s.container, "HLS (fragmented MP4)");
    EXPECT_EQ(s.segments_loaded, 3);
    EXPECT_EQ(rig.record->video_units.load(), 150u); // 6 s at 25 fps
    EXPECT_GE(rig.record->frames.load(), 149u);
    EXPECT_EQ(rig.record->decode_errors.load(), 0u);
    EXPECT_GT(rig.record->audio_units.load(), 250u); // ~281 AAC frames
    EXPECT_EQ(rig.record->audio_type.load(), 0x0fu);
    EXPECT_NEAR(s.position, 6.0, 0.5);
}

TEST(Player, FragmentedMp4HlsResumesAtASegment)
{
    test::TestServer server(fixtures());
    Rig rig;
    media::PlayRequest request;
    request.url = server.url("hls/fmp4/index.m3u8");
    request.start_seconds = 4.5;
    rig.player->play(request);
    const auto s = rig.wait_done();
    ASSERT_EQ(s.state, media::PlayerState::ended) << s.error;
    EXPECT_EQ(rig.record->video_units.load(), 50u); // the segment at 4.0 s
    EXPECT_EQ(rig.record->decode_errors.load(), 0u);
    EXPECT_NEAR(s.position, 6.0, 0.5);
}

TEST(Player, PlaysASeparateAudioRendition)
{
    test::TestServer server(fixtures());
    Rig rig;
    media::PlayRequest request;
    request.url = server.url("hls/split/master.m3u8");
    rig.player->play(request);
    const auto s = rig.wait_done();
    ASSERT_EQ(s.state, media::PlayerState::ended) << s.error << " / " << s.notice;
    EXPECT_TRUE(s.notice.empty()) << s.notice;
    EXPECT_EQ(rig.record->video_units.load(), 150u);
    EXPECT_EQ(rig.record->decode_errors.load(), 0u);
    EXPECT_GT(rig.record->audio_units.load(), 250u);
    EXPECT_EQ(rig.record->audio_type.load(), 0x0fu);
}

TEST(Player, MissingAudioRenditionFallsBackToVideo)
{
    test::TestServer server(fixtures());
    server.fail("hls/split/audio/index.m3u8", 404);
    Rig rig;
    media::PlayRequest request;
    request.url = server.url("hls/split/master.m3u8");
    rig.player->play(request);
    const auto s = rig.wait_done();
    ASSERT_EQ(s.state, media::PlayerState::ended) << s.error;
    EXPECT_NE(s.notice.find("audio track could not be loaded"), std::string::npos) << s.notice;
    EXPECT_EQ(rig.record->video_units.load(), 150u);
    EXPECT_EQ(rig.record->audio_units.load(), 0u);
}

TEST(Player, PlaysAes128EncryptedHls)
{
    for (const char *playlist : {"hls/aes/index.m3u8", "hls/aes-seq/index.m3u8"})
    {
        test::TestServer server(fixtures());
        Rig rig;
        media::PlayRequest request;
        request.url = server.url(playlist);
        rig.player->play(request);
        const auto s = rig.wait_done();
        ASSERT_EQ(s.state, media::PlayerState::ended) << playlist << ": " << s.error;
        EXPECT_GE(rig.record->video_units.load(), 150u) << playlist;
        EXPECT_EQ(rig.record->decode_errors.load(), 0u) << playlist;
        EXPECT_GT(rig.record->audio_units.load(), 250u) << playlist;
    }
}

TEST(Player, WrongAes128KeyIsAnError)
{
    test::TestServer server(fixtures());
    server.override_body("hls/aes-seq/key.bin", std::string(16, '\x42'));
    Rig rig;
    media::PlayRequest request;
    request.url = server.url("hls/aes-seq/index.m3u8");
    rig.player->play(request);
    const auto s = rig.wait_done();
    ASSERT_EQ(s.state, media::PlayerState::error);
    EXPECT_TRUE(s.error.find("decrypted") != std::string::npos ||
                s.error.find("not MPEG-TS") != std::string::npos)
        << s.error;
}

TEST(Player, PlaysByteRangeSegments)
{
    for (const bool ranges : {true, false})
    {
        for (const char *playlist : {"hls/single/index.m3u8", "hls/single-fmp4/index.m3u8"})
        {
            test::TestServer server(fixtures());
            server.ignore_ranges(!ranges);
            Rig rig;
            media::PlayRequest request;
            request.url = server.url(playlist);
            rig.player->play(request);
            const auto s = rig.wait_done();
            ASSERT_EQ(s.state, media::PlayerState::ended) << playlist << ": " << s.error;
            EXPECT_EQ(rig.record->video_units.load(), 150u) << playlist;
            EXPECT_EQ(rig.record->decode_errors.load(), 0u) << playlist;
            EXPECT_GE(server.range_requests(), 3) << playlist;
        }
    }
}

TEST(Player, PlaysWebFilesWithRangeRequests)
{
    for (const char *name : {"h264-aac.mp4", "h264-aac.mkv"})
    {
        test::TestServer server(fixtures());
        Rig rig;
        media::PlayRequest request;
        request.url = server.url(name);
        request.kind = media::SourceKind::automatic;
        rig.player->play(request);
        const auto s = rig.wait_done();
        ASSERT_EQ(s.state, media::PlayerState::ended) << name << ": " << s.error;
        EXPECT_EQ(rig.record->video_units.load(), 75u) << name;
        EXPECT_GT(rig.record->audio_units.load(), 50u) << name;
        EXPECT_NEAR(s.duration, 3.0, 0.2) << name;
        EXPECT_TRUE(s.seekable) << name;
        EXPECT_GT(s.bytes_downloaded, 0u) << name;
        EXPECT_EQ(s.last_http_status, 206) << name;
    }
}

TEST(Player, WebFileResumesAtAKeyframe)
{
    test::TestServer server(fixtures());
    Rig rig;
    media::PlayRequest request;
    request.url = server.url("h264-aac-720p.ts");
    request.kind = media::SourceKind::http_file;
    request.start_seconds = 3.0;
    rig.player->play(request);
    const auto s = rig.wait_done();
    ASSERT_EQ(s.state, media::PlayerState::ended) << s.error;
    EXPECT_EQ(rig.record->video_units.load(), 100u); // keyframe at 2.0 s
    EXPECT_GE(server.range_requests(), 2);
}

TEST(Player, WebFileWithoutRangeSupportStillPlays)
{
    test::TestServer server(fixtures());
    server.ignore_ranges(true);
    Rig rig;
    media::PlayRequest request;
    request.url = server.url("h264-aac.mp4"); // index at the end of the file
    request.kind = media::SourceKind::http_file;
    request.start_seconds = 1.0;
    rig.player->play(request);
    const auto s = rig.wait_done();
    ASSERT_EQ(s.state, media::PlayerState::ended) << s.error;
    EXPECT_GT(rig.record->video_units.load(), 0u);
    EXPECT_LE(rig.record->video_units.load(), 75u);
    EXPECT_EQ(rig.record->decode_errors.load(), 0u);
}

TEST(Player, DetectsWhatAnAddressServes)
{
    struct Case
    {
        const char *path;
        const char *container;
        std::uint64_t video_units;
    };
    for (const Case &c : {Case{"hls/v720/index.m3u8", "HLS (MPEG-TS)", 200u},
                          Case{"h264-aac-720p.ts", "MPEG-TS", 150u}})
    {
        test::TestServer server(fixtures());
        Rig rig;
        media::PlayRequest request;
        request.url = server.url(c.path);
        request.kind = media::SourceKind::automatic;
        rig.player->play(request);
        const auto s = rig.wait_done();
        ASSERT_EQ(s.state, media::PlayerState::ended) << c.path << ": " << s.error;
        EXPECT_EQ(s.container, c.container) << c.path;
        EXPECT_EQ(rig.record->video_units.load(), c.video_units) << c.path;
    }
    test::TestServer server(fixtures());
    Rig rig;
    media::PlayRequest request;
    request.url = server.url("nothing-here.mp4");
    request.kind = media::SourceKind::automatic;
    rig.player->play(request);
    const auto s = rig.wait_done();
    ASSERT_EQ(s.state, media::PlayerState::error);
    EXPECT_NE(s.error.find("404"), std::string::npos) << s.error;
}

TEST(Player, SniffsSourceKinds)
{
    using media::SourceKind;
    EXPECT_EQ(media::sniff_source("", "#EXTM3U\n#EXT-X-VERSION:3\n"), SourceKind::hls);
    EXPECT_EQ(media::sniff_source("", "\xEF\xBB\xBF\r\n#EXTM3U\n"), SourceKind::hls);
    EXPECT_EQ(media::sniff_source("application/vnd.apple.mpegURL", "garbage"), SourceKind::hls);
    std::string ts(376, '\0');
    ts[0] = ts[188] = 0x47;
    EXPECT_EQ(media::sniff_source("video/mp2t", ts), SourceKind::http_ts);
    ts[188] = 0;
    EXPECT_EQ(media::sniff_source("", ts), SourceKind::http_file);
    EXPECT_EQ(media::sniff_source("video/mp4", std::string("\0\0\0\x20"
                                                           "ftypisom",
                                                           12)),
              SourceKind::http_file);
}

TEST(Player, SegmentFailureAfterRetriesIsAnError)
{
    test::TestServer server(fixtures());
    server.fail("hls/v720/seg002.ts", 503);
    Rig rig;
    media::PlayRequest request;
    request.url = server.url("hls/v720/index.m3u8");
    rig.player->play(request);
    const auto s = rig.wait_done();
    ASSERT_EQ(s.state, media::PlayerState::error);
    EXPECT_NE(s.error.find("503"), std::string::npos) << s.error;
    EXPECT_GE(s.retries, 1);
    EXPECT_EQ(rig.record->opens.load(), rig.record->closes.load());
}

TEST(Player, LocalFileErrorsAreExplained)
{
    Rig rig;
    media::PlayRequest request;
    request.url = fixtures() + "/does-not-exist.mp4";
    request.kind = media::SourceKind::local_file;
    rig.player->play(request);
    const auto s = rig.wait_done();
    ASSERT_EQ(s.state, media::PlayerState::error);
    EXPECT_NE(s.error.find("cannot open"), std::string::npos) << s.error;
}
