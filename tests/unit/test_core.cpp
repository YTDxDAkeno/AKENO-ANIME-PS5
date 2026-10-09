// AKENO STREAM PS5 - JSON and URL unit tests.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/json.hpp"
#include "core/url.hpp"

#include <gtest/gtest.h>

using namespace akeno;

TEST(Json, ParsesNestedDocument)
{
    const auto r = json::parse(R"({"data":{"Page":{"media":[{"id":21,"title":{"romaji":"One Piece"},
        "score":8.7,"adult":false,"genres":["Action","Adventure"],"next":null}]}}})");
    ASSERT_TRUE(r.ok) << r.error;
    const auto &media = r.value.at_path("data.Page.media");
    ASSERT_TRUE(media.is_array());
    ASSERT_EQ(media.size(), 1u);
    EXPECT_EQ(media[0]["id"].integer(), 21);
    EXPECT_EQ(media[0]["title"]["romaji"].str(), "One Piece");
    EXPECT_DOUBLE_EQ(media[0]["score"].num(), 8.7);
    EXPECT_FALSE(media[0]["adult"].boolean(true));
    EXPECT_TRUE(media[0]["next"].is_null());
    EXPECT_EQ(media[0]["genres"][1].str(), "Adventure");
    // Missing members are null, never a crash.
    EXPECT_TRUE(media[0]["missing"]["deeper"].is_null());
    EXPECT_EQ(media[5]["id"].integer(-1), -1);
}

TEST(Json, DecodesUnicodeEscapesAndSurrogates)
{
    const auto r = json::parse(R"(["éあ", "🌸", "\ud800x", "a\/b\n"])");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.value[0].str(), "\xC3\xA9\xE3\x81\x82");
    EXPECT_EQ(r.value[1].str(), "\xF0\x9F\x8C\xB8");
    EXPECT_EQ(r.value[2].str(), "\xEF\xBF\xBDx"); // lone surrogate -> U+FFFD
    EXPECT_EQ(r.value[3].str(), "a/b\n");
}

TEST(Json, RejectsMalformedAndBoundsDepth)
{
    EXPECT_FALSE(json::parse("{").ok);
    EXPECT_FALSE(json::parse("[1,]").ok);
    EXPECT_FALSE(json::parse("{\"a\" 1}").ok);
    EXPECT_FALSE(json::parse("01").ok);
    EXPECT_FALSE(json::parse("\"unterminated").ok);
    EXPECT_FALSE(json::parse("[1] x").ok);
    EXPECT_FALSE(json::parse("\"tab\there\"").ok);
    std::string deep(200, '[');
    deep += std::string(200, ']');
    const auto r = json::parse(deep);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.error, "nesting too deep");
    json::ParseLimits small;
    small.max_bytes = 4;
    EXPECT_FALSE(json::parse("[1,2,3]", small).ok);
}

TEST(Json, RoundTripsAndEscapes)
{
    json::Value doc = json::Value::object();
    doc.set("version", 2);
    doc.set("name", "Akeno \"Stream\"\n");
    json::Value list = json::Value::array();
    list.push(1.5);
    list.push(true);
    list.push(nullptr);
    doc.set("list", list);
    const std::string text = doc.dump();
    EXPECT_EQ(text, R"({"list":[1.5,true,null],"name":"Akeno \"Stream\"\n","version":2})");
    const auto back = json::parse(doc.dump(true));
    ASSERT_TRUE(back.ok);
    EXPECT_EQ(back.value["name"].str(), "Akeno \"Stream\"\n");
    EXPECT_EQ(back.value["version"].integer(), 2);
}

TEST(Json, CopiesDoNotAlias)
{
    json::Value a = json::Value::object();
    a.set("k", 1);
    json::Value b = a;
    b.set("k", 2);
    EXPECT_EQ(a["k"].integer(), 1);
    EXPECT_EQ(b["k"].integer(), 2);
}

