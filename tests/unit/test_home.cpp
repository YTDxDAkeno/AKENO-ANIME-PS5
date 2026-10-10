// AKENO STREAM PS5 - Home's rows.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The row composition (order, hidden empty rows, websites, the local
// library) and Home inside the real App: its cards open what they say, also
// without a network connection.
#include "app/app.hpp"
#include "app/home.hpp"
#include "core/fs.hpp"
#include "host_web_view.hpp"
#include "net/http.hpp"
#include "platform/platform.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace akeno;

namespace
{
std::string fresh_dir(const char *name)
{
    char path[128];
    std::snprintf(path, sizeof(path), "/tmp/akeno-home-%s-XXXXXX", name);
    if (!mkdtemp(path))
        return "/tmp";
    return path;
}

MediaItem item(const std::string &provider, const std::string &id)
{
    MediaItem m;
    m.provider = provider;
    m.id = id;
    m.kind = ItemKind::video;
    m.title = provider + " " + id;
    return m;
}

HistoryEntry watched(const MediaItem &m)
{
    HistoryEntry e;
    e.item = m;
    return e;
}

web::Website site(const std::string &id, const std::string &name, std::uint64_t added,
                  bool pinned = false, bool private_site = false)
{
    web::Website w;
    w.id = id;
    w.name = name;
    w.url = "https://" + id + ".example/";
    w.added = added;
    w.pinned = pinned;
    w.private_site = private_site;
    return w;
}

std::vector<std::string> titles(const std::vector<Shelf> &rows)
{
    std::vector<std::string> out;
    for (const Shelf &s : rows)
        out.push_back(s.title);
    return out;
}

const Shelf *row(const std::vector<Shelf> &rows, const std::string &title)
{
    for (const Shelf &s : rows)
        if (s.title == title)
            return &s;
    return nullptr;
}

std::vector<std::string> keys(const Shelf &s)
{
    std::vector<std::string> out;
    for (const MediaItem &m : s.items)
        out.push_back(m.key());
    return out;
}
} // namespace

TEST(Home, ShowsTheRowsInOrder)
{
    HomeContent c;
    c.continue_watching = {watched(item("open", "a"))};
    c.history = c.continue_watching;
    c.youtube = {item("youtube", "v1")};
    c.youtube_configured = true;
    c.anime = {item("anilist", "1")};
    c.discover = {item("peertube", "p1")};
    c.open_streams = {item("open", "movie")};
    c.local_files = {item("local", "/app0/media/a.mp4")};
    c.bundled = {item("open", "bundled-av-sync")};
    c.websites = {site("one", "One", 10, true)};
    c.favorites = {item("open", "fav")};
    const auto rows = home_shelves(c);
    EXPECT_EQ(titles(rows), (std::vector<std::string>{"Continue Watching", "YouTube", "Anime",
                                                      "My Websites", "Discover", "Local Library",
                                                      "Recently Added Websites", "Favorites"}));
    EXPECT_TRUE(row(rows, "Anime")->portrait); // AniList posters
}

TEST(Home, HidesEmptyRows)
{
    // A first start without a network: no history, websites, files or favourites.
    const auto rows = home_shelves(HomeContent{});
    EXPECT_EQ(titles(rows), (std::vector<std::string>{"YouTube", "Anime", "Discover"}));
    // Rows that lead somewhere keep their cards when their content is missing.
    EXPECT_EQ(keys(*row(rows, "YouTube")),
              (std::vector<std::string>{"action:yt-link", "action:yt-site", "mode:youtube"}));
    EXPECT_EQ(keys(*row(rows, "Anime")), (std::vector<std::string>{"mode:crunchyroll"}));
    EXPECT_FALSE(row(rows, "Anime")->portrait);
    EXPECT_EQ(keys(*row(rows, "Discover")), (std::vector<std::string>{"mode:open"}));

    HomeContent offline;
    offline.bundled = {item("open", "bundled-av-sync")};
    EXPECT_EQ(titles(home_shelves(offline)),
              (std::vector<std::string>{"YouTube", "Anime", "Discover", "Local Library"}));
}

