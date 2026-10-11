// AKENO STREAM PS5 - The video-only player: official embeds, frame rules, routes.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for what decides whether a video may play on AKENO's video-only
// page: the documented YouTube and PeerTube embed addresses (and nothing
// that only looks like them), the frame restrictions a service sends, and
// the route an episode takes (native, embed, website, unavailable). The
// page itself runs in a real browser in tests/browser/video_only.mjs.
#include "anime/playback_route.hpp"
#include "browser_page.hpp"
#include "web/embed.hpp"
#include "web/local_pages.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace akeno;

namespace
{
constexpr char kUuid[] = "9c9de5e8-0a1e-484a-b099-e80766180a6d";
constexpr char kShort[] = "kkGMgK9ZtnKfYAgnEtQxbv";
constexpr char kEmbedder[] = "http://127.0.0.1:8095";
constexpr char kInstance[] = "https://video.blender.org";
} // namespace

TEST(Embed, ReadsTheDocumentedYouTubeAndPeerTubeAddresses)
{
    auto t = web::parse_embed("https://www.youtube.com/watch?v=aqz-KE-bpKQ&t=30");
    ASSERT_TRUE(t);
    EXPECT_EQ(t->kind, web::EmbedKind::youtube);
    EXPECT_EQ(t->video_id, "aqz-KE-bpKQ");
    EXPECT_EQ(t->start_seconds, 30);
    EXPECT_EQ(t->origin(), "https://www.youtube.com");
    EXPECT_EQ(t->page_url(), "https://www.youtube.com/watch?v=aqz-KE-bpKQ");
    t = web::parse_embed("https://www.youtube-nocookie.com/embed/aqz-KE-bpKQ");
    ASSERT_TRUE(t);
    EXPECT_EQ(t->kind, web::EmbedKind::youtube);

    t = web::parse_embed(std::string{kInstance} + "/videos/embed/" + kUuid);
    ASSERT_TRUE(t);
    EXPECT_EQ(t->kind, web::EmbedKind::peertube);
    EXPECT_EQ(t->host, "video.blender.org");
    EXPECT_EQ(t->video_id, kUuid);
    EXPECT_EQ(t->origin(), kInstance);
    EXPECT_EQ(t->page_url(), std::string{kInstance} + "/w/" + kUuid);
    t = web::parse_embed(std::string{"  "} + kInstance + "/w/" + kShort + "?start=1m30s ");
    ASSERT_TRUE(t);
    EXPECT_EQ(t->video_id, kShort);
    EXPECT_EQ(t->start_seconds, 90);
    t = web::parse_embed(std::string{"https://Tube.Example:8443/videos/watch/"} + kUuid + "?t=45");
    ASSERT_TRUE(t);
    EXPECT_EQ(t->host, "tube.example:8443");
    EXPECT_EQ(t->start_seconds, 45);
    EXPECT_EQ(web::watch_query(*t),
              std::string{"p=peertube&h=tube.example%3A8443&v="} + kUuid + "&t=45");
}

TEST(Embed, RefusesWhatIsNotADocumentedEmbed)
{
    for (const std::string &address :
         {std::string{"https://www.youtube.com/playlist?list=PLx0sYbCqOb8TBPRdmBHs5Iftvv9TPboYG"},
          "http://video.blender.org/w/" + std::string{kShort}, // not https
          std::string{kInstance} + "/w/p/" + kShort,           // a playlist
          std::string{kInstance} + "/videos/embed/../../etc",
          std::string{kInstance} + "/videos/embed/" + kUuid + "/extra",
          std::string{kInstance} + "/videos/embed/O0Il" + kShort, // not base58
          std::string{"https://localhost/w/"} + kShort,           // not a public name
          std::string{"https://user:pw@video.blender.org/w/"} + kShort,
          std::string{"javascript:alert(1)"}, std::string{}, std::string{"not an address"}})
        EXPECT_FALSE(web::parse_embed(address)) << address;

    // Hosts end up in the page's Content-Security-Policy header.
    for (const char *host : {"video.blender.org; script-src *", "a.b\r\nX-Evil: 1", "a b.org",
                             ".a.org", "a.org.", "-a.org", "a..org", "A.ORG", "a.org:0",
                             "a.org:65536", "a.org:", "a.org:80:80", "*.a.org"})
        EXPECT_FALSE(web::valid_embed_host(host)) << host;
    EXPECT_TRUE(web::valid_embed_host("video.blender.org"));
    EXPECT_TRUE(web::valid_embed_host("tube.example:8443"));

    web::EmbedTarget forged;
    forged.kind = web::EmbedKind::peertube;
    forged.host = "a.org; frame-src *";
    forged.video_id = kUuid;
    EXPECT_FALSE(forged.valid());
    EXPECT_EQ(forged.origin(), "");
    EXPECT_EQ(web::watch_query(forged), "");
}

