// AKENO STREAM PS5 - Website providers: adapters, detection, catalogue, series, playback checks.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Offline. The schema.org adapter reads tests/fixtures/website-schema, served
// with AKENO's self-test clips from a local HTTP server; the PeerTube
// instance adapter talks to canned answers shaped like PeerTube's documented
// REST API. What is checked: a site is recognised only from what it
// publishes, catalogue pages and search come from the site, seasons and
// episodes load from the series' own pages, only a contentUrl is ever played
// (never an embed or a trailer), sign-in, DRM and missing files are labelled,
// and every request stays on the provider's own site.
#include "anime/registry.hpp"
#include "anime/website.hpp"
#include "core/fs.hpp"
#include "net/http.hpp"
#include "test_server.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

using namespace akeno;
using namespace akeno::anime;

namespace
{
std::string source_root()
{
    const char *root = std::getenv("AKENO_SOURCE_ROOT");
    return root ? root : ".";
}

// The fixture site with the self-test clips, in a fresh folder.
std::string make_site()
{
    namespace sfs = std::filesystem;
    char path[] = "/tmp/akeno-site-XXXXXX";
    const std::string root = mkdtemp(path) ? path : "/tmp/akeno-site";
    const sfs::path source{source_root()};
    sfs::copy(source / "tests/fixtures/website-schema", root,
              sfs::copy_options::recursive | sfs::copy_options::overwrite_existing);
    sfs::create_directories(sfs::path{root} / "media");
    sfs::create_directories(sfs::path{root} / "subs");
    sfs::create_directories(sfs::path{root} / "art");
    for (const char *clip : {"h264-aac-360p.mp4", "h264-aac-360p.ts", "h264-aac-360p-cenc.m4s",
                             "h264-aac-360p-frag.mp4"})
        sfs::copy_file(source / "assets/selftest" / clip, sfs::path{root} / "media" / clip,
                       sfs::copy_options::overwrite_existing);
    sfs::copy_file(source / "assets/anime/sample/subs/s1e1.en.vtt",
                   sfs::path{root} / "subs/s1e1.en.vtt", sfs::copy_options::overwrite_existing);
    sfs::copy_file(source / "assets/anime/sample/art/poster.jpg",
                   sfs::path{root} / "art/signal.jpg", sfs::copy_options::overwrite_existing);
    return root;
}

class Site
{
  public:
    Site() : root_{make_site()}, server_{root_}
    {
        server_.override_body("", fs::read_text(root_ + "/index.html").value_or(""));
    }
    test::TestServer &server()
    {
        return server_;
    }
    // path starts with '/', as on the site.
    std::string url(const std::string &path) const
    {
        return server_.url(path.empty() || path.front() != '/' ? path : path.substr(1));
    }
    // The provider as "Add Streaming Provider" would save it.
    ProviderConfig provider()
    {
        const SitePage home = fetch_site_page(url("/"), {});
        const SiteDetection d = detect_website(home, {});
        ProviderConfig c;
        c.id = "web_fixture";
        c.name = "Open Anime Fixture";
        c.type = ProviderType::website;
        c.adapter = d.adapter;
        c.settings = d.settings;
        c.address = d.homepage;
        c.original_url = url("/");
        return c;
    }

  private:
    std::string root_;
    test::TestServer server_;
};

const Series *titled(const std::vector<Series> &list, const std::string &title)
{
    for (const Series &s : list)
        if (s.title == title)
            return &s;
    return nullptr;
}

Series entry(const ProviderConfig &config, const std::string &title)
{
    const SiteListing page = load_website_page(config, {}, {}, {});
    const Series *s = titled(page.series, title);
    EXPECT_NE(s, nullptr) << title;
    return s ? *s : Series{};
}
} // namespace

