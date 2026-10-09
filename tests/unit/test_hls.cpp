// AKENO STREAM PS5 - HLS parser and variant selection tests.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/hls.hpp"

#include <gtest/gtest.h>

using namespace akeno;

namespace
{
// Structure of the public mux.dev Big Buck Bunny master that v0.2 fetched on a
// real PS5 (five variants).
constexpr char kMaster[] = R"(#EXTM3U
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=2149280,CODECS="mp4a.40.2,avc1.64001f",RESOLUTION=1280x720,NAME="720"
url_0/193039199_mp4_h264_aac_hd_7.m3u8
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=246440,CODECS="mp4a.40.5,avc1.42000d",RESOLUTION=320x184,NAME="240"
url_2/193039199_mp4_h264_aac_ld_7.m3u8
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=460560,CODECS="mp4a.40.5,avc1.420016",RESOLUTION=512x288,NAME="380"
url_4/193039199_mp4_h264_aac_7.m3u8
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=836280,CODECS="mp4a.40.2,avc1.64001f",RESOLUTION=848x480,NAME="480"
url_6/193039199_mp4_h264_aac_hq_7.m3u8
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=6221600,CODECS="mp4a.40.2,avc1.640028",RESOLUTION=1920x1080,NAME="1080"
url_8/193039199_mp4_h264_aac_fhd_7.m3u8
)";
constexpr char kBase[] = "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8";
} // namespace

TEST(Hls, ParsesMasterPlaylist)
{
    const auto r = hls::parse(kMaster, kBase);
    ASSERT_TRUE(r.ok) << r.error;
    const auto &pl = r.playlist;
    EXPECT_EQ(pl.kind, hls::Kind::master);
    ASSERT_EQ(pl.variants.size(), 5u);
    EXPECT_EQ(pl.variants[0].bandwidth, 2149280u);
    EXPECT_EQ(pl.variants[0].width, 1280);
    EXPECT_EQ(pl.variants[0].height, 720);
    EXPECT_EQ(pl.variants[0].codecs, "mp4a.40.2,avc1.64001f");
    EXPECT_EQ(pl.variants[0].uri,
              "https://test-streams.mux.dev/x36xhzz/url_0/193039199_mp4_h264_aac_hd_7.m3u8");
    EXPECT_TRUE(pl.unsupported.empty());
}

TEST(Hls, SelectsBestVariantWithinLimits)
{
    const auto r = hls::parse(kMaster, kBase);
    ASSERT_TRUE(r.ok);
    hls::Selection full;
    EXPECT_EQ(hls::select_variant(r.playlist, full), 4); // 1080p
    hls::Selection hd;
    hd.max_width = 1280;
    hd.max_height = 720;
    EXPECT_EQ(hls::select_variant(r.playlist, hd), 0);
    hls::Selection tiny;
    tiny.max_width = 100;
    tiny.max_height = 100;
    // Nothing fits: fall back to the lightest playable variant.
    EXPECT_EQ(hls::select_variant(r.playlist, tiny), 1);
    hls::Selection capped;
    capped.max_bandwidth = 1000000;
    EXPECT_EQ(hls::select_variant(r.playlist, capped), 3);
}

TEST(Hls, ParsesVodMediaPlaylistWithTimeline)
{
    const char text[] = "#EXTM3U\r\n#EXT-X-VERSION:3\r\n#EXT-X-PLAYLIST-TYPE:VOD\r\n"
                        "#EXT-X-TARGETDURATION:11\r\n#EXT-X-MEDIA-SEQUENCE:7\r\n"
                        "#EXTINF:10.000,\r\nurl_462/seg0.ts\r\n"
                        "#EXTINF:10.000,\r\nurl_463/seg1.ts\r\n"
                        "#EXT-X-DISCONTINUITY\r\n#EXTINF:4.5,title\r\n../other/seg2.ts\r\n"
                        "#EXT-X-ENDLIST\r\n";
    const auto r = hls::parse(text, "https://cdn.example/x/url_0/index.m3u8");
    ASSERT_TRUE(r.ok) << r.error;
    const auto &pl = r.playlist;
    EXPECT_EQ(pl.kind, hls::Kind::media);
    EXPECT_TRUE(pl.endlist);
    EXPECT_FALSE(pl.is_live());
    EXPECT_EQ(pl.playlist_type, "VOD");
    ASSERT_EQ(pl.segments.size(), 3u);
    EXPECT_EQ(pl.segments[0].uri, "https://cdn.example/x/url_0/url_462/seg0.ts");
    EXPECT_EQ(pl.segments[2].uri, "https://cdn.example/x/other/seg2.ts");
    EXPECT_EQ(pl.segments[0].sequence, 7u);
    EXPECT_EQ(pl.segments[2].sequence, 9u);
    EXPECT_TRUE(pl.segments[2].discontinuity);
    EXPECT_DOUBLE_EQ(pl.segments[2].start, 20.0);
    EXPECT_DOUBLE_EQ(pl.total_duration, 24.5);
    EXPECT_EQ(hls::segment_at(pl, 0.0), 0u);
    EXPECT_EQ(hls::segment_at(pl, 9.99), 0u);
    EXPECT_EQ(hls::segment_at(pl, 10.0), 1u);
    EXPECT_EQ(hls::segment_at(pl, 23.0), 2u);
    EXPECT_EQ(hls::segment_at(pl, 1000.0), 2u);
}

