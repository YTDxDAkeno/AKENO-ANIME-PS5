// AKENO STREAM PS5 - Websites, the embedded browser and AKENO STREAM's own pages.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for everything above the console's browser: address handling,
// saved websites, the site check, the loopback page server (token, Host
// check, events, byte ranges), the test-result log, icon decoding, and whole
// browser sessions driven through the real App with the scripted stand-in
// for the system browser (tests/host/host_web_view.hpp).
#include "app/app.hpp"
#include "core/fs.hpp"
#include "gfx/image_decode.hpp"
#include "host_web_view.hpp"
#include "net/http.hpp"
#include "platform/platform.hpp"
#include "web/address.hpp"
#include "web/local_pages.hpp"
#include "web/site_probe.hpp"
#include "web/web_tests.hpp"
#include "web/websites.hpp"

#include "stb/stb_image_write.h"

// Defined with the rest of stb_image_write in tests/host/canned_api.cpp; the
// header declares it only in its implementation part.
extern "C" unsigned char *stbi_write_png_to_mem(const unsigned char *pixels, int stride_bytes,
                                                int x, int y, int n, int *out_len);

#include <gtest/gtest.h>

#include <cstdlib>
#include <functional>
#include <string>
#include <unistd.h>
#include <vector>

using namespace akeno;

namespace
{
std::string fresh_dir(const char *name)
{
    char path[128];
    std::snprintf(path, sizeof(path), "/tmp/akeno-web-%s-XXXXXX", name);
    if (!mkdtemp(path))
        return "/tmp";
    return path;
}

net::Response request(const std::string &url, const std::string &method = "GET",
                      const std::string &body = {}, std::vector<std::string> headers = {})
{
    net::Client client;
    net::Request r;
    r.url = url;
    r.method = method;
    r.body = body;
    r.headers = std::move(headers);
    r.total_timeout_ms = 5000;
    return client.perform(r);
}

net::Response html(const std::string &body)
{
    net::Response r;
    r.outcome = net::Outcome::ok;
    r.status = 200;
    r.body = body;
    r.content_type = "text/html";
    return r;
}
} // namespace

// ---------------------------------------------------------------------------
TEST(WebAddress, TurnsTypedTextIntoAddressesOrSearches)
{
    using web::SearchEngine;
    auto d = web::interpret("youtube.com", SearchEngine::duckduckgo);
    ASSERT_TRUE(d.ok) << d.error;
    EXPECT_FALSE(d.is_search);
    EXPECT_EQ(d.url, "https://youtube.com/");

    // No list of allowed sites: any well-formed host is an address.
    for (const char *site :
         {"crunchyroll.com", "hentaiheaven.com", "anikototv.to", "media.example.org/path?x=1"})
    {
        d = web::interpret(site, SearchEngine::duckduckgo);
        EXPECT_TRUE(d.ok && !d.is_search) << site << ": " << d.error;
        EXPECT_TRUE(d.url.starts_with("https://")) << d.url;
    }

    d = web::interpret("  HTTPS://Example.COM/Watch?v=1#t=2  ", SearchEngine::google);
    ASSERT_TRUE(d.ok);
    EXPECT_EQ(d.url, "https://example.com/Watch?v=1#t=2"); // path, query and fragment kept

    d = web::interpret("http://192.168.1.20:8096/web/", SearchEngine::duckduckgo);
    ASSERT_TRUE(d.ok);
    EXPECT_TRUE(d.insecure);
    EXPECT_EQ(d.url, "http://192.168.1.20:8096/web/");

    d = web::interpret("example.com:8443/app", SearchEngine::duckduckgo);
    ASSERT_TRUE(d.ok);
    EXPECT_EQ(d.url, "https://example.com:8443/app");

    d = web::interpret("frieren episode 1", SearchEngine::duckduckgo);
    ASSERT_TRUE(d.ok);
    EXPECT_TRUE(d.is_search);
    EXPECT_EQ(d.url, "https://duckduckgo.com/?q=frieren%20episode%201");
    d = web::interpret("weather", SearchEngine::google);
    EXPECT_EQ(d.url, "https://www.google.com/search?q=weather");
    d = web::interpret("v1.2", SearchEngine::bing); // numeric "TLD": not a host
    EXPECT_TRUE(d.is_search);
}

TEST(WebAddress, RefusesWhatMustNotLeaveTheApp)
{
    using web::SearchEngine;
    for (const char *bad :
         {"javascript:alert(1)", "JaVaScRiPt:alert(1)", "data:text/html,<b>x", "file:///etc/passwd",
          "about:blank", "ftp://example.com/", "psno://store", "https:example.com"})
    {
        const auto d = web::interpret(bad, SearchEngine::duckduckgo);
        EXPECT_FALSE(d.ok) << bad;
        EXPECT_FALSE(d.error.empty()) << bad;
    }
    // Credentials in addresses and the console's own loopback addresses.
    for (const char *bad : {"https://user:pass@example.com/", "user@example.com",
                            "http://localhost:8095/s/x/youtube", "127.0.0.1", "127.12.0.1:80",
                            "https://[::1]/", "http://0.0.0.0/", "https://a.localhost/"})
        EXPECT_FALSE(web::interpret(bad, SearchEngine::duckduckgo).ok) << bad;
    EXPECT_FALSE(web::interpret("", SearchEngine::duckduckgo).ok);
    EXPECT_FALSE(
        web::interpret("https://example.com/" + std::string(3000, 'a'), SearchEngine::duckduckgo)
            .ok);
    EXPECT_FALSE(web::interpret("https://exa mple.com/", SearchEngine::duckduckgo).ok);
    EXPECT_FALSE(web::interpret("https://example.com/<script>", SearchEngine::duckduckgo).ok);
    // Stored addresses never turn into searches.
    EXPECT_FALSE(web::check_address("not an address").ok);
    EXPECT_TRUE(web::check_address("crunchyroll.com").ok);
}

