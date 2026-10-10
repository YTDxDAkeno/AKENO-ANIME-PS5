// AKENO STREAM PS5 - Host screenshot tool.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Runs the real application shell on the build machine with canned API
// responses and a software decoder, drives it with controller events and
// writes each screen to build/screenshots/*.png for visual review.
#include "app/app.hpp"
#include "app/store.hpp"
#include "canned_api.hpp"
#include "core/fs.hpp"
#include "host_web_view.hpp"
#include "net/http.hpp"
#include "platform/platform.hpp"
#include "software_sink.hpp"

#include "stb/stb_image_write.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace akeno;

namespace
{
std::vector<gfx::Pixel> g_pixels(1920 * 1080);
gfx::Surface g_surface{g_pixels.data(), 1920, 1080, 1920};
std::uint64_t g_now = 1000;

void settle(App &app, int frames = 30)
{
    for (int i = 0; i < frames; ++i)
    {
        g_now += 16;
        app.update(g_now);
        if (app.jobs().pending())
            platform::sleep_us(20000);
    }
    for (int i = 0; i < 400 && app.jobs().pending(); ++i)
    {
        platform::sleep_us(10000);
        app.update(g_now += 16);
    }
    for (int i = 0; i < 20; ++i)
        app.update(g_now += 16);
}

void shot(App &app, const std::string &name)
{
    settle(app);
    // Artwork requested by the first render arrives asynchronously.
    app.render(g_surface, g_now);
    settle(app);
    app.render(g_surface, g_now);
    const std::string path = "build/screenshots/" + name + ".png";
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (!file)
        return;
    stbi_write_png_to_func([](void *context, void *data, int size)
                           { std::fwrite(data, 1, static_cast<std::size_t>(size), static_cast<std::FILE *>(context)); },
                           file, 1920, 1080, 4, g_pixels.data(), 1920 * 4);
    std::fclose(file);
    std::printf("wrote %s\n", path.c_str());
}

void press(App &app, input::Button b, int times = 1)
{
    for (int i = 0; i < times; ++i)
    {
        app.handle({b, false});
        settle(app, 4);
    }
}
} // namespace