TEST(WebsiteSchema, ReadsJsonLdBlocksAndNextLinks)
{
    const std::string html = R"(<html><head>
      <script type="application/ld+json">{"@type": "TVSeries", "name": "A"}</script>
      <SCRIPT TYPE='application/ld+json; charset=utf-8'><![CDATA[ [{"@type": ["Movie", "CreativeWork"]}] ]]></SCRIPT>
      <script type="application/ld+json"><!-- {"@type": "VideoObject"} --></script>
      <script type="application/ld+json">{not json}</script>
      <script type="text/javascript">var x = {"@type": "TVSeries"};</script>
      <script type=application/ld+json>{"@context": "https://schema.org", "@type": "schema:TVSeason"}</script>
      <link href="/p/2?x=1&amp;y=2" rel="prev next">
    </head></html>)";
    const std::vector<json::Value> blocks = schema::json_ld_blocks(html);
    ASSERT_EQ(blocks.size(), 4u); // the damaged block and the plain script are left out
    EXPECT_TRUE(schema::is_type(blocks[0], "TVSeries"));
    EXPECT_TRUE(schema::is_type(blocks[1][0], "Movie"));
    EXPECT_TRUE(schema::is_type(blocks[2], "VideoObject"));
    EXPECT_TRUE(schema::is_type(blocks[3], "TVSeason"));
    EXPECT_FALSE(schema::is_type(blocks[0], "Movie"));
    EXPECT_EQ(schema::next_link(html, "https://site.example/list/"),
              "https://site.example/p/2?x=1&y=2");
    EXPECT_EQ(schema::next_link("<a href=/b rel=next>more</a>", "https://site.example/a"),
              "https://site.example/b");
    EXPECT_EQ(schema::next_link("<link rel=nextpage href=/b>", "https://site.example/a"), "");
    EXPECT_EQ(schema::next_link("<link rel=next href='javascript:go()'>", "https://site.example/a"),
              "");
}

TEST(WebsiteSchema, ParsingIsBounded)
{
    std::string many;
    for (int i = 0; i < 40; ++i)
        many += R"(<script type="application/ld+json">{"@type": "VideoObject"}</script>)";
    EXPECT_EQ(schema::json_ld_blocks(many).size(), 32u);
    std::string huge = R"(<script type="application/ld+json">{"name": ")";
    huge.append(1100u * 1024u, 'x');
    huge += R"("}</script>)";
    EXPECT_TRUE(schema::json_ld_blocks(huge).empty());
    std::string deep = R"(<script type="application/ld+json">)";
    for (int i = 0; i < 200; ++i)
        deep += "[";
    for (int i = 0; i < 200; ++i)
        deep += "]";
    deep += "</script>";
    EXPECT_TRUE(schema::json_ld_blocks(deep).empty());
    EXPECT_TRUE(schema::json_ld_blocks("<script type=\"application/ld+json\">{\"a\": 1}").empty());
}

TEST(WebsiteSchema, SameSiteRule)
{
    EXPECT_TRUE(same_site("https://anime.example/a", "https://anime.example/"));
    EXPECT_TRUE(same_site("https://www.anime.example/a", "https://anime.example/"));
    EXPECT_TRUE(same_site("https://cdn.anime.example/a", "https://anime.example/"));
    EXPECT_TRUE(same_site("http://anime.example/a", "https://anime.example/"));
    EXPECT_FALSE(same_site("https://anime.example.evil.example/", "https://anime.example/"));
    EXPECT_FALSE(same_site("https://other.example/", "https://anime.example/"));
    EXPECT_FALSE(same_site("http://127.0.0.1:9000/", "http://127.0.0.1:9001/"));
    EXPECT_TRUE(same_site("http://127.0.0.1:9000/x", "http://127.0.0.1:9000/"));
}

TEST(WebsiteSchema, RecognisesASiteFromItsStructuredData)
{
    Site site;
    const SitePage home = fetch_site_page(site.url("/"), {});
    ASSERT_TRUE(home.ok) << home.error;
    const SiteDetection d = detect_website(home, {});
    ASSERT_TRUE(d.recognised) << d.why_not;
    EXPECT_EQ(d.adapter, "schema-org");
    EXPECT_EQ(d.site_name, "Open Anime Fixture");
    EXPECT_EQ(d.settings.at("catalogue"), site.url("/"));
    EXPECT_EQ(d.settings.at("search"), site.url("/search.html?q=AKENOQUERY"));
    EXPECT_TRUE(d.capabilities.catalogue);
    EXPECT_TRUE(d.capabilities.search);
    EXPECT_TRUE(d.capabilities.pagination);
    EXPECT_TRUE(d.capabilities.seasons);
    EXPECT_FALSE(d.capabilities.native_playback); // not before an episode was checked
    ASSERT_FALSE(d.evidence.empty());
    EXPECT_NE(d.evidence.front().find("ItemList"), std::string::npos) << d.evidence.front();

    // A page that answers 200 without structured data, or with structured
    // data about something else, is not a provider.
    const SiteDetection plain = detect_website(fetch_site_page(site.url("/plain.html"), {}), {});
    EXPECT_FALSE(plain.recognised);
    EXPECT_NE(plain.why_not.find("structured data"), std::string::npos) << plain.why_not;
    const SiteDetection about =
        detect_website(fetch_site_page(site.url("/organisation.html"), {}), {});
    EXPECT_FALSE(about.recognised);
    EXPECT_NE(about.why_not.find("Organization"), std::string::npos) << about.why_not;
}