TEST(WebAddress, LabelsAndOrigins)
{
    EXPECT_EQ(web::display_host("https://www.youtube.com/watch?v=1"), "youtube.com");
    EXPECT_EQ(web::display_host("https://m.crunchyroll.com/"), "crunchyroll.com");
    EXPECT_EQ(web::display_host("http://192.168.1.2:8096/"), "192.168.1.2:8096");
    EXPECT_EQ(web::origin_of("https://Example.com:8443/a/b?c"), "https://example.com:8443");
    EXPECT_EQ(web::engine_from_id("google"), web::SearchEngine::google);
    EXPECT_EQ(web::engine_from_id("nonsense"), web::SearchEngine::duckduckgo);
}

TEST(WebAddress, ReadsYouTubeLinks)
{
    auto t = web::parse_youtube("https://www.youtube.com/watch?v=dQw4w9WgXcQ&t=1m30s");
    ASSERT_TRUE(t);
    EXPECT_EQ(t->video_id, "dQw4w9WgXcQ");
    EXPECT_EQ(t->start_seconds, 90);
    t = web::parse_youtube("youtu.be/aqz-KE-bpKQ?t=42");
    ASSERT_TRUE(t);
    EXPECT_EQ(t->video_id, "aqz-KE-bpKQ");
    EXPECT_EQ(t->start_seconds, 42);
    t = web::parse_youtube("m.youtube.com/shorts/aqz-KE-bpKQ");
    ASSERT_TRUE(t);
    EXPECT_EQ(t->video_id, "aqz-KE-bpKQ");
    t = web::parse_youtube("https://www.youtube-nocookie.com/embed/aqz-KE-bpKQ?start=5");
    ASSERT_TRUE(t);
    EXPECT_EQ(t->start_seconds, 5);
    t = web::parse_youtube(
        "https://www.youtube.com/playlist?list=PLx0sYbCqOb8TBPRdmBHs5Iftvv9TPboYG");
    ASSERT_TRUE(t);
    EXPECT_TRUE(t->video_id.empty());
    EXPECT_EQ(t->list_id, "PLx0sYbCqOb8TBPRdmBHs5Iftvv9TPboYG");
    t = web::parse_youtube("https://music.youtube.com/watch?v=aqz-KE-bpKQ&list=RDaqz");
    ASSERT_TRUE(t);
    EXPECT_EQ(t->list_id, "RDaqz");
    t = web::parse_youtube("aqz-KE-bpKQ");
    ASSERT_TRUE(t);
    EXPECT_EQ(t->video_id, "aqz-KE-bpKQ");
    EXPECT_EQ(web::parse_start_time("1h2m3s"), 3723);
    EXPECT_EQ(web::parse_start_time("abc"), 0);

    for (const char *bad : {"https://example.com/watch?v=aqz-KE-bpKQ", "https://youtube.com/",
                            "https://www.youtube.com/watch?v=short", "youtube.com/watch?v=<x>",
                            "https://youtube.com/channel/UC123", "hello world"})
        EXPECT_FALSE(web::parse_youtube(bad)) << bad;
}

// ---------------------------------------------------------------------------
TEST(WebsiteStore, AddsEditsRemovesAndPersists)
{
    const std::string dir = fresh_dir("store");
    {
        web::WebsiteStore store(dir);
        store.load();
        std::string why, id;
        ASSERT_TRUE(store.add("Crunchyroll", "crunchyroll.com", &why, &id)) << why;
        EXPECT_EQ(store.find(id)->url, "https://crunchyroll.com/");
        EXPECT_FALSE(store.add("Again", "https://crunchyroll.com/", &why));
        EXPECT_NE(why.find("already"), std::string::npos);
        EXPECT_FALSE(store.add("Bad", "javascript:alert(1)", &why));
        std::string other;
        ASSERT_TRUE(store.add("", "https://www.youtube.com/", &why, &other));
        EXPECT_EQ(store.find(other)->name, "youtube.com"); // named after the host

        web::Website site = *store.find(id);
        site.name = "  Crunchy\nroll  " + std::string(80, 'x');
        site.pinned = true;
        site.checks.loads = web::Mark::works;
        site.checks.video = web::Mark::fails;
        site.icon_url = "https://crunchyroll.com/favicon.ico";
        ASSERT_TRUE(store.update(site));
        EXPECT_EQ(store.find(id)->name.size(), web::WebsiteStore::kMaxName);
        EXPECT_EQ(store.find(id)->name.find('\n'), std::string::npos);
        // Another entry's address is refused; a new address drops the old icon.
        site.url = "https://www.youtube.com/";
        EXPECT_FALSE(store.update(site, &why));
        site = *store.find(id);
        site.url = "https://beta.crunchyroll.com/";
        ASSERT_TRUE(store.update(site));
        EXPECT_TRUE(store.find(id)->icon_url.empty());
        store.set_icon(id, "javascript:alert(1)");
        EXPECT_TRUE(store.find(id)->icon_url.empty());
        store.set_icon(id, "https://beta.crunchyroll.com/icon.png");
    }
    web::WebsiteStore reloaded(dir);
    reloaded.load();
    ASSERT_EQ(reloaded.sites().size(), 2u);
    const web::Website w = reloaded.sites()[0]; // a copy: remove() changes the list
    EXPECT_EQ(w.url, "https://beta.crunchyroll.com/");
    EXPECT_TRUE(w.pinned);
    EXPECT_EQ(w.icon_url, "https://beta.crunchyroll.com/icon.png");
    EXPECT_EQ(w.checks.loads, web::Mark::works);
    EXPECT_EQ(w.checks.video, web::Mark::fails);
    EXPECT_EQ(w.checks.sound, web::Mark::untested);
    EXPECT_TRUE(reloaded.remove(w.id));
    EXPECT_FALSE(reloaded.remove(w.id));
    EXPECT_EQ(reloaded.sites().size(), 1u);
    // A new entry never reuses an id.
    std::string why, id;
    ASSERT_TRUE(reloaded.add("New", "example.org", &why, &id));
    EXPECT_NE(id, w.id);
}