int main()
{
    fs::make_directory("build");
    fs::make_directory("build/screenshots");
    const std::string data = "build/screenshot-data";
    fs::make_directory(data);
    for (const char *f : {"settings.json", "history.json", "favorites.json", "secrets.json",
                          "sources.json", "websites.json", "web-tests.json"})
        fs::remove_file(fs::join(data, f));
    setenv("AKENO_DATA_DIR", data.c_str(), 1);
    net::set_test_transport(test::canned_transport);
    net::Client::global_init();

    // A little history so Continue Watching has content.
    {
        Store store(data);
        store.load();
        auto streams = OpenCatalog::public_streams();
        store.record_progress(streams[0], 212.0, 596.0);
        store.record_progress(streams[2], 405.0, 888.0);
        store.toggle_favorite(streams[1]);
        store.add_source({"Example List", "https://lists.example/demo.m3u", false});
        Settings s = store.settings();
        s.web_check_first = false; // the canned network has no websites
        s.websites_notice_accepted = true;
        store.update_settings(s);
        web::WebsiteStore sites(data);
        sites.load();
        std::string why, id;
        sites.add("Crunchyroll", "https://www.crunchyroll.com/", &why, &id);
        sites.add("PeerTube", "https://framatube.org/", &why, &id);
        web::Website pinned = *sites.find(id);
        pinned.pinned = true;
        pinned.checks.loads = web::Mark::works;
        pinned.checks.video = web::Mark::works;
        sites.update(pinned);
        sites.add("Example Video Site", "https://video.example/", &why, &id);
        sites.record_visit("https://framatube.org/", "PeerTube", 1760000000);
        web::WebTestLog tests(data);
        tests.load();
        const std::string report =
            R"j({"results":[{"id":"drm.eme","group":"DRM (Encrypted Media Extensions)","name":"EME","status":"yes","detail":"present"},
            {"id":"drm.widevine","group":"DRM (Encrypted Media Extensions)","name":"Widevine","status":"no","detail":"NotSupportedError (example data)"},
            {"id":"codec.h264","group":"Video formats (canPlayType)","name":"H.264 High","status":"yes","detail":"probably"},
            {"id":"codec.vp9","group":"Video formats (canPlayType)","name":"VP9 (WebM)","status":"no","detail":"no answer"},
            {"id":"mse.available","group":"Streaming (Media Source Extensions)","name":"MediaSource","status":"yes"},
            {"id":"playback.video","group":"HTML5 playback","name":"Video plays","status":"yes","detail":"example data"}]})j";
        tests.accept_report(report, 1760000000, &why);
    }
    test::web_view_script().finish_after_updates = -1; // stays open for its screenshot

    gfx::FontEngine fonts;
    for (const auto &[weight, file] : {std::pair{gfx::Weight::regular, "Inter-Regular.ttf"},
                                       std::pair{gfx::Weight::semibold, "Inter-SemiBold.ttf"},
                                       std::pair{gfx::Weight::bold, "Inter-Bold.ttf"}})
        if (auto bytes = fs::read_bytes(std::string{"assets/fonts/"} + file))
            fonts.load(weight, std::move(*bytes));

    AppConfig config;
    config.build = "host screenshots";
    auto record = std::make_shared<test::SinkRecord>();
    App *app_ptr = nullptr;
    config.make_sink = [&app_ptr, record] { return test::make_software_sink(app_ptr->frames(), record); };
    App app(config, fonts);
    app_ptr = &app;
    app.start(g_now);
    if (app.mode() != Mode::home)
        app.switch_mode(Mode::home);

    // Home: Continue Watching, YouTube, Anime, My Websites, Discover, Local
    // Library, Recently Added Websites, Favorites.
    shot(app, "01-home");
    press(app, input::Button::down, 2);
    shot(app, "02-home-anime");
    press(app, input::Button::down, 2);
    shot(app, "35-home-discover");
    press(app, input::Button::down, 2);
    shot(app, "36-home-websites-added");
    press(app, input::Button::up, 8);

    // Modes are reached by name: the tab order has website modes in it.
    app.switch_mode(Mode::anime);
    shot(app, "03-anime");
    press(app, input::Button::right, 2);
    shot(app, "04-anime-focus");
    press(app, input::Button::left, 2);
    press(app, input::Button::cross);
    shot(app, "05-anime-details");
    press(app, input::Button::down);
    shot(app, "06-anime-details-episodes");
    press(app, input::Button::cross);
    shot(app, "07-qr");
    press(app, input::Button::circle);
    press(app, input::Button::circle);
    app.switch_mode(Mode::youtube);
    shot(app, "08-youtube-setup");
    app.store().set_youtube_api_key("AIzaSyD-test-key-0123456789abcdefghijkl");
    app.switch_mode(Mode::home);
    app.switch_mode(Mode::youtube);
    shot(app, "09-youtube");
    press(app, input::Button::triangle);
    shot(app, "10-keyboard");
    press(app, input::Button::cross, 3);
    press(app, input::Button::options);
    shot(app, "11-youtube-search");
    press(app, input::Button::cross);
    shot(app, "12-youtube-details");
    press(app, input::Button::left, 6);
    press(app, input::Button::cross); // Play: the official embedded player
    shot(app, "34-youtube-player-opening");
    press(app, input::Button::l3);
    press(app, input::Button::r3); // emergency close
    settle(app, 20);
    press(app, input::Button::circle);
    press(app, input::Button::circle);
    app.switch_mode(Mode::websites);
    shot(app, "28-websites");
    press(app, input::Button::triangle);
    shot(app, "29-websites-address");
    press(app, input::Button::circle); // cancel the keyboard
    press(app, input::Button::down);
    press(app, input::Button::square);
    shot(app, "30-website-options");
    press(app, input::Button::circle);
    press(app, input::Button::cross);
    shot(app, "31-browser-open");
    press(app, input::Button::l3);
    press(app, input::Button::r3);
    settle(app, 20);
    press(app, input::Button::down);
    press(app, input::Button::right);
    press(app, input::Button::cross);
    shot(app, "32-crunchyroll");
    press(app, input::Button::circle);
    press(app, input::Button::up, 3);
    press(app, input::Button::right, 2); // Browse > Playback Lab
    press(app, input::Button::cross);
    shot(app, "37-playback-lab");
    press(app, input::Button::circle);
    press(app, input::Button::left, 2);
    // A saved website as its own mode, with its tab after Websites.
    {
        const web::Website *peertube = app.websites().find_by_url("https://framatube.org/");
        std::string why;
        if (peertube && app.websites().set_mode(peertube->id, true, &why))
        {
            app.site_modes_changed();
            app.open_site_mode(peertube->id);
            shot(app, "38-website-mode");
        }
    }
    app.switch_mode(Mode::discover);
    shot(app, "13-discover");
    press(app, input::Button::cross);
    shot(app, "14-discover-details");
    press(app, input::Button::circle);
    app.switch_mode(Mode::library);
    shot(app, "15-library");
    press(app, input::Button::cross);
    shot(app, "16-library-files");
    app.switch_mode(Mode::sources);
    shot(app, "17-sources");
    press(app, input::Button::cross);
    shot(app, "18-source-list");
    press(app, input::Button::circle);
    press(app, input::Button::down);
    press(app, input::Button::cross);
    shot(app, "19-sources-notice");
    press(app, input::Button::cross);
    shot(app, "20-sources-keyboard");
    press(app, input::Button::circle); // cancel the keyboard
    press(app, input::Button::right);
    press(app, input::Button::cross);
    shot(app, "21-sources-phone");
    press(app, input::Button::circle);
    app.switch_mode(Mode::settings);
    shot(app, "22-settings");
    press(app, input::Button::down, 12);
    press(app, input::Button::cross);
    press(app, input::Button::right);
    press(app, input::Button::cross);
    settle(app, 120);
    shot(app, "23-diagnostics");
    press(app, input::Button::right, 2);
    press(app, input::Button::cross);
    shot(app, "33-browser-tests");
    press(app, input::Button::circle);
    press(app, input::Button::circle);
    // Play the bundled clip through the software decoder: Home's Local
    // Library row (after Continue Watching, YouTube, Anime, My Websites and
    // Discover).
    app.switch_mode(Mode::home);
    press(app, input::Button::up, 8);
    press(app, input::Button::down, 5);
    press(app, input::Button::cross);
    shot(app, "25-details-offline-clip");
    press(app, input::Button::cross);
    for (int i = 0; i < 60; ++i)
    {
        settle(app, 2);
        platform::sleep_us(5000);
    }
    shot(app, "26-player");
    press(app, input::Button::options);
    shot(app, "27-player-info");
    press(app, input::Button::circle);
    press(app, input::Button::circle);
    net::set_test_transport({});
    app.player().stop();
    return 0;
}