TEST(WebsiteSchema, CataloguePagesAndSearch)
{
    Site site;
    const ProviderConfig config = site.provider();
    const SiteListing first = load_website_page(config, {}, {}, {});
    ASSERT_TRUE(first.ok) << first.error;
    ASSERT_EQ(first.series.size(), 4u); // another site's series is not listed
    EXPECT_EQ(first.series[0].title, "The Signal");
    EXPECT_EQ(first.series[0].id, "anime-the-signal-html");
    EXPECT_EQ(first.series[0].provider_id, "web_fixture");
    EXPECT_EQ(first.series[0].year, 2026);
    EXPECT_EQ(first.series[0].poster, site.url("/art/signal.jpg"));
    EXPECT_TRUE(first.series[0].details_pending);
    EXPECT_TRUE(first.series[0].seasons.empty());
    EXPECT_EQ(first.series[1].title, "Night Train");
    EXPECT_EQ(first.series[3].title, "Web Player Only"); // a ListItem with name and url only
    EXPECT_EQ(titled(first.series, "Elsewhere"), nullptr);
    EXPECT_EQ(first.next_page, site.url("/page-2.html"));

    const SiteListing second = load_website_page(config, first.next_page, {}, {});
    ASSERT_TRUE(second.ok) << second.error;
    ASSERT_EQ(second.series.size(), 1u);
    EXPECT_EQ(second.series[0].title, "Second Page Show");
    EXPECT_TRUE(second.next_page.empty());
    // A "next page" on another site is never followed.
    EXPECT_FALSE(load_website_page(config, "https://other-site.example/page-3", {}, {}).ok);

    const SiteListing found = search_website(config, "signal & co", {}, {});
    ASSERT_TRUE(found.ok) << found.error;
    ASSERT_EQ(found.series.size(), 1u); // listed twice on the page, once here
    EXPECT_EQ(found.series[0].title, "The Signal");
    EXPECT_EQ(found.series[0].id, first.series[0].id); // the same series
}