TEST(WebsiteStore, RecentlyVisitedRespectsPrivateSites)
{
    web::WebsiteStore store(fresh_dir("recent"));
    store.load();
    std::string why, secret;
    ASSERT_TRUE(store.add("Secret", "secret.example", &why, &secret));
    web::Website s = *store.find(secret);
    s.private_site = true;
    store.update(s);

    store.record_visit("https://a.example/", "A", 100);
    store.record_visit("https://b.example/", "B", 200);
    store.record_visit("https://a.example/", "A again", 300);
    store.record_visit("https://secret.example/", "Secret", 400);
    ASSERT_EQ(store.recent().size(), 2u);
    EXPECT_EQ(store.recent()[0].url, "https://a.example/"); // newest first, no duplicates
    EXPECT_EQ(store.recent()[0].title, "A again");
    EXPECT_EQ(store.find(secret)->visits, 1);
    EXPECT_EQ(store.find(secret)->last_visit, 400u);
    for (int i = 0; i < 40; ++i)
        store.record_visit("https://s" + std::to_string(i) + ".example/", "", 500 + i);
    EXPECT_EQ(store.recent().size(), web::WebsiteStore::kMaxRecent);
    store.remove_recent(store.recent()[0].url);
    EXPECT_EQ(store.recent().size(), web::WebsiteStore::kMaxRecent - 1);
    store.clear_recent();
    EXPECT_TRUE(store.recent().empty());
}

TEST(WebsiteStore, ImportsWebsitesTxtOnceAndSurvivesDamage)
{
    const std::string dir = fresh_dir("import");
    web::WebsiteStore store(dir);
    store.load();
    const std::string text = "\xEF\xBB\xBF# My sites\n"
                             "Anime = https://anikototv.to/\n"
                             "crunchyroll.com\n"
                             "Broken = javascript:alert(1)\n"
                             "A = B = https://example.com/a=b\n";
    std::string problem;
    EXPECT_EQ(store.import_text(text, &problem), 3);
    EXPECT_NE(problem.find("line 4"), std::string::npos);
    ASSERT_EQ(store.sites().size(), 3u);
    EXPECT_EQ(store.sites()[0].name, "Anime");
    EXPECT_EQ(store.sites()[1].name, "crunchyroll.com");
    EXPECT_EQ(store.sites()[2].name, "A = B");
    // Removed by the user: the same file does not bring it back.
    store.remove(store.sites()[0].id);
    EXPECT_EQ(store.import_text(text, nullptr), 0);
    EXPECT_EQ(store.sites().size(), 2u);
    EXPECT_NE(store.export_text().find("crunchyroll.com = https://crunchyroll.com/"),
              std::string::npos);

    ASSERT_TRUE(fs::write_atomic(fs::join(dir, "websites.json"), "{ broken"));
    web::WebsiteStore damaged(dir);
    damaged.load();
    EXPECT_TRUE(damaged.sites().empty());
    EXPECT_FALSE(damaged.last_error().empty());
    EXPECT_TRUE(fs::exists(fs::join(dir, "websites.json.corrupt")));
}

// ---------------------------------------------------------------------------
TEST(WebTests, KeepsResultsAndChecksReports)
{
    const std::string dir = fresh_dir("tests");
    {
        web::WebTestLog log(dir);
        log.load();
        log.set({"youtube.playing", "YouTube embedded player", "Video plays", web::Outcome::yes,
                 "started", 1, "YouTube player"});
        const std::string report =
            R"({"version":1,"results":[
            {"id":"codec.h264","group":"Video formats","name":"H.264","status":"yes","detail":"probably"},
            {"id":"drm.widevine","group":"DRM","name":"Widevine","status":"no","detail":"NotSupportedError"},
            {"id":"youtube.playing","group":"x","name":"forged","status":"no"},
            {"id":"bad id!","group":"x","name":"x","status":"yes"},
            {"id":"no.group","name":"x","status":"yes"},
            {"id":"long.text","group":"G","name":"N","status":"maybe","detail":")" +
            std::string(1000, 'z') + R"("}]})";
        std::string error;
        EXPECT_EQ(log.accept_report(report, 99, &error), 3) << error;
        EXPECT_EQ(log.get("codec.h264")->outcome, web::Outcome::yes);
        EXPECT_EQ(log.get("drm.widevine")->outcome, web::Outcome::no);
        EXPECT_EQ(log.get("long.text")->outcome, web::Outcome::unknown);
        EXPECT_LE(log.get("long.text")->detail.size(), 300u);
        // The page cannot overwrite the player's own results.
        EXPECT_EQ(log.get("youtube.playing")->outcome, web::Outcome::yes);
        EXPECT_EQ(log.get("bad id!"), nullptr);
        EXPECT_EQ(log.accept_report("not json", 1, &error), 0);
        EXPECT_FALSE(error.empty());
    }
    web::WebTestLog reloaded(dir);
    reloaded.load();
    EXPECT_EQ(reloaded.records().size(), 4u);
    EXPECT_EQ(reloaded.with_prefix("drm.").size(), 1u);
    reloaded.clear();
    EXPECT_TRUE(reloaded.records().empty());
}