TEST(Home, YouTubeRowStartsWithWhatWasWatched)
{
    HomeContent c;
    c.history = {watched(item("youtube", "seen")), watched(item("open", "other"))};
    c.youtube = {item("youtube", "trend"), item("youtube", "seen")};
    c.youtube_configured = true;
    const auto rows = home_shelves(c);
    // Watched first, duplicates once, no setup card once a key is saved.
    EXPECT_EQ(keys(*row(rows, "YouTube")),
              (std::vector<std::string>{"youtube:seen", "youtube:trend", "action:yt-link",
                                        "action:yt-site"}));
}

TEST(Home, WebsitesRows)
{
    HomeContent c;
    c.websites = {site("old", "Old", 100, true), site("hidden", "Hidden", 300, true, true),
                  site("new", "New", 200), site("noclock", "No clock", 0),
                  site("noclock2", "No clock 2", 0)};
    const auto rows = home_shelves(c);
    // Pinned sites, never private ones.
    EXPECT_EQ(keys(*row(rows, "My Websites")), (std::vector<std::string>{"website:old"}));
    // Newest first; sites without a clock by the order they were added, newest first.
    EXPECT_EQ(keys(*row(rows, "Recently Added Websites")),
              (std::vector<std::string>{"website:new", "website:old", "website:noclock2",
                                        "website:noclock"}));
    // The card carries the site's own badge for Home.
    EXPECT_EQ(row(rows, "My Websites")->items[0].badge, "HOME");

    HomeContent many;
    for (int i = 0; i < 15; ++i)
        many.websites.push_back(site("s" + std::to_string(i), "S", 1000 + i));
    const auto recent = home_shelves(many);
    ASSERT_NE(row(recent, "Recently Added Websites"), nullptr);
    EXPECT_EQ(row(recent, "Recently Added Websites")->items.size(), kHomeRecentWebsites);
    EXPECT_EQ(row(recent, "Recently Added Websites")->items[0].id, "s14");
    EXPECT_EQ(row(recent, "My Websites"), nullptr);
}

TEST(Home, LocalLibraryRow)
{
    HomeContent c;
    c.history = {watched(item("local", "/mnt/usb0/film.mkv")), watched(item("youtube", "x"))};
    c.local_files = {item("local", "/app0/media/a.mp4"), item("local", "/mnt/usb0/film.mkv")};
    c.bundled = {item("open", "bundled-av-sync")};
    const auto rows = home_shelves(c);
    EXPECT_EQ(keys(*row(rows, "Local Library")),
              (std::vector<std::string>{"local:/mnt/usb0/film.mkv", "local:/app0/media/a.mp4",
                                        "open:bundled-av-sync"}));
}

TEST(Home, RowsAreLimited)
{
    HomeContent c;
    for (int i = 0; i < 40; ++i)
        c.favorites.push_back(item("open", std::to_string(i)));
    const auto rows = home_shelves(c);
    EXPECT_EQ(row(rows, "Favorites")->items.size(), kHomeRowItems);
}

TEST(Home, PicksTheFirstRowsOfFrontPages)
{
    ShelvesResult anime;
    anime.ok = true;
    anime.shelves = {{"Empty", {}, false},
                     {"Trending", {item("anilist", "1"), item("anilist", "2")}, true}};
    EXPECT_EQ(first_row(anime, 1).size(), 1u);
    EXPECT_EQ(first_row(anime, 10)[1].id, "2");
    EXPECT_TRUE(first_row(ShelvesResult{}, 10).empty());

    ShelvesResult discover;
    discover.ok = true;
    discover.shelves = {{"PeerTube", {item("peertube", "p1"), item("peertube", "p2")}, false},
                        {"More PeerTube", {item("peertube", "p3")}, false},
                        {"Archive", {item("archive", "a1"), item("archive", "a2")}, false}};
    const auto mixed = discover_row(discover, 3);
    ASSERT_EQ(mixed.size(), 3u);
    EXPECT_EQ(mixed[0].id, "p1");
    EXPECT_EQ(mixed[1].id, "a1");
    EXPECT_EQ(mixed[2].id, "p2");
}