TEST(Hls, LivePlaylistHasNoEndlist)
{
    const auto r = hls::parse("#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-MEDIA-SEQUENCE:1000\n"
                              "#EXTINF:6,\nlive1000.ts\n#EXTINF:6,\nlive1001.ts\n",
                              "https://live.example/ch/index.m3u8");
    ASSERT_TRUE(r.ok);
    EXPECT_TRUE(r.playlist.is_live());
    EXPECT_EQ(r.playlist.segments.back().sequence, 1001u);
}

TEST(Hls, ReportsUnsupportedFeaturesHonestly)
{
    const auto aes =
        hls::parse("#EXTM3U\n#EXT-X-TARGETDURATION:6\n"
                   "#EXT-X-KEY:METHOD=AES-128,URI=\"key.bin\"\n#EXTINF:6,\na.ts\n#EXT-X-ENDLIST\n",
                   "https://a.example/i.m3u8");
    ASSERT_TRUE(aes.ok);
    EXPECT_NE(aes.playlist.unsupported.find("encrypted"), std::string::npos);
    const auto drm =
        hls::parse("#EXTM3U\n#EXT-X-TARGETDURATION:6\n"
                   "#EXT-X-KEY:METHOD=SAMPLE-AES,KEYFORMAT=\"com.apple.streamingkeydelivery\"\n"
                   "#EXTINF:6,\na.ts\n#EXT-X-ENDLIST\n",
                   "https://a.example/i.m3u8");
    EXPECT_NE(drm.playlist.unsupported.find("DRM"), std::string::npos);
    const auto fmp4 = hls::parse("#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-MAP:URI=\"init.mp4\"\n"
                                 "#EXTINF:6,\na.m4s\n#EXT-X-ENDLIST\n",
                                 "https://a.example/i.m3u8");
    EXPECT_NE(fmp4.playlist.unsupported.find("MP4"), std::string::npos);
    const auto clear = hls::parse("#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-KEY:METHOD=NONE\n"
                                  "#EXTINF:6,\na.ts\n#EXT-X-ENDLIST\n",
                                  "https://a.example/i.m3u8");
    EXPECT_TRUE(clear.playlist.unsupported.empty());
}

TEST(Hls, RejectsNonPlaylistsAndBadInput)
{
    EXPECT_FALSE(hls::parse("<html>Not found</html>", kBase).ok);
    EXPECT_FALSE(hls::parse("", kBase).ok);
    EXPECT_FALSE(hls::parse("#EXTM3U\n", kBase).ok);
    EXPECT_FALSE(hls::parse("#EXTM3U\nsegment.ts\n", kBase).ok);
    EXPECT_FALSE(
        hls::parse("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nv.m3u8\n#EXTINF:5,\ns.ts\n", kBase).ok);
    hls::Limits limits;
    limits.max_segments = 2;
    EXPECT_FALSE(
        hls::parse("#EXTM3U\n#EXTINF:1,\na.ts\n#EXTINF:1,\nb.ts\n#EXTINF:1,\nc.ts\n", kBase, limits)
            .ok);
}

TEST(Hls, PrefersMuxedAudioAndSkipsUnsupportedCodecs)
{
    const char text[] = R"(#EXTM3U
#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="aud",NAME="English",LANGUAGE="en",DEFAULT=YES,URI="audio/en.m3u8"
#EXT-X-STREAM-INF:BANDWIDTH=9000000,CODECS="av01.0.08M.08,mp4a.40.2",RESOLUTION=1920x1080
av1.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=5000000,CODECS="avc1.640028",AUDIO="aud",RESOLUTION=1920x1080
separate.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=3000000,CODECS="avc1.64001f,mp4a.40.2",RESOLUTION=1280x720
muxed.m3u8
)";
    const auto r = hls::parse(text, "https://s.example/master.m3u8");
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(r.playlist.variants.size(), 3u);
    EXPECT_FALSE(r.playlist.variants[0].video_codec_supported);
    ASSERT_EQ(r.playlist.renditions.size(), 1u);
    EXPECT_EQ(r.playlist.renditions[0].uri, "https://s.example/audio/en.m3u8");
    std::string why;
    EXPECT_EQ(hls::select_variant(r.playlist, {}, &why), 2);
    EXPECT_TRUE(why.empty());
}

TEST(Hls, AttributeListsWithQuotedCommas)
{
    const auto attrs =
        hls::parse_attributes(R"(BANDWIDTH=10,CODECS="a,b,c",NAME="x y", AUDIO="g")");
    ASSERT_EQ(attrs.size(), 4u);
    EXPECT_EQ(attrs[1].first, "CODECS");
    EXPECT_EQ(attrs[1].second, "a,b,c");
    EXPECT_EQ(attrs[2].second, "x y");
    EXPECT_EQ(attrs[3].second, "g");
}