TEST(WebTests, DrmVerdictSaysOnlyWhatWasMeasured)
{
    web::WebTestLog log(fresh_dir("verdict"));
    log.load();
    const auto level = [&] { return web::drm_verdict(log, "crunchyroll.playback").level; };
    const auto set = [&](const char *id, web::Outcome o)
    { log.set({id, "DRM", id, o, "", 1, "capability test"}); };
    using L = web::DrmVerdict::Level;
    EXPECT_EQ(level(), L::not_measured);
    // Rendering and sign-in are not playback.
    set("crunchyroll.render", web::Outcome::yes);
    set("crunchyroll.login", web::Outcome::yes);
    EXPECT_EQ(level(), L::not_measured);
    // One refused key system is not "none".
    set("drm.widevine", web::Outcome::no);
    EXPECT_EQ(level(), L::partly_measured);
    set("drm.playready", web::Outcome::no);
    EXPECT_EQ(level(), L::partly_measured);
    set("drm.fairplay", web::Outcome::no);
    EXPECT_EQ(level(), L::unavailable);
    set("drm.playready", web::Outcome::yes);
    EXPECT_EQ(level(), L::possible);
    set("crunchyroll.playback", web::Outcome::yes);
    EXPECT_EQ(level(), L::confirmed);
    // No EME at all (measured in a secure page) is "none" too.
    log.clear();
    set("drm.eme", web::Outcome::no);
    EXPECT_EQ(level(), L::unavailable);
    // Not a secure context: the page reports unknown, which stays unmeasured.
    log.clear();
    set("drm.eme", web::Outcome::unknown);
    set("drm.widevine", web::Outcome::unknown);
    EXPECT_EQ(level(), L::not_measured);
}

// ---------------------------------------------------------------------------
TEST(SiteProbe, FindsTitleAndIcon)
{
    const std::string page =
        "<html><head><TITLE> Crunchyroll &amp; Friends </TITLE>"
        "<link rel=\"icon\" href=\"/favicon-32.png\" sizes=\"32x32\">"
        "<link rel='mask-icon' href='/mask.svg'>"
        "<link rel=icon href=data:image/png;base64,AAAA>"
        "<link rel=\"apple-touch-icon\" sizes=\"180x180\" href=\"//static.example/touch.png\">"
        "<link rel=\"icon\" type=\"image/svg+xml\" href=\"/icon.svg\">"
        "</head></html>";
    EXPECT_EQ(web::find_title(page), "Crunchyroll & Friends");
    EXPECT_EQ(web::find_icon(page, "https://www.example.com/start"),
              "https://static.example/touch.png");
    EXPECT_EQ(web::find_icon("<link rel=\"shortcut icon\" href=\"img/f.ico\">",
                             "https://a.example/dir/page"),
              "https://a.example/dir/img/f.ico");
    EXPECT_EQ(web::find_icon("<p>no icons</p>", "https://a.example/x"),
              "https://a.example/favicon.ico");
    EXPECT_EQ(web::find_title("<title>unterminated"), "");
}

TEST(SiteProbe, ExplainsNetworkProblems)
{
    net::Response dns;
    dns.outcome = net::Outcome::network_error;
    dns.curl_code = 6;
    bool blocking = false;
    EXPECT_NE(web::describe_failure(dns, &blocking).find("DNS"), std::string::npos);
    EXPECT_TRUE(blocking);
    net::Response cert = dns;
    cert.curl_code = 60;
    EXPECT_NE(web::describe_failure(cert, &blocking).find("certificate"), std::string::npos);
    net::Response forbidden;
    forbidden.outcome = net::Outcome::http_error;
    forbidden.status = 403;
    EXPECT_NE(web::describe_failure(forbidden, &blocking).find("403"), std::string::npos);
    EXPECT_FALSE(blocking); // the browser may still open it

    net::set_test_transport(
        [](const net::Request &r)
        {
            if (r.url.find("down.example") != std::string::npos)
            {
                net::Response e;
                e.outcome = net::Outcome::network_error;
                e.curl_code = 7;
                return e;
            }
            return html("<title>Home</title><link rel=icon href=/i.png>");
        });
    const web::SiteProbe up = web::probe_site("https://up.example/", net::make_cancel_flag());
    EXPECT_TRUE(up.reached);
    EXPECT_FALSE(up.blocking);
    EXPECT_EQ(up.title, "Home");
    EXPECT_EQ(up.icon_url, "https://up.example/i.png");
    const web::SiteProbe down = web::probe_site("https://down.example/", net::make_cancel_flag());
    EXPECT_FALSE(down.reached);
    EXPECT_TRUE(down.blocking);
    EXPECT_NE(down.problem.find("could not be reached"), std::string::npos);
    net::set_test_transport({});
}

// ---------------------------------------------------------------------------
TEST(SiteIcons, DecodesIcoFiles)
{
    // PNG inside an ICO.
    const std::uint8_t rgba[4 * 4] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 9, 9, 9, 0};
    int length = 0;
    unsigned char *png = stbi_write_png_to_mem(rgba, 2 * 4, 2, 2, 4, &length);
    ASSERT_NE(png, nullptr);
    std::vector<std::uint8_t> ico = {0, 0, 1, 0, 1, 0, 2, 2, 0, 0, 1, 0, 32, 0};
    const auto put32 = [&](std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            ico.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    };
    put32(static_cast<std::uint32_t>(length));
    put32(22);
    ico.insert(ico.end(), png, png + length);
    std::free(png);
    gfx::Image image = gfx::decode_image(ico.data(), ico.size());
    ASSERT_TRUE(image.valid());
    EXPECT_EQ(image.width, 2);

    // A classic 32-bit bitmap icon (height counts the mask too).
    std::vector<std::uint8_t> bmp = {0, 0, 1, 0, 1, 0, 2, 2, 0, 0, 1, 0, 32, 0};
    ico.swap(bmp);
    const std::uint32_t dib = 40 + 2 * 2 * 4 + 2 * 4; // header, pixels, AND mask rows
    put32(dib);
    put32(22);
    put32(40);
    put32(2);
    put32(4); // 2 rows of colour + 2 rows of mask
    ico.push_back(1);
    ico.push_back(0);
    ico.push_back(32);
    ico.push_back(0);
    for (int i = 0; i < 6; ++i)
        put32(0);
    for (int i = 0; i < 4; ++i)
        put32(0xFF336699u); // opaque pixels, BGRA
    for (int i = 0; i < 2; ++i)
        put32(0);
    image = gfx::decode_image(ico.data(), ico.size());
    ASSERT_TRUE(image.valid());
    EXPECT_EQ(image.width, 2);
    EXPECT_EQ(image.height, 2);

    const std::uint8_t garbage[] = {0, 0, 1, 0, 200, 0, 1, 2, 3, 4, 5, 6, 7, 8};
    EXPECT_FALSE(gfx::decode_image(garbage, sizeof(garbage)).valid());
}

