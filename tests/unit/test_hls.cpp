// AKENO STREAM PS5 - HLS parser and variant selection tests.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/hls.hpp"
#include "media/hls_source.hpp"

#include <gtest/gtest.h>
#include <openssl/evp.h>

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
    const auto drm =
        hls::parse("#EXTM3U\n#EXT-X-TARGETDURATION:6\n"
                   "#EXT-X-KEY:METHOD=SAMPLE-AES,KEYFORMAT=\"com.apple.streamingkeydelivery\"\n"
                   "#EXTINF:6,\na.ts\n#EXT-X-ENDLIST\n",
                   "https://a.example/i.m3u8");
    EXPECT_NE(drm.playlist.unsupported.find("DRM"), std::string::npos);
    const auto widevine =
        hls::parse("#EXTM3U\n#EXT-X-TARGETDURATION:6\n"
                   "#EXT-X-KEY:METHOD=AES-128,URI=\"k\",KEYFORMAT=\"urn:uuid:edef8ba9\"\n"
                   "#EXTINF:6,\na.ts\n#EXT-X-ENDLIST\n",
                   "https://a.example/i.m3u8");
    EXPECT_NE(widevine.playlist.unsupported.find("DRM"), std::string::npos);
    const auto clear = hls::parse("#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-KEY:METHOD=NONE\n"
                                  "#EXTINF:6,\na.ts\n#EXT-X-ENDLIST\n",
                                  "https://a.example/i.m3u8");
    EXPECT_TRUE(clear.playlist.unsupported.empty());
    EXPECT_TRUE(clear.playlist.keys.empty());
    EXPECT_EQ(clear.playlist.segments[0].key, -1);
}

TEST(Hls, ParsesStandardAes128Keys)
{
    const auto r = hls::parse(
        "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-MEDIA-SEQUENCE:7\n"
        "#EXT-X-KEY:METHOD=AES-128,URI=\"keys/k1.bin\"\n#EXTINF:6,\na.ts\n"
        "#EXT-X-KEY:METHOD=AES-128,URI=\"k2.bin\",IV=0x000102030405060708090A0B0C0D0E0F\n"
        "#EXTINF:6,\nb.ts\n#EXT-X-KEY:METHOD=NONE\n#EXTINF:6,\nc.ts\n"
        "#EXT-X-ENDLIST\n",
        "https://a.example/v/i.m3u8");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.playlist.unsupported.empty()) << r.playlist.unsupported;
    ASSERT_EQ(r.playlist.keys.size(), 2u);
    EXPECT_EQ(r.playlist.keys[0].uri, "https://a.example/v/keys/k1.bin");
    EXPECT_FALSE(r.playlist.keys[0].has_iv);
    EXPECT_TRUE(r.playlist.keys[1].has_iv);
    EXPECT_EQ(r.playlist.keys[1].iv[0], 0x00);
    EXPECT_EQ(r.playlist.keys[1].iv[15], 0x0f);
    ASSERT_EQ(r.playlist.segments.size(), 3u);
    EXPECT_EQ(r.playlist.segments[0].key, 0);
    EXPECT_EQ(r.playlist.segments[1].key, 1);
    EXPECT_EQ(r.playlist.segments[2].key, -1);
    EXPECT_EQ(r.playlist.segments[0].sequence, 7u);
}

TEST(Hls, ParsesFragmentedMp4AndByteRanges)
{
    const auto r =
        hls::parse("#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:4\n"
                   "#EXT-X-MAP:URI=\"media.mp4\",BYTERANGE=\"720@0\"\n"
                   "#EXTINF:4,\n#EXT-X-BYTERANGE:1000@720\nmedia.mp4\n"
                   "#EXTINF:4,\n#EXT-X-BYTERANGE:2000\nmedia.mp4\n"
                   "#EXT-X-MAP:URI=\"init2.mp4\"\n#EXTINF:4,\nseg3.m4s\n#EXT-X-ENDLIST\n",
                   "https://a.example/v/i.m3u8");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.playlist.unsupported.empty()) << r.playlist.unsupported;
    EXPECT_TRUE(r.playlist.fragmented_mp4());
    ASSERT_EQ(r.playlist.maps.size(), 2u);
    EXPECT_EQ(r.playlist.maps[0].uri, "https://a.example/v/media.mp4");
    EXPECT_EQ(r.playlist.maps[0].offset, 0);
    EXPECT_EQ(r.playlist.maps[0].length, 720);
    EXPECT_EQ(r.playlist.maps[1].offset, -1);
    ASSERT_EQ(r.playlist.segments.size(), 3u);
    EXPECT_EQ(r.playlist.segments[0].map, 0);
    EXPECT_EQ(r.playlist.segments[0].offset, 720);
    EXPECT_EQ(r.playlist.segments[0].length, 1000);
    // Without @offset a range continues where the previous one ended.
    EXPECT_EQ(r.playlist.segments[1].offset, 1720);
    EXPECT_EQ(r.playlist.segments[1].length, 2000);
    EXPECT_EQ(r.playlist.segments[2].map, 1);
    EXPECT_EQ(r.playlist.segments[2].offset, -1);
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