TEST(Embed, ReadsPeerTubeSourceReferences)
{
    auto t = web::peertube_embed_from_ref(std::string{kUuid} + "@Video.Blender.org");
    ASSERT_TRUE(t);
    EXPECT_EQ(t->host, "video.blender.org");
    EXPECT_EQ(t->video_id, kUuid);
    EXPECT_FALSE(web::peertube_embed_from_ref("abc@video.blender.org"));
    EXPECT_FALSE(web::peertube_embed_from_ref(std::string{kUuid} + "@a@b.org"));
    EXPECT_FALSE(web::peertube_embed_from_ref(kUuid));
    EXPECT_FALSE(web::peertube_embed_from_ref("https://site.example/episode-1"));
}

TEST(Embed, FrameRulesFollowTheBrowsers)
{
    using web::FramePolicy;
    const auto policy = [](const char *xfo, const char *csp)
    { return web::frame_policy(xfo, csp, kInstance, kEmbedder).policy; };
    EXPECT_EQ(policy("", ""), FramePolicy::unknown);
    EXPECT_EQ(policy("DENY", ""), FramePolicy::blocked);
    EXPECT_EQ(policy(" sameorigin ", ""), FramePolicy::blocked);
    EXPECT_EQ(policy("ALLOW-FROM http://127.0.0.1:8095", ""), FramePolicy::unknown);
    EXPECT_EQ(policy("", "default-src 'self'"), FramePolicy::unknown);
    EXPECT_EQ(policy("DENY", "default-src 'self'"), FramePolicy::blocked);
    EXPECT_EQ(policy("", "frame-ancestors 'none'"), FramePolicy::blocked);
    EXPECT_EQ(policy("", "frame-ancestors 'self'"), FramePolicy::blocked);
    EXPECT_EQ(policy("", "frame-ancestors 'self' https://partner.example"), FramePolicy::blocked);
    EXPECT_EQ(policy("", "default-src 'self'; frame-ancestors *"), FramePolicy::allowed);
    EXPECT_EQ(policy("DENY", "frame-ancestors *"), FramePolicy::allowed); // CSP decides
    EXPECT_EQ(policy("", "frame-ancestors http:"), FramePolicy::allowed);
    EXPECT_EQ(policy("", "frame-ancestors https:"), FramePolicy::blocked);
    EXPECT_EQ(policy("", "frame-ancestors http://127.0.0.1:*"), FramePolicy::allowed);
    EXPECT_EQ(policy("", "frame-ancestors 127.0.0.1:8095"), FramePolicy::allowed);
    EXPECT_EQ(policy("", "frame-ancestors http://127.0.0.1:9000"), FramePolicy::blocked);
    EXPECT_EQ(policy("", "frame-ancestors http://127.0.0.1"), FramePolicy::blocked);
    EXPECT_EQ(policy("", "frame-ancestors *.example"), FramePolicy::blocked);
    // Several policies must all allow it.
    EXPECT_EQ(policy("", "frame-ancestors *, frame-ancestors 'none'"), FramePolicy::blocked);
    EXPECT_NE(web::frame_policy("DENY", "", kInstance, kEmbedder).detail.find("DENY"),
              std::string::npos);
}