// ---------------------------------------------------------------------------
TEST(LocalPages, ServesOnlyWithTheTokenAndTheRightHost)
{
    net::Client::global_init();
    net::set_test_transport({});
    const std::string clips = fs::join(platform::app_dir(), "assets/selftest");
    web::LocalPages pages;
    std::string error;
    ASSERT_TRUE(pages.start(clips, &error)) << error;
    const std::string base = "http://127.0.0.1:" + std::to_string(pages.port());
    const std::string page = pages.page_url("youtube", "v=aqz-KE-bpKQ");
    EXPECT_TRUE(page.starts_with(base + "/s/"));

    net::Response r = request(page);
    EXPECT_EQ(r.status, 200);
    EXPECT_NE(r.body.find("youtube.js"), std::string::npos);
    EXPECT_NE(r.body.find("strict-origin-when-cross-origin"), std::string::npos);
    EXPECT_FALSE(pages.page_loaded());
    r = request(pages.page_url("youtube.js"));
    EXPECT_EQ(r.status, 200);
    EXPECT_NE(r.body.find("https://www.youtube.com/iframe_api"), std::string::npos);
    EXPECT_TRUE(pages.page_loaded());

    // Wrong token, no token, other paths.
    EXPECT_EQ(request(base + "/s/00000000000000000000000000000000/youtube").status, 404);
    EXPECT_EQ(request(base + "/youtube").status, 404);
    EXPECT_EQ(request(base + "/").status, 404);
    // DNS rebinding: another host name pointing here is refused.
    EXPECT_EQ(
        request(page, "GET", {}, {"Host: evil.example:" + std::to_string(pages.port())}).status,
        403);

    // Events: known types only, size-limited, plain values.
    r = request(pages.page_url("event"), "POST",
                R"({"type":"error","value":150,"detail":"embedding\nnot allowed"})",
                {"Content-Type: application/json"});
    EXPECT_EQ(r.status, 204);
    EXPECT_EQ(request(pages.page_url("event"), "POST", R"({"type":"exec","value":"rm"})").status,
              400);
    EXPECT_EQ(request(pages.page_url("event"), "POST",
                      R"({"type":"log","detail":")" + std::string(5000, 'x') + R"("})")
                  .status,
              413);
    const auto events = pages.take_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, "error");
    EXPECT_EQ(events[0].value, "150");
    EXPECT_EQ(events[0].detail, "embedding not allowed");
    EXPECT_TRUE(pages.take_events().empty());

    EXPECT_EQ(request(pages.page_url("report"), "POST", R"({"version":1,"results":[]})").status,
              204);
    EXPECT_EQ(pages.take_reports().size(), 1u);
    EXPECT_TRUE(pages.take_reports().empty());
    // The lab reports after every step: the newest are kept, oldest first.
    for (int i = 0; i < 20; ++i)
        EXPECT_EQ(request(pages.page_url("report"), "POST",
                          R"({"version":2,"n":)" + std::to_string(i) + "}")
                      .status,
                  204);
    const auto reports = pages.take_reports();
    ASSERT_EQ(reports.size(), web::LocalPages::kMaxReports);
    EXPECT_NE(reports.back().find(R"("n":19)"), std::string::npos);
    EXPECT_NE(reports.front().find(R"("n":8)"), std::string::npos);
    EXPECT_FALSE(pages.take_close_request());
    EXPECT_EQ(request(pages.page_url("close"), "POST").status, 204);
    EXPECT_TRUE(pages.take_close_request());
    EXPECT_FALSE(pages.take_close_request());
    const std::string event_url = pages.page_url("event");
    const std::string path = event_url.substr(event_url.find("/s/"));
    const std::string host = "Host: 127.0.0.1:" + std::to_string(pages.port());
    EXPECT_EQ(pages.respond_to("DELETE " + path + " HTTP/1.1\r\n" + host + "\r\n\r\n").status, 405);
    EXPECT_EQ(pages.respond_to("GET " + path + " HTTP/1.1\r\n" + host + "\r\n\r\n").status, 404);
    EXPECT_EQ(pages.respond_to("garbage").status, 400);
    EXPECT_GT(pages.last_contact_ms(), 0u);

    // The bundled clip with byte ranges, as WebKit's media loader asks for it.
    r = request(pages.page_url("test.mp4"), "GET", {}, {"Range: bytes=0-99"});
    EXPECT_EQ(r.status, 206);
    EXPECT_EQ(r.body.size(), 100u);
    r = request(pages.page_url("test.mp4"));
    EXPECT_EQ(r.status, 200);
    EXPECT_GT(r.body.size(), 100000u);
    r = request(pages.page_url("test.mp4"), "GET", {}, {"Range: bytes=99999999-"});
    EXPECT_EQ(r.status, 416);
    // The lab's other clips: fragmented MP4 for MediaSource, an HLS playlist
    // with its MPEG-TS segment.
    r = request(pages.page_url("test-frag.mp4"));
    EXPECT_EQ(r.status, 200);
    EXPECT_NE(r.body.find("moof"), std::string::npos);
    r = request(pages.page_url("test.m3u8"));
    EXPECT_EQ(r.status, 200);
    EXPECT_NE(r.body.find("#EXT-X-ENDLIST"), std::string::npos);
    EXPECT_NE(r.body.find("test.ts"), std::string::npos);
    r = request(pages.page_url("test.ts"), "GET", {}, {"Range: bytes=0-187"});
    EXPECT_EQ(r.status, 206);
    ASSERT_EQ(r.body.size(), 188u);
    EXPECT_EQ(r.body[0], 0x47); // MPEG-TS sync byte
    EXPECT_EQ(request(pages.page_url("../app.json")).status, 404);

    // The lab's frame comes from "localhost": it may be framed by the lab page
    // only; every other page refuses to be framed.
    const std::string frame_path = pages.page_url("frame").substr(base.size());
    const auto frame = pages.respond_to("GET " + frame_path + " HTTP/1.1\r\nHost: localhost:" +
                                        std::to_string(pages.port()) + "\r\n\r\n");
    EXPECT_EQ(frame.status, 200);
    std::string frame_headers;
    for (const auto &h : frame.headers)
        frame_headers += h + "\n";
    EXPECT_NE(
        frame_headers.find("frame-ancestors http://127.0.0.1:" + std::to_string(pages.port())),
        std::string::npos);
    EXPECT_EQ(frame_headers.find("X-Frame-Options"), std::string::npos);
    const auto lab = pages.respond_to(
        "GET " + pages.page_url("captest").substr(base.size()) +
        " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(pages.port()) + "\r\n\r\n");
    std::string lab_headers;
    for (const auto &h : lab.headers)
        lab_headers += h + "\n";
    EXPECT_NE(lab_headers.find("frame-ancestors 'none'"), std::string::npos);
    EXPECT_NE(lab_headers.find("X-Frame-Options: DENY"), std::string::npos);
    EXPECT_NE(lab_headers.find("frame-src https://www.youtube.com https://www.youtube-nocookie.com "
                               "http://localhost:" +
                               std::to_string(pages.port())),
              std::string::npos);

    const std::string old_token_page = pages.page_url("youtube");
    pages.stop();
    EXPECT_FALSE(pages.running());
    // The YouTube page is served without any media.
    ASSERT_TRUE(pages.start("", &error));
    EXPECT_EQ(request(pages.page_url("test.mp4")).status, 404);
    pages.stop();
    ASSERT_TRUE(pages.start(clips, &error));
    EXPECT_NE(pages.page_url("youtube"), old_token_page); // a new token every time
    pages.stop();
}