TEST(WebsiteSchema, SeriesSeasonsAndEpisodesFromTheSitesOwnPages)
{
    Site site;
    const ProviderConfig config = site.provider();
    const Series signal = entry(config, "The Signal");
    const SiteSeries full = load_website_series(config, signal, {}, {});
    ASSERT_TRUE(full.ok) << full.error;
    const Series &s = full.series;
    EXPECT_EQ(s.id, signal.id);
    EXPECT_FALSE(s.details_pending);
    EXPECT_EQ(s.description, "A fictional test series & more.");
    ASSERT_EQ(s.alt_titles.size(), 1u);
    ASSERT_EQ(s.genres.size(), 2u);
    ASSERT_EQ(s.seasons.size(), 2u);
    const Season &one = s.seasons[0];
    ASSERT_EQ(one.episodes.size(), 3u);
    EXPECT_EQ(one.episodes[0].number, 1); // ordered by episodeNumber
    EXPECT_EQ(one.episodes[0].title, "First Contact");
    ASSERT_TRUE(one.episodes[0].media.has_value());
    EXPECT_EQ(one.episodes[0].media->url, site.url("/media/h264-aac-360p.mp4"));
    EXPECT_EQ(one.episodes[0].media->container, "mp4");
    ASSERT_EQ(one.episodes[0].media->subtitles.size(), 1u);
    EXPECT_EQ(one.episodes[0].media->subtitles[0].url, site.url("/subs/s1e1.en.vtt"));
    EXPECT_EQ(one.episodes[0].media->subtitles[0].language, "en");
    EXPECT_EQ(one.episodes[0].duration, 3.0);
    EXPECT_EQ(one.episodes[0].air_date, "2026-01-05");
    EXPECT_EQ(one.episodes[1].media->kind, media::SourceKind::http_ts);
    EXPECT_EQ(one.episodes[1].thumbnail, site.url("/art/s1e2.jpg"));
    // Season 2 named only its page: read from there.
    const Season &two = s.seasons[1];
    EXPECT_EQ(two.number, 2);
    ASSERT_EQ(two.episodes.size(), 2u);
    EXPECT_FALSE(two.episodes[0].media.has_value());
    EXPECT_EQ(two.episodes[0].source_ref, site.url("/anime/the-signal/s2e1.html"));
    EXPECT_EQ(two.episodes[0].availability, Availability::unverified);
    // An embed is a web player: listed, never played.
    EXPECT_FALSE(two.episodes[1].media.has_value());
    EXPECT_TRUE(two.episodes[1].source_ref.empty());
    EXPECT_EQ(two.episodes[1].availability, Availability::episodes_indexed);
    EXPECT_NE(two.episodes[1].availability_detail.find("web player"), std::string::npos);

    // Checking: the first of each season first; the removed file is missing.
    Series checked = s;
    EXPECT_EQ(Registry::check_series(config, {}, checked, {}, 10), 4);
    EXPECT_EQ(checked.seasons[0].episodes[0].availability, Availability::playable);
    EXPECT_EQ(checked.seasons[0].episodes[1].availability, Availability::playable);
    EXPECT_EQ(checked.seasons[0].episodes[2].availability, Availability::missing);
    // The episode's own page names its file.
    EXPECT_EQ(checked.seasons[1].episodes[0].availability, Availability::playable)
        << checked.seasons[1].episodes[0].availability_detail;
    const auto provider = make_website_provider(config, {});
    const Resolution r = provider->resolve(s, two, two.episodes[0], {});
    ASSERT_TRUE(r.ok) << r.detail;
    EXPECT_EQ(r.media.url, site.url("/media/h264-aac-360p-frag.mp4"));
}

TEST(WebsiteSchema, ProtectedFilmsAndSubscriberEpisodesNeverPlay)
{
    Site site;
    const ProviderConfig config = site.provider();
    // A film: its video is encrypted; the playable trailer is not the film.
    const SiteSeries film = load_website_series(config, entry(config, "Night Train"), {}, {});
    ASSERT_TRUE(film.ok) << film.error;
    ASSERT_EQ(film.series.seasons.size(), 1u);
    ASSERT_EQ(film.series.seasons[0].episodes.size(), 1u);
    const Episode &movie = film.series.seasons[0].episodes[0];
    EXPECT_EQ(movie.title, "Night Train");
    ASSERT_TRUE(movie.media.has_value());
    EXPECT_EQ(movie.media->url, site.url("/media/h264-aac-360p-cenc.m4s"));
    Series checked = film.series;
    Registry::check_series(config, {}, checked, {}, 4);
    EXPECT_EQ(checked.seasons[0].episodes[0].availability, Availability::drm_unsupported);

    const SiteSeries locked = load_website_series(config, entry(config, "Locked Away"), {}, {});
    ASSERT_TRUE(locked.ok) << locked.error;
    ASSERT_EQ(locked.series.seasons.size(), 1u);
    for (const Episode &e : locked.series.seasons[0].episodes)
        EXPECT_EQ(e.availability, Availability::auth_required) << e.title;
    const int before = site.server().requests();
    Series locked_checked = locked.series;
    EXPECT_EQ(Registry::check_series(config, {}, locked_checked, {}, 4), 0);
    EXPECT_EQ(site.server().requests(), before); // nothing fetched for them

    const SiteSeries web = load_website_series(config, entry(config, "Web Player Only"), {}, {});
    ASSERT_TRUE(web.ok) << web.error;
    for (const Episode &e : web.series.seasons[0].episodes)
    {
        EXPECT_FALSE(e.media.has_value()) << e.title;
        EXPECT_TRUE(e.source_ref.empty()) << e.title; // no address is invented
        EXPECT_EQ(e.availability, Availability::episodes_indexed);
    }
}