TEST(Hls, UsesSeparateAudioRenditionsAndSkipsUnsupportedCodecs)
{
    const char text[] = R"(#EXTM3U
#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="aud",NAME="Deutsch",LANGUAGE="de",AUTOSELECT=YES,URI="audio/de.m3u8"
#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="aud",NAME="English",LANGUAGE="en",DEFAULT=YES,URI="audio/en.m3u8"
#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="muxed",NAME="Main",DEFAULT=YES
#EXT-X-STREAM-INF:BANDWIDTH=9000000,CODECS="av01.0.08M.08,mp4a.40.2",RESOLUTION=1920x1080
av1.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=5000000,CODECS="avc1.640028,mp4a.40.2",AUDIO="aud",RESOLUTION=1920x1080
separate.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=3000000,CODECS="avc1.64001f,mp4a.40.2",AUDIO="muxed",RESOLUTION=1280x720
muxed.m3u8
)";
    const auto r = hls::parse(text, "https://s.example/master.m3u8");
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(r.playlist.variants.size(), 3u);
    EXPECT_FALSE(r.playlist.variants[0].video_codec_supported);
    ASSERT_EQ(r.playlist.renditions.size(), 3u);
    std::string why;
    EXPECT_EQ(hls::select_variant(r.playlist, {}, &why), 1);
    EXPECT_TRUE(why.empty()) << why;
    // DEFAULT wins over AUTOSELECT.
    const hls::Rendition *audio = hls::audio_rendition(r.playlist, r.playlist.variants[1]);
    ASSERT_NE(audio, nullptr);
    EXPECT_EQ(audio->uri, "https://s.example/audio/en.m3u8");
    // A group member without URI means the variant carries the audio.
    EXPECT_EQ(hls::audio_rendition(r.playlist, r.playlist.variants[2]), nullptr);
    EXPECT_EQ(hls::audio_rendition(r.playlist, r.playlist.variants[0]), nullptr);
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

TEST(Hls, DecryptsAes128CbcSegments)
{
    const std::uint8_t key[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                  0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    std::uint8_t iv[16] = {};
    iv[15] = 9;
    const std::string plain = std::string(188 * 3, '\x47') + "tail";
    // Encrypt with OpenSSL directly (PKCS#7 padding, as HLS packagers do).
    std::string cipher(plain.size() + 16, '\0');
    EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
    int written = 0, final_bytes = 0;
    ASSERT_EQ(EVP_EncryptInit_ex(context, EVP_aes_128_cbc(), nullptr, key, iv), 1);
    ASSERT_EQ(EVP_EncryptUpdate(context, reinterpret_cast<unsigned char *>(cipher.data()), &written,
                                reinterpret_cast<const unsigned char *>(plain.data()),
                                static_cast<int>(plain.size())),
              1);
    ASSERT_EQ(EVP_EncryptFinal_ex(context,
                                  reinterpret_cast<unsigned char *>(cipher.data()) + written,
                                  &final_bytes),
              1);
    EVP_CIPHER_CTX_free(context);
    cipher.resize(static_cast<std::size_t>(written + final_bytes));
    ASSERT_EQ(cipher.size() % 16, 0u);

    std::string data = cipher;
    ASSERT_TRUE(media::aes128_cbc_decrypt(key, iv, &data));
    EXPECT_EQ(data, plain);
    std::string truncated = cipher.substr(0, cipher.size() - 3);
    EXPECT_FALSE(media::aes128_cbc_decrypt(key, iv, &truncated));
    std::string empty;
    EXPECT_FALSE(media::aes128_cbc_decrypt(key, iv, &empty));
}