TEST(LocalPages, PagesCarryASecurityPolicy)
{
    web::LocalPages pages;
    // Without a running server there is no token: nothing is served.
    const auto reply = pages.respond_to("GET /s//youtube HTTP/1.1\r\nHost: 127.0.0.1:0\r\n\r\n");
    EXPECT_EQ(reply.status, 404);
    EXPECT_NE(web::youtube_page_js().find("origin: location.origin"), std::string::npos);
    EXPECT_NE(web::capability_page_js().find("requestMediaKeySystemAccess"), std::string::npos);
    EXPECT_NE(web::capability_page_js().find("com.widevine.alpha"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Whole sessions through the App with the scripted browser.
namespace
{
class AppHarness
{
  public:
    AppHarness() : dir_{fresh_dir("app")}
    {
        setenv("AKENO_DATA_DIR", dir_.c_str(), 1);
        test::reset_web_view_script();
        app_ = std::make_unique<App>(AppConfig{}, fonts_);
        app_->start(now_);
        Settings s = app_->store().settings();
        s.websites_notice_accepted = true;
        app_->store().update_settings(s);
    }
    ~AppHarness()
    {
        app_.reset();
        net::set_test_transport({});
        test::reset_web_view_script();
    }
    App &app()
    {
        return *app_;
    }
    void step(int frames = 1)
    {
        for (int i = 0; i < frames; ++i)
        {
            now_ += 16;
            app_->update(now_);
            if (app_->needs_redraw())
                app_->render(surface_, now_);
        }
    }
    // Steps until done() or about five seconds of real time pass.
    bool until(const std::function<bool()> &done)
    {
        for (int i = 0; i < 500; ++i)
        {
            if (done())
                return true;
            step();
            platform::sleep_us(10000);
        }
        return done();
    }
    const std::string &dir() const
    {
        return dir_;
    }

  private:
    std::string dir_;
    gfx::FontEngine fonts_;
    std::vector<gfx::Pixel> pixels_ = std::vector<gfx::Pixel>(1920 * 1080);
    gfx::Surface surface_{pixels_.data(), 1920, 1080, 1920};
    std::uint64_t now_ = 1000;
    std::unique_ptr<App> app_;
};
} // namespace

TEST(BrowserSession, OpensAWebsiteAndComesBack)
{
    AppHarness h;
    net::set_test_transport([](const net::Request &) { return html("<title>Example</title>"); });
    auto &script = test::web_view_script();
    script.finish_after_updates = 20;
    h.app().open_address("example.com");
    EXPECT_TRUE(h.app().browser_active());
    ASSERT_TRUE(h.until([&] { return !script.opened.empty(); }));
    EXPECT_EQ(script.opened[0].url, "https://example.com/");
    EXPECT_EQ(script.opened[0].layout, platform::WebLayout::standard);
    ASSERT_TRUE(h.until([&] { return !h.app().browser_active(); }));
    ASSERT_EQ(h.app().websites().recent().size(), 1u);
    EXPECT_EQ(h.app().websites().recent()[0].url, "https://example.com/");
    ASSERT_NE(h.app().web_tests().get("browser.open"), nullptr);
    EXPECT_EQ(h.app().web_tests().get("browser.open")->outcome, web::Outcome::yes);
    EXPECT_EQ(h.app().web_tests().get("browser.return")->outcome, web::Outcome::yes);
    // Searches go to the chosen engine and skip the site check.
    h.app().open_address("lofi music");
    ASSERT_TRUE(h.until([&] { return script.opened.size() == 2; }));
    EXPECT_EQ(script.opened[1].url, "https://duckduckgo.com/?q=lofi%20music");
    ASSERT_TRUE(h.until([&] { return !h.app().browser_active(); }));
}

TEST(BrowserSession, AsksBeforeOpeningAnUnreachableSite)
{
    AppHarness h;
    net::set_test_transport(
        [](const net::Request &)
        {
            net::Response e;
            e.outcome = net::Outcome::network_error;
            e.curl_code = 6;
            return e;
        });
    auto &script = test::web_view_script();
    h.app().open_address("no-such-site.example");
    h.until([&] { return h.app().jobs().pending() == 0; }); // the check answers
    h.step(3);
    EXPECT_TRUE(script.opened.empty());
    EXPECT_TRUE(h.app().browser_active());
    h.app().handle({input::Button::cross, false}); // open anyway
    ASSERT_TRUE(h.until([&] { return !script.opened.empty(); }));
    ASSERT_TRUE(h.until([&] { return !h.app().browser_active(); }));
}

TEST(BrowserSession, ReportsAnUnavailableEngine)
{
    AppHarness h;
    auto &script = test::web_view_script();
    script.available = false;
    Settings s = h.app().store().settings();
    s.web_check_first = false;
    h.app().store().update_settings(s);
    h.app().open_address("example.com");
    h.step(5);
    EXPECT_TRUE(script.opened.empty());
    ASSERT_NE(h.app().web_tests().get("browser.open"), nullptr);
    EXPECT_EQ(h.app().web_tests().get("browser.open")->outcome, web::Outcome::no);
    h.app().handle({input::Button::circle, false});
    h.step(2);
    EXPECT_FALSE(h.app().browser_active());
}

TEST(BrowserSession, RunsTheYouTubePlayerAndRecordsEachCapability)
{
    AppHarness h;
    net::Client::global_init();
    auto &script = test::web_view_script();
    script.finish_after_updates = -1; // open until the page asks to close
    h.app().play_youtube({"aqz-KE-bpKQ", {}, 30}, "Big Buck Bunny");
    ASSERT_TRUE(h.until([&] { return !script.opened.empty(); }));
    const std::string url = script.opened[0].url;
    EXPECT_TRUE(url.starts_with("http://127.0.0.1:")) << url;
    EXPECT_NE(url.find("/youtube?v=aqz-KE-bpKQ&t=30"), std::string::npos) << url;

    // What the page would send from the console's browser.
    const std::string base = url.substr(0, url.find("/youtube?"));
    request(base + "/youtube.js");
    for (const char *event :
         {R"j({"type":"loaded","value":"1","detail":"Mozilla/5.0 (PlayStation; PlayStation 5)"})j",
          R"({"type":"api","value":"ok"})", R"({"type":"ready","value":"1.5"})",
          R"({"type":"playing","value":"2.4"})", R"({"type":"state","value":"2"})",
          R"({"type":"state","value":"1"})", R"({"type":"fullscreen","value":"ok"})",
          R"({"type":"input","value":"ArrowRight","detail":"keyCode 39"})"})
        EXPECT_EQ(request(base + "/event", "POST", event).status, 204);
    h.step(3);
    EXPECT_EQ(h.app().web_tests().get("youtube.playing")->outcome, web::Outcome::yes);
    EXPECT_EQ(h.app().web_tests().get("youtube.pause")->outcome, web::Outcome::yes);
    EXPECT_EQ(h.app().web_tests().get("youtube.resume")->outcome, web::Outcome::yes);
    EXPECT_EQ(h.app().web_tests().get("youtube.fullscreen")->outcome, web::Outcome::yes);

    // "Back to AKENO" on the page closes the browser.
    EXPECT_EQ(request(base + "/close", "POST").status, 204);
    ASSERT_TRUE(h.until([&] { return !h.app().browser_active(); }));
    EXPECT_EQ(script.closes_requested, 1);
    EXPECT_EQ(h.app().web_tests().get("youtube.return")->outcome, web::Outcome::yes);
    EXPECT_EQ(h.app().web_tests().get("browser.keys")->detail, "ArrowRight (keyCode 39)");
    EXPECT_FALSE(h.app().local_pages().running()); // the page server lives only with the page
}

TEST(BrowserSession, RecordsYouTubeErrorsAndTheFullScreenLayout)
{
    AppHarness h;
    Settings s = h.app().store().settings();
    s.web_full_screen_pages = true;
    h.app().store().update_settings(s);
    auto &script = test::web_view_script();
    script.finish_after_updates = -1;
    h.app().play_youtube({{}, "PLx0sYbCqOb8TBPRdmBHs5Iftvv9TPboYG", 0}, "Playlist");
    ASSERT_TRUE(h.until([&] { return !script.opened.empty(); }));
    EXPECT_EQ(script.opened[0].layout, platform::WebLayout::custom);
    EXPECT_NE(script.opened[0].url.find("list=PLx0sYbCqOb8TBPRdmBHs5Iftvv9TPboYG"),
              std::string::npos);
    const std::string base = script.opened[0].url.substr(0, script.opened[0].url.find("/youtube?"));
    request(base + "/youtube.js");
    request(
        base + "/event", "POST",
        R"({"type":"error","value":"150","detail":"The owner of this video does not allow it"})");
    request(base + "/close", "POST");
    ASSERT_TRUE(h.until([&] { return !h.app().browser_active(); }));
    EXPECT_EQ(h.app().web_tests().get("youtube.error")->outcome, web::Outcome::no);
    EXPECT_NE(h.app().web_tests().get("youtube.error")->detail.find("150"), std::string::npos);
    EXPECT_EQ(h.app().web_tests().get("youtube.playing")->outcome, web::Outcome::no);
}

TEST(BrowserSession, SavesTheCapabilityReport)
{
    AppHarness h;
    auto &script = test::web_view_script();
    script.finish_after_updates = -1;
    h.app().run_browser_test();
    ASSERT_TRUE(h.until([&] { return !script.opened.empty(); }));
    const std::string url = script.opened[0].url;
    EXPECT_TRUE(url.ends_with("/captest")) << url;
    const std::string base = url.substr(0, url.size() - 8);
    EXPECT_EQ(request(base + "/captest.js").status, 200);
    EXPECT_EQ(
        request(
            base + "/report", "POST",
            R"({"version":2,"done":false,"results":[{"id":"drm.eme","group":"DRM","name":"EME","status":"no"}]})")
            .status,
        204);
    // Every partial report is saved while the page is still open.
    ASSERT_TRUE(h.until([&] { return h.app().web_tests().get("drm.eme") != nullptr; }));
    EXPECT_TRUE(h.app().browser_active());
    EXPECT_EQ(
        request(
            base + "/report", "POST",
            R"({"version":2,"done":true,"results":[{"id":"drm.eme","group":"DRM","name":"EME","status":"no"},
                         {"id":"codec.h264","group":"Video","name":"H.264","status":"yes"}]})")
            .status,
        204);
    // The lab's clips are served while it runs.
    EXPECT_EQ(request(base + "/test-frag.mp4").status, 200);
    request(base + "/close", "POST");
    ASSERT_TRUE(h.until([&] { return !h.app().browser_active(); }));
    EXPECT_EQ(h.app().web_tests().get("drm.eme")->outcome, web::Outcome::no);
    EXPECT_EQ(h.app().web_tests().get("codec.h264")->outcome, web::Outcome::yes);
    EXPECT_EQ(h.app().web_tests().get("captest.finished")->outcome, web::Outcome::yes);
    // The report (and the diagnostics export) carries the results.
    const std::string report = h.app().diagnostics().build_report(h.app().snapshot());
    EXPECT_NE(report.find("[no] drm.eme"), std::string::npos);
    EXPECT_NE(report.find("Browser (Websites, YouTube player)"), std::string::npos);
}

TEST(BrowserSession, KeepsTheLabResultsOfAnEarlyExit)
{
    AppHarness h;
    auto &script = test::web_view_script();
    script.finish_after_updates = -1;
    h.app().run_browser_test();
    ASSERT_TRUE(h.until([&] { return !script.opened.empty(); }));
    const std::string base = script.opened[0].url.substr(0, script.opened[0].url.size() - 8);
    request(base + "/captest.js");
    request(
        base + "/report", "POST",
        R"({"version":2,"done":false,"results":[{"id":"mse.available","group":"Streaming","name":"MediaSource","status":"yes"}]})");
    request(base + "/close", "POST");
    ASSERT_TRUE(h.until([&] { return !h.app().browser_active(); }));
    EXPECT_EQ(h.app().web_tests().get("mse.available")->outcome, web::Outcome::yes);
    EXPECT_EQ(h.app().web_tests().get("captest.finished")->outcome, web::Outcome::no);
    EXPECT_NE(h.app().web_tests().get("captest.finished")->detail.find("before the last test"),
              std::string::npos);
}

TEST(BrowserSession, RunsTheAfterStepOnceTheBrowserHasClosed)
{
    AppHarness h;
    Settings s = h.app().store().settings();
    s.web_check_first = false;
    h.app().store().update_settings(s);
    auto &script = test::web_view_script();
    script.finish_after_updates = 3;
    int calls = 0;
    WebSession session;
    session.url = "https://hlsjs.video-dev.org/demo/";
    session.after = [&]
    {
        ++calls;
        EXPECT_FALSE(h.app().browser_active());
    };
    h.app().open_web(std::move(session));
    ASSERT_TRUE(h.until([&] { return calls > 0; }));
    EXPECT_EQ(calls, 1);
}

TEST(BrowserSession, OpensASavedPrivateWebsite)
{
    AppHarness h;
    std::string why, id;
    ASSERT_TRUE(h.app().websites().add("Docs", "docs.example", &why, &id));
    web::Website w = *h.app().websites().find(id);
    w.private_site = true;
    h.app().websites().update(w);
    Settings s = h.app().store().settings();
    s.web_check_first = false;
    h.app().store().update_settings(s);
    MediaItem item;
    item.provider = "website";
    item.id = id;
    h.app().open_item(item);
    auto &script = test::web_view_script();
    ASSERT_TRUE(h.until([&] { return !script.opened.empty(); }));
    EXPECT_EQ(script.opened[0].url, "https://docs.example/");
    ASSERT_TRUE(h.until([&] { return !h.app().browser_active(); }));
    EXPECT_TRUE(h.app().websites().recent().empty()); // private
    EXPECT_EQ(h.app().websites().find(id)->visits, 1);
    EXPECT_NE(h.app().websites().find(id)->last_result.find("opened"), std::string::npos);
}

TEST(Store, BrowserSettingsRoundTrip)
{
    const std::string dir = fresh_dir("settings");
    {
        Store store(dir);
        store.load();
        Settings s = store.settings();
        EXPECT_EQ(s.web_search, "duckduckgo");
        EXPECT_TRUE(s.web_check_first);
        EXPECT_FALSE(s.web_full_screen_pages);
        s.web_search = "startpage";
        s.web_check_first = false;
        s.web_full_screen_pages = true;
        s.websites_notice_accepted = true;
        store.update_settings(s);
    }
    Store reloaded(dir);
    reloaded.load();
    EXPECT_EQ(reloaded.settings().web_search, "startpage");
    EXPECT_FALSE(reloaded.settings().web_check_first);
    EXPECT_TRUE(reloaded.settings().web_full_screen_pages);
    EXPECT_TRUE(reloaded.settings().websites_notice_accepted);
}