TEST(Url, ParsesAndNormalises)
{
    const auto u = url::parse("HTTPS://Example.COM:8443/a/b.m3u8?x=1#frag");
    ASSERT_TRUE(u);
    EXPECT_EQ(u->scheme, "https");
    EXPECT_EQ(u->host, "example.com");
    EXPECT_EQ(u->port, 8443);
    EXPECT_EQ(u->path, "/a/b.m3u8");
    EXPECT_EQ(u->query, "x=1");
    EXPECT_EQ(u->str(), "https://example.com:8443/a/b.m3u8?x=1");
    EXPECT_FALSE(url::parse("ftp://example.com/x"));
    EXPECT_FALSE(url::parse("https://user:pass@example.com/"));
    EXPECT_FALSE(url::parse("https://example.com:99999/"));
    EXPECT_FALSE(url::parse("https://exa mple.com/"));
    EXPECT_FALSE(url::parse("example.com/no-scheme"));
}

TEST(Url, ResolvesRfc3986Examples)
{
    const std::string base = "http://a/b/c/d;p?q";
    const std::pair<const char *, const char *> cases[] = {
        {"g", "http://a/b/c/g"},      {"./g", "http://a/b/c/g"},
        {"g/", "http://a/b/c/g/"},    {"/g", "http://a/g"},
        {"//g", "http://g/"},         {"?y", "http://a/b/c/d;p?y"},
        {"g?y", "http://a/b/c/g?y"},  {"", "http://a/b/c/d;p?q"},
        {".", "http://a/b/c/"},       {"./", "http://a/b/c/"},
        {"..", "http://a/b/"},        {"../", "http://a/b/"},
        {"../g", "http://a/b/g"},     {"../..", "http://a/"},
        {"../../g", "http://a/g"},    {"../../../g", "http://a/g"},
        {"/./g", "http://a/g"},       {"/../g", "http://a/g"},
        {"g.", "http://a/b/c/g."},    {"..g", "http://a/b/c/..g"},
        {"./../g", "http://a/b/g"},   {"g/./h", "http://a/b/c/g/h"},
        {"g/../h", "http://a/b/c/h"}, {"https://other/x", "https://other/x"},
    };
    for (const auto &[ref, expected] : cases)
    {
        const auto r = url::resolve(base, ref);
        ASSERT_TRUE(r) << ref;
        EXPECT_EQ(*r, expected) << "reference: " << ref;
    }
    EXPECT_FALSE(url::resolve(base, "has space"));
    EXPECT_FALSE(url::resolve("not a url", "g"));
}

TEST(Url, HlsStyleRelativeSegments)
{
    const auto v = url::resolve("https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8",
                                "url_0/193039199_mp4_h264_aac_hd_7.m3u8");
    ASSERT_TRUE(v);
    EXPECT_EQ(*v, "https://test-streams.mux.dev/x36xhzz/url_0/193039199_mp4_h264_aac_hd_7.m3u8");
    const auto s = url::resolve(*v, "url_462/193039199_mp4_h264_aac_hd_7.ts");
    ASSERT_TRUE(s);
    EXPECT_EQ(*s,
              "https://test-streams.mux.dev/x36xhzz/url_0/url_462/193039199_mp4_h264_aac_hd_7.ts");
    // Parent references that the v0.3 resolver rejected outright are valid HLS.
    const auto up = url::resolve("https://cdn.example/hls/v720/index.m3u8", "../seg/0001.ts");
    ASSERT_TRUE(up);
    EXPECT_EQ(*up, "https://cdn.example/hls/seg/0001.ts");
}

TEST(Url, EncodesAndRedacts)
{
    EXPECT_EQ(url::encode_component("Attack on Titan & co/?"),
              "Attack%20on%20Titan%20%26%20co%2F%3F");
    EXPECT_EQ(url::decode_component("a%20b+c%2F"), "a b c/");
    EXPECT_EQ(url::build_query({{"part", "snippet"}, {"q", "lo-fi"}}), "part=snippet&q=lo-fi");
    EXPECT_EQ(
        url::redact("https://www.googleapis.com/youtube/v3/search?part=snippet&key=SECRET123&q=x"),
        "https://www.googleapis.com/youtube/v3/search?part=snippet&key=REDACTED&q=x");
    EXPECT_EQ(url::redact("https://a/b?access_token=t&x=1"),
              "https://a/b?access_token=REDACTED&x=1");
    EXPECT_EQ(url::redact("https://a/b"), "https://a/b");
}
