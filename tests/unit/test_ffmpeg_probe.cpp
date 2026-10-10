// AKENO STREAM PS5 - Regression tests for the v0.3 "ERROR -51" decode path.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// These run the same steps v0.3 performed on the console (in-memory MPEG-TS,
// custom AVIO, forced mpegts demuxer, avformat_find_stream_info, H.264
// decode) against FFmpeg 8.0.1 built with the console's configuration. A
// green result here proves the application-side logic and the FFmpeg
// configuration; only a hardware run of Diagnostics > Media self-test proves
// the console runtime.
#include "media/ffmpeg_probe.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace akeno::media;

namespace
{
std::vector<std::uint8_t> read_file(const std::string &relative)
{
    const char *root = std::getenv("AKENO_SOURCE_ROOT");
    std::ifstream in(std::string{root ? root : "."} + "/" + relative, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

ProbeReport probe(const std::string &relative, const char *format = nullptr)
{
    const auto bytes = read_file(relative);
    EXPECT_FALSE(bytes.empty()) << relative << " missing; run tools/make-media-fixtures.sh";
    ProbeOptions options;
    options.format = format;
    return probe_media(bytes.data(), bytes.size(), options);
}
} // namespace

TEST(FfmpegProbe, V03FailurePathDecodes720pH264High)
{
    const ProbeReport r = probe("build/fixtures/h264-aac-720p.ts", "mpegts");
    ASSERT_TRUE(r.success) << r.summary();
    EXPECT_EQ(r.container, "mpegts");
    EXPECT_EQ(r.video_codec, "h264");
    EXPECT_EQ(r.audio_codec, "aac");
    EXPECT_EQ(r.width, 1280);
    EXPECT_EQ(r.height, 720);
    EXPECT_EQ(r.pixel_format, "yuv420p");
    EXPECT_GE(r.frames_decoded, 1u);
    EXPECT_GE(r.stream_info_result, 0);
    EXPECT_GE(r.audio_frames_decoded, 1u);
}

TEST(FfmpegProbe, PackagedSelfTestClipDecodes)
{
    const ProbeReport r = probe("assets/selftest/h264-aac-360p.ts", "mpegts");
    ASSERT_TRUE(r.success) << r.summary();
    EXPECT_EQ(r.width, 640);
    EXPECT_EQ(r.height, 360);
}

TEST(FfmpegProbe, ContainersEnabledInTheConsoleBuild)
{
    for (const char *name : {"build/fixtures/h264-aac.mp4", "build/fixtures/h264-aac.mkv",
                             "build/fixtures/h264-mp3.ts", "build/fixtures/h264-ac3.ts",
                             "build/fixtures/hevc-aac.ts", "build/fixtures/video-only.ts"})
    {
        const ProbeReport r = probe(name);
        EXPECT_TRUE(r.success) << name << ": " << r.summary();
    }
}

TEST(FfmpegProbe, ReportsTheFailingStageForGarbage)
{
    std::vector<std::uint8_t> html(4096, ' ');
    const std::string page = "<html><body>404 Not Found</body></html>";
    std::copy(page.begin(), page.end(), html.begin());
    ProbeOptions options;
    options.format = "mpegts";
    const ProbeReport r = probe_media(html.data(), html.size(), options);
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.error, 0);
    EXPECT_FALSE(r.error_text.empty());
    EXPECT_NE(r.summary().find("FAILED"), std::string::npos);

    const ProbeReport empty = probe_media(nullptr, 0);
    EXPECT_FALSE(empty.success);
    EXPECT_EQ(empty.reached, ProbeStage::not_run);
}

TEST(FfmpegProbe, ProbedFormatAndBuildInfo)
{
    const ProbeReport r = probe("build/fixtures/h264-aac-720p.ts");
    ASSERT_TRUE(r.success) << r.summary();
    EXPECT_EQ(r.container, "mpegts");
    EXPECT_NE(ffmpeg_build_info().find("8.0"), std::string::npos);
}