TEST(WebsiteSchema, CheckSourceRecognisesAndSamplesTheSite)
{
    Site site;
    ProviderConfig config;
    const ProviderReport r = check_source(site.url("/"), "", {}, {}, &config);
    ASSERT_TRUE(r.importable) << r.verdict;
    EXPECT_EQ(r.adapter, "schema-org");
    EXPECT_EQ(r.adapter_label, "Structured data (schema.org)");
    EXPECT_NE(r.verdict.find("Recognised"), std::string::npos) << r.verdict;
    EXPECT_GT(r.playable, 0);
    EXPECT_EQ(r.series, 4);
    EXPECT_NE(std::find(r.capabilities.begin(), r.capabilities.end(), "Search on the site"),
              r.capabilities.end());
    EXPECT_NE(std::find(r.capabilities.begin(), r.capabilities.end(), "Native playback (checked)"),
              r.capabilities.end());
    ASSERT_GE(r.preview.size(), 2u);
    EXPECT_EQ(r.preview[0], "The Signal - 2 seasons, 5 episodes");
    EXPECT_EQ(r.preview[1], "Night Train (2025)"); // not opened while checking
    EXPECT_EQ(config.type, ProviderType::website);
    EXPECT_EQ(config.adapter, "schema-org");
    EXPECT_EQ(config.name, "Open Anime Fixture");
    EXPECT_EQ(config.address, site.url("/"));
    EXPECT_EQ(config.settings.at("search"), site.url("/search.html?q=AKENOQUERY"));
    EXPECT_EQ(config.known_series, 4);

    // Pages that are not providers say why, and are not importable.
    const ProviderReport plain = check_source(site.url("/plain.html"), "", {}, {}, nullptr);
    EXPECT_EQ(plain.source, SourceClass::web_page);
    EXPECT_FALSE(plain.importable);
    EXPECT_NE(plain.verdict.find("adapters"), std::string::npos) << plain.verdict;
}

TEST(WebsiteSchema, RegistryLoadsTheCatalogueAndOpensSeriesLater)
{
    Site site;
    char path[] = "/tmp/akeno-web-registry-XXXXXX";
    const std::string dir = mkdtemp(path) ? path : "/tmp";
    Registry registry(dir, dir);
    registry.load();
    ProviderConfig config;
    ASSERT_TRUE(check_source(site.url("/"), "", {}, {}, &config).importable);
    std::string why, added;
    ASSERT_TRUE(registry.add(config, &why, nullptr, &added)) << why;
    EXPECT_EQ(added, "web_open-anime-fixture");
    const ProviderConfig saved = *registry.config(added);
    ProviderState state = Registry::load_provider(saved, {}, {}, 12);
    ASSERT_TRUE(state.loaded) << state.error;
    EXPECT_EQ(state.series.size(), 4u);
    EXPECT_EQ(state.next_page, site.url("/page-2.html"));
    registry.install(std::move(state));
    const Series *signal = registry.find_series(added, "anime-the-signal-html");
    ASSERT_NE(signal, nullptr);
    EXPECT_TRUE(signal->details_pending);

    SiteSeries full = load_website_series(saved, *signal, {}, {});
    ASSERT_TRUE(full.ok);
    Registry::check_series(saved, {}, full.series, {}, 6);
    registry.install_series(added, std::move(full.series));
    signal = registry.find_series(added, "anime-the-signal-html");
    ASSERT_NE(signal, nullptr);
    EXPECT_FALSE(signal->details_pending);
    EXPECT_EQ(signal->seasons.size(), 2u);
    ASSERT_EQ(registry.available().size(), 1u);

    // The next page joins the list; a reload keeps what was opened.
    SiteListing more = load_website_page(saved, registry.state(added)->next_page, {}, {});
    ASSERT_TRUE(more.ok);
    const std::string end;
    registry.merge_series(added, std::move(more.series), &end);
    EXPECT_EQ(registry.state(added)->series.size(), 5u);
    EXPECT_TRUE(registry.state(added)->next_page.empty());
    registry.install(Registry::load_provider(saved, {}, {}, 0));
    EXPECT_EQ(registry.state(added)->series.size(), 5u);
    EXPECT_FALSE(registry.find_series(added, "anime-the-signal-html")->details_pending);
    // Local search finds catalogue entries before they are opened.
    EXPECT_FALSE(registry.search("night", {}).empty());
}