TEST(PlaybackRoute, PrefersNativeThenTheOfficialEmbedThenTheWebsite)
{
    anime::Episode e;
    e.availability = anime::Availability::unverified;
    EXPECT_EQ(anime::route_episode(e).route, anime::Route::unavailable);

    e.media = anime::MediaResource{};
    e.media->url = "https://cdn.example/e1.mp4";
    EXPECT_EQ(anime::route_episode(e).route, anime::Route::native);

    // A PeerTube video plays natively and also has the official embed.
    anime::Episode pt;
    pt.availability = anime::Availability::unverified;
    pt.source_ref = std::string{kUuid} + "@video.blender.org";
    EXPECT_EQ(anime::route_episode(pt).route, anime::Route::native);
    auto embed = anime::episode_embed(pt);
    ASSERT_TRUE(embed);
    EXPECT_EQ(embed->kind, web::EmbedKind::peertube);

    anime::Episode web_player;
    web_player.availability = anime::Availability::episodes_indexed;
    web_player.embed = "https://www.youtube.com/embed/aqz-KE-bpKQ";
    anime::PlaybackRoute r = anime::route_episode(web_player);
    EXPECT_EQ(r.route, anime::Route::embed);
    EXPECT_EQ(r.embed.kind, web::EmbedKind::youtube);
    EXPECT_STREQ(anime::route_label(r.route), "Video-only player");

    // An embed AKENO does not know: the website, labelled as such.
    web_player.embed = "https://player.example/e/1";
    web_player.link = "https://site.example/show/1";
    web_player.link_label = "Example";
    r = anime::route_episode(web_player);
    EXPECT_EQ(r.route, anime::Route::website);
    EXPECT_EQ(r.website, "https://site.example/show/1");
    EXPECT_NE(r.reason.find("not in AKENO's player"), std::string::npos);

    // DRM, a sign-in, a region or a missing video: never an embed or website.
    for (anime::Availability a :
         {anime::Availability::drm_unsupported, anime::Availability::auth_required,
          anime::Availability::region_unavailable, anime::Availability::missing})
    {
        anime::Episode locked = pt;
        locked.embed = "https://www.youtube.com/embed/aqz-KE-bpKQ";
        locked.link = "https://site.example/show/1";
        locked.availability = a;
        EXPECT_EQ(anime::route_episode(locked).route, anime::Route::unavailable)
            << anime::availability_id(a);
        EXPECT_FALSE(anime::embed_may_follow(a));
    }
    EXPECT_TRUE(anime::embed_may_follow(anime::Availability::provider_unsupported));
    EXPECT_FALSE(anime::embed_may_follow(anime::Availability::network_error));
}

// ---------------------------------------------------------------------------
// The page in Chromium (tests/browser/video_only.cjs), served by the real page
// server. Opt-in: AKENO_BROWSER_TESTS=1 with Node.js and Playwright installed
// (NODE_PATH pointing at the directory that holds the playwright package).
namespace
{
bool browser_tests() noexcept
{
    return test::browser_tests_enabled();
}

int run_page(const std::string &scenario, const std::string &address, std::string *output)
{
    return test::run_browser_page(scenario, address, output);
}

web::EmbedTarget mock_instance(const char *host)
{
    web::EmbedTarget t;
    t.kind = web::EmbedKind::peertube;
    t.host = host;
    t.video_id = kUuid;
    t.start_seconds = 5;
    return t;
}

bool sent(const std::vector<web::PageEvent> &events, const std::string &type,
          const std::string &value = {}, const std::string &detail = {})
{
    return std::any_of(events.begin(), events.end(),
                       [&](const web::PageEvent &e)
                       {
                           return e.type == type && (value.empty() || e.value == value) &&
                                  (detail.empty() || e.detail == detail);
                       });
}

std::string listed(const std::vector<web::PageEvent> &events)
{
    std::string out;
    for (const web::PageEvent &e : events)
        out += e.type + "=" + e.value + (e.detail.empty() ? "" : " (" + e.detail + ")") + "\n";
    return out;
}
} // namespace

