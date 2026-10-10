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
                          "sources.json"})
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
    }

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

    shot(app, "01-home");
    press(app, input::Button::down);
    press(app, input::Button::right, 1);
    shot(app, "02-home-explore");
    press(app, input::Button::r1);
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
    press(app, input::Button::r1);
    shot(app, "08-youtube-setup");
    app.store().set_youtube_api_key("AIzaSyD-test-key-0123456789abcdefghijkl");
    press(app, input::Button::l1);
    press(app, input::Button::r1);
    shot(app, "09-youtube");
    press(app, input::Button::triangle);
    shot(app, "10-keyboard");
    press(app, input::Button::cross, 3);
    press(app, input::Button::options);
    shot(app, "11-youtube-search");
    press(app, input::Button::circle);
    press(app, input::Button::r1);
    shot(app, "12-library");
    press(app, input::Button::cross);
    shot(app, "13-library-files");
    press(app, input::Button::r1);
    shot(app, "14-sources");
    press(app, input::Button::cross);
    shot(app, "15-source-list");
    press(app, input::Button::circle);
    press(app, input::Button::down);
    press(app, input::Button::cross);
    shot(app, "16-sources-notice");
    press(app, input::Button::cross);
    shot(app, "17-sources-keyboard");
    press(app, input::Button::circle); // cancel the keyboard
    press(app, input::Button::r1);
    shot(app, "18-settings");
    press(app, input::Button::down, 9);
    press(app, input::Button::cross);
    press(app, input::Button::right);
    press(app, input::Button::cross);
    settle(app, 120);
    shot(app, "19-diagnostics");
    press(app, input::Button::circle);
    press(app, input::Button::up);
    press(app, input::Button::cross);
    press(app, input::Button::cross);
    press(app, input::Button::cross);
    press(app, input::Button::cross);
    shot(app, "20-crunchyroll-status");
    press(app, input::Button::circle);
    // Play the bundled clip through the software decoder.
    press(app, input::Button::r1); // home, focus still on the Explore row
    press(app, input::Button::down, 2);
    press(app, input::Button::cross);
    shot(app, "21-details-offline-clip");
    press(app, input::Button::cross);
    for (int i = 0; i < 60; ++i)
    {
        settle(app, 2);
        platform::sleep_us(5000);
    }
    shot(app, "22-player");
    press(app, input::Button::options);
    shot(app, "23-player-info");
    press(app, input::Button::circle);
    press(app, input::Button::circle);
    net::set_test_transport({});
    app.player().stop();
    return 0;
}