// ---------------------------------------------------------------------------
// A PeerTube instance, through answers shaped like its documented REST API.
namespace
{
std::mutex g_requests_lock;
std::vector<std::string> g_requests;

net::Response tube_instance(const net::Request &request)
{
    {
        std::lock_guard<std::mutex> guard(g_requests_lock);
        g_requests.push_back(request.url);
    }
    net::Response r;
    r.outcome = net::Outcome::ok;
    r.status = 200;
    const std::string &u = request.url;
    const auto starts = [&u](const char *prefix) { return u.rfind(prefix, 0) == 0; };
    const std::string video =
        R"({"uuid": "%s", "name": "%n", "duration": 300, "nsfw": false, "isLive": false,
        "thumbnailPath": "/lazy-static/thumbnails/%s.jpg", "publishedAt": "2025-03-01T00:00:00Z",
        "account": {"host": "tube.example"}, "channel": {"host": "tube.example"}})";
    const auto v = [&video](const std::string &uuid, const std::string &name)
    {
        std::string out = video;
        for (std::size_t p; (p = out.find("%s")) != std::string::npos;)
            out.replace(p, 2, uuid);
        out.replace(out.find("%n"), 2, name);
        return out;
    };
    if (u == "https://tube.example/")
    {
        r.content_type = "text/html";
        r.body = "<!doctype html><html><head><title>Open Tube</title></head>"
                 "<body><my-app>PeerTube</my-app></body></html>";
    }
    else if (u == "https://tube.example/api/v1/config")
        r.body = R"({"instance": {"name": "Open Tube", "shortDescription": "CC BY anime"},
                     "serverVersion": "7.2.1"})";
    else if (starts("https://tube.example/api/v1/video-channels?"))
        r.body = R"({"total": 3, "data": [
          {"name": "openanime", "displayName": "Open Anime", "isLocal": true, "host": "tube.example",
           "description": "CC BY episodes", "createdAt": "2024-05-01T00:00:00Z",
           "avatars": [{"path": "/lazy-static/avatars/b.png", "width": 600}], "banners": []},
          {"name": "faraway", "displayName": "Far Away", "isLocal": false, "host": "other.example"},
          {"name": "bad handle!", "displayName": "Bad", "isLocal": true, "host": "tube.example"}]})";
    else if (starts("https://tube.example/api/v1/search/video-channels?"))
        r.body = R"({"total": 1, "data": [
          {"name": "openanime", "displayName": "Open Anime", "isLocal": true, "host": "tube.example"}]})";
    else if (u == "https://tube.example/api/v1/video-channels/openanime")
        r.body = R"({"displayName": "Open Anime", "description": "CC BY episodes",
                     "avatars": [{"path": "/lazy-static/avatars/b.png", "width": 600}], "banners": []})";
    else if (starts("https://tube.example/api/v1/video-channels/openanime/video-playlists"))
        r.body = R"({"total": 1, "data": [
          {"uuid": "p1", "displayName": "Season One", "privacy": {"id": 1}, "type": {"id": 1},
           "videosLength": 1, "createdAt": "2024-01-01"}]})";
    else if (starts("https://tube.example/api/v1/video-playlists/p1/videos"))
        r.body =
            R"({"total": 1, "data": [{"position": 1, "video": )" + v("v1", "The First") + "}]}";
    else if (u == "https://tube.example/api/v1/videos/v1")
        r.body = R"({"uuid": "v1", "name": "The First",
          "streamingPlaylists": [{"playlistUrl": "https://tube.example/static/hls/v1/master.m3u8"}],
          "files": []})";
    else if (u == "https://tube.example/static/hls/v1/master.m3u8")
        r.body = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:4\n#EXTINF:4.0,\nseg0.ts\n"
                 "#EXT-X-ENDLIST\n";
    else if (u == "https://plain.example/")
        r.body = "<!doctype html><html><head><title>Plain</title></head><body></body></html>";
    else
    {
        r.outcome = net::Outcome::http_error;
        r.status = 404;
        r.body.clear();
    }
    return r;
}