TEST(BrowserPage, PeerTubeEmbedApiDrivesTheVideoInChromium)
{
    if (!browser_tests())
        GTEST_SKIP() << "AKENO_BROWSER_TESTS=1 runs the page in Chromium (Playwright)";
    for (const char *scenario : {"peertube", "peertube-legacy", "peertube-silent"})
    {
        SCOPED_TRACE(scenario);
        web::LocalPages pages;
        std::string error, output;
        const web::EmbedTarget target = mock_instance("peertube.test");
        ASSERT_TRUE(pages.start("", &error, target)) << error;
        const int code =
            run_page(scenario, pages.page_url("watch", web::watch_query(target)), &output);
        EXPECT_EQ(code, 0) << output;
        const std::vector<web::PageEvent> events = pages.take_events();
        for (const char *type : {"loaded", "ready", "playing"})
            EXPECT_TRUE(sent(events, type)) << type << "\n" << listed(events);
        EXPECT_TRUE(sent(events, "embed", "linked")) << listed(events);
        EXPECT_TRUE(sent(events, "embed", "frame-loaded", "https://peertube.test"))
            << listed(events);
        EXPECT_TRUE(sent(events, "state", "playing")) << listed(events);
        EXPECT_FALSE(sent(events, "error")) << listed(events);
        EXPECT_FALSE(sent(events, "log")) << listed(events);
        if (std::string{scenario} == "peertube")
        {
            EXPECT_TRUE(sent(events, "seek")) << listed(events);
            EXPECT_TRUE(sent(events, "state", "paused")) << listed(events);
            EXPECT_TRUE(sent(events, "progress", {}, "paused")) << listed(events);
            EXPECT_TRUE(sent(events, "fullscreen")) << listed(events);
            EXPECT_TRUE(sent(events, "input", "gamepad 5")) << listed(events);
            EXPECT_TRUE(sent(events, "ended")) << listed(events);
            EXPECT_TRUE(sent(events, "progress", "30.0/30.0", "ended")) << listed(events);
            EXPECT_TRUE(sent(events, "state", "exit")) << listed(events);
            EXPECT_TRUE(pages.take_close_request());
        }
        pages.stop();
    }
}

TEST(BrowserPage, FrameRefusalsAndThePagePolicyBecomeClearErrors)
{
    if (!browser_tests())
        GTEST_SKIP() << "AKENO_BROWSER_TESTS=1 runs the page in Chromium (Playwright)";
    web::LocalPages pages;
    std::string error, output;
    const web::EmbedTarget target = mock_instance("peertube.test");
    // The instance answers X-Frame-Options: DENY: the player never answers.
    ASSERT_TRUE(pages.start("", &error, target)) << error;
    EXPECT_EQ(run_page("frame-denied",
                       pages.page_url("watch", web::watch_query(target) + "&wait=3"), &output),
              0)
        << output;
    std::vector<web::PageEvent> events = pages.take_events();
    EXPECT_TRUE(sent(events, "error", "embed-timeout")) << listed(events);
    EXPECT_FALSE(sent(events, "ready")) << listed(events);

    // A page asked to frame another instance than the one AKENO opened it for:
    // its own Content-Security-Policy refuses, before any request.
    output.clear();
    ASSERT_TRUE(pages.start("", &error, target)) << error;
    EXPECT_EQ(run_page("csp",
                       pages.page_url("watch", web::watch_query(mock_instance("other.test"))),
                       &output),
              0)
        << output;
    events = pages.take_events();
    EXPECT_TRUE(sent(events, "error", "csp")) << listed(events);
    EXPECT_TRUE(sent(events, "embed", "csp")) << listed(events);
    pages.stop();
}

TEST(BrowserPage, YouTubeIFramePlayerApiInChromium)
{
    if (!browser_tests())
        GTEST_SKIP() << "AKENO_BROWSER_TESTS=1 runs the page in Chromium (Playwright)";
    web::EmbedTarget target;
    target.kind = web::EmbedKind::youtube;
    target.video_id = "aqz-KE-bpKQ";
    target.start_seconds = 5;
    web::LocalPages pages;
    std::string error, output;
    ASSERT_TRUE(pages.start("", &error, target)) << error;
    EXPECT_EQ(run_page("youtube", pages.page_url("watch", web::watch_query(target)), &output), 0)
        << output;
    std::vector<web::PageEvent> events = pages.take_events();
    EXPECT_TRUE(sent(events, "api", "ok")) << listed(events);
    EXPECT_TRUE(sent(events, "playing")) << listed(events);
    EXPECT_TRUE(sent(events, "seek")) << listed(events);
    EXPECT_TRUE(sent(events, "state", "paused")) << listed(events);
    EXPECT_TRUE(pages.take_close_request());

    output.clear();
    ASSERT_TRUE(pages.start("", &error, target)) << error;
    EXPECT_EQ(run_page("youtube-error", pages.page_url("watch", web::watch_query(target)), &output),
              0)
        << output;
    events = pages.take_events();
    EXPECT_TRUE(sent(events, "error", "150")) << listed(events);
    pages.stop();
}
