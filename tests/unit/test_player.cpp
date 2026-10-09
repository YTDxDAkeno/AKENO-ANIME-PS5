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

TEST(Player, RefusesEncryptedStreamsWithAReason)
{
    test::TestServer server(fixtures());
    server.override_body("enc.m3u8",
                         "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-KEY:METHOD=AES-128,URI=\"k\"\n"
                         "#EXTINF:2,\nhls/v720/seg000.ts\n#EXT-X-ENDLIST\n");
    Rig rig;
    media::PlayRequest request;
    request.url = server.url("enc.m3u8");
    rig.player->play(request);
    const auto s = rig.wait_done();
    ASSERT_EQ(s.state, media::PlayerState::error);
    EXPECT_NE(s.error.find("encrypted"), std::string::npos) << s.error;
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