struct TubeTransport
{
    TubeTransport()
    {
        std::lock_guard<std::mutex> guard(g_requests_lock);
        g_requests.clear();
        net::set_test_transport(tube_instance);
    }
    ~TubeTransport()
    {
        net::set_test_transport({});
    }
    static bool requested(const std::string &part)
    {
        std::lock_guard<std::mutex> guard(g_requests_lock);
        for (const std::string &u : g_requests)
            if (u.find(part) != std::string::npos)
                return true;
        return false;
    }
};
} // namespace

TEST(WebsitePeerTube, RecognisesAnInstanceByItsDocumentedApi)
{
    TubeTransport tube;
    const SiteDetection d = detect_website(fetch_site_page("https://tube.example/", {}), {});
    ASSERT_TRUE(d.recognised) << d.why_not;
    EXPECT_EQ(d.adapter, "peertube-instance");
    EXPECT_EQ(d.site_name, "Open Tube");
    EXPECT_EQ(d.homepage, "https://tube.example/");
    EXPECT_EQ(d.settings.at("host"), "tube.example");
    EXPECT_NE(d.format.find("PeerTube 7.2.1"), std::string::npos);
    // A site without the API is not a PeerTube instance, whatever it says.
    EXPECT_FALSE(detect_website(fetch_site_page("https://plain.example/", {}), {}).recognised);
}

TEST(WebsitePeerTube, LocalChannelsAreSeriesAndPlaylistsSeasons)
{
    TubeTransport tube;
    ProviderConfig config;
    const ProviderReport report = check_source("https://tube.example", "", {}, {}, &config);
    ASSERT_TRUE(report.importable) << report.verdict;
    EXPECT_EQ(report.adapter, "peertube-instance");
    EXPECT_EQ(report.series, 1); // only the instance's own, well-formed channels
    EXPECT_EQ(report.playable, 1);
    EXPECT_EQ(config.type, ProviderType::website);
    EXPECT_EQ(config.address, "https://tube.example/");
    EXPECT_EQ(config.original_url, "https://tube.example/");
    EXPECT_EQ(config.name, "Open Tube");
    config.id = "web_open-tube";

    const SiteListing list = load_website_page(config, {}, {}, {});
    ASSERT_TRUE(list.ok) << list.error;
    ASSERT_EQ(list.series.size(), 1u);
    const Series &channel = list.series[0];
    EXPECT_EQ(channel.title, "Open Anime");
    EXPECT_EQ(channel.id, "openanime-tube-example");
    EXPECT_EQ(channel.poster, "https://tube.example/lazy-static/avatars/b.png");
    EXPECT_TRUE(channel.details_pending);
    EXPECT_TRUE(TubeTransport::requested("video-channels?count=50&sort=-createdAt&start=0") ||
                TubeTransport::requested("video-channels?start=0&count=50&sort=-createdAt"));

    const SiteSeries full = load_website_series(config, channel, {}, {});
    ASSERT_TRUE(full.ok) << full.error;
    EXPECT_EQ(full.series.provider_id, "web_open-tube");
    ASSERT_EQ(full.series.seasons.size(), 1u);
    EXPECT_EQ(full.series.seasons[0].title, "Season One");
    ASSERT_EQ(full.series.seasons[0].episodes.size(), 1u);
    const Resolution r =
        make_website_provider(config, {})
            ->resolve(full.series, full.series.seasons[0], full.series.seasons[0].episodes[0], {});
    ASSERT_TRUE(r.ok) << r.detail;
    EXPECT_EQ(r.media.kind, media::SourceKind::hls);

    const SiteListing found = search_website(config, "open anime", {}, {});
    ASSERT_TRUE(found.ok) << found.error;
    ASSERT_EQ(found.series.size(), 1u);
    EXPECT_TRUE(TubeTransport::requested("searchTarget=local"));
    EXPECT_TRUE(TubeTransport::requested("host=tube.example"));
    EXPECT_TRUE(TubeTransport::requested("search=open%20anime"));
}