TEST(Home, FindsVideoFilesInAFolder)
{
    const std::string dir = fresh_dir("media");
    for (const char *name : {"b.mp4", "a.mkv", "notes.txt", ".hidden.mp4", "clip.ts"})
        ASSERT_TRUE(fs::write_atomic(fs::join(dir, name), "x"));
    fs::make_directory(fs::join(dir, "folder.mp4"));
    const auto items = local_media_items(dir, 10);
    std::vector<std::string> names;
    for (const auto &m : items)
        names.push_back(m.title);
    EXPECT_EQ(names, (std::vector<std::string>{"a.mkv", "b.mp4", "clip.ts"}));
    ASSERT_FALSE(items.empty());
    EXPECT_EQ(items[0].provider, "local");
    EXPECT_EQ(items[0].id, fs::join(dir, "a.mkv"));
    ASSERT_TRUE(items[0].playable.has_value());
    EXPECT_EQ(items[0].playable->kind, media::SourceKind::local_file);
    EXPECT_EQ(local_media_items(dir, 2).size(), 2u);
    EXPECT_TRUE(local_media_items(fs::join(dir, "missing"), 10).empty());
}

// Home inside the App, offline: the first row is YouTube, its first card
// asks for a link; a pinned website opens in the browser from My Websites.
TEST(Home, CardsOpenWhatTheySayOffline)
{
    const std::string dir = fresh_dir("app");
    setenv("AKENO_DATA_DIR", dir.c_str(), 1);
    {
        web::WebsiteStore sites(dir);
        sites.load();
        std::string why, id;
        ASSERT_TRUE(sites.add("Pinned Site", "https://pinned.example/", &why, &id)) << why;
        web::Website w = *sites.find(id);
        w.pinned = true;
        ASSERT_TRUE(sites.update(w));
        Store store(dir);
        store.load();
        Settings s = store.settings();
        s.web_check_first = false;
        store.update_settings(s);
    }
    net::set_test_transport(
        [](const net::Request &)
        {
            net::Response e;
            e.outcome = net::Outcome::network_error;
            e.curl_code = 6;
            return e;
        });
    test::reset_web_view_script();
    gfx::FontEngine fonts;
    std::vector<gfx::Pixel> pixels(1920 * 1080);
    gfx::Surface surface{pixels.data(), 1920, 1080, 1920};
    {
        App app(AppConfig{}, fonts);
        std::uint64_t now = 1000;
        app.start(now);
        ASSERT_EQ(app.mode(), Mode::home);
        const auto step = [&](int frames)
        {
            for (int i = 0; i < frames; ++i)
            {
                now += 16;
                app.update(now);
                if (app.needs_redraw())
                    app.render(surface, now);
                platform::sleep_us(2000);
            }
        };
        step(5);
        for (int i = 0; i < 300 && app.jobs().pending() > 0; ++i)
            step(1);
        step(3);
        // Row 1: YouTube - "Play a YouTube Link" opens the keyboard.
        app.handle({input::Button::cross, false});
        step(2);
        EXPECT_TRUE(app.keyboard_active());
        app.handle({input::Button::circle, false}); // cancel
        step(2);
        EXPECT_FALSE(app.keyboard_active());
        // Row 3: My Websites (YouTube, Anime above it).
        app.handle({input::Button::down, false});
        step(2);
        app.handle({input::Button::down, false});
        step(2);
        app.handle({input::Button::cross, false});
        auto &script = test::web_view_script();
        for (int i = 0; i < 300 && script.opened.empty(); ++i)
            step(1);
        ASSERT_EQ(script.opened.size(), 1u);
        EXPECT_EQ(script.opened[0].url, "https://pinned.example/");
        for (int i = 0; i < 300 && app.browser_active(); ++i)
            step(1);
        EXPECT_FALSE(app.browser_active());
    }
    net::set_test_transport({});
    test::reset_web_view_script();
}
