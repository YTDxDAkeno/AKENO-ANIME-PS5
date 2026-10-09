// AKENO STREAM PS5 - Console entry point.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Opens the display and controller, then runs the application loop: read the
// controller, update, redraw only when something changed, and pace on the
// display's vertical blank. Video decoding runs on the player's own threads;
// this loop only draws the latest decoded picture.
#include "app/app.hpp"
#include "app/version.hpp"
#include "core/fs.hpp"
#include "media/native/native_sink.hpp"
#include "net/http.hpp"
#include "platform/display.hpp"
#include "platform/pad.hpp"
#include "platform/platform.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace akeno;

namespace
{
// Returning from main ends this launch context badly; stay alive instead.
[[noreturn]] void halt(const std::string &message)
{
    platform::notify(message);
    std::fprintf(stderr, "[akeno] %s\n", message.c_str());
    for (;;)
        platform::sleep_us(1000000);
}

void load_fonts(gfx::FontEngine &fonts)
{
    const std::string dir = fs::join(platform::app_dir(), "assets/fonts");
    const struct
    {
        gfx::Weight weight;
        const char *file;
    } faces[] = {{gfx::Weight::regular, "Inter-Regular.ttf"},
                 {gfx::Weight::semibold, "Inter-SemiBold.ttf"},
                 {gfx::Weight::bold, "Inter-Bold.ttf"}};
    for (const auto &face : faces)
        if (auto bytes = fs::read_bytes(fs::join(dir, face.file), 8u << 20))
            fonts.load(face.weight, std::move(*bytes));
}

std::string build_label()
{
    // tools/build.sh writes this for CI and pull-request builds; releases have none.
    if (auto text = fs::read_text(fs::join(platform::app_dir(), "build-label.txt"), 256))
    {
        std::string label = *text;
        while (!label.empty() &&
               (label.back() == '\n' || label.back() == '\r' || label.back() == ' '))
            label.pop_back();
        if (!label.empty())
            return label;
    }
    return "release";
}
} // namespace

int main()
{
    net::Client::global_init();

    gfx::FontEngine fonts;
    load_fonts(fonts);

    platform::Display display;
    std::string error;
    if (!display.open(&error))
        halt("AKENO STREAM could not open the display: " + error);

    std::vector<gfx::Pixel> pixels(static_cast<std::size_t>(platform::Display::kWidth) *
                                   platform::Display::kHeight);
    gfx::Surface surface{pixels.data(), platform::Display::kWidth, platform::Display::kHeight,
                         platform::Display::kWidth};

    platform::Pad pad;
    (void)pad.open();

    AppConfig config;
    config.version = kAppVersion;
    config.build = build_label();
    App *app_ptr = nullptr;
    config.make_sink = [&app_ptr] { return media::make_native_sink(app_ptr->frames()); };
    config.set_volume = [](int percent)
    { media::set_native_volume(static_cast<unsigned>(percent < 0 ? 0 : percent)); };
    config.log = [](const std::string &message)
    { std::fprintf(stderr, "[akeno] %s\n", message.c_str()); };

    App app(config, fonts);
    app_ptr = &app;
    app.start(platform::monotonic_us() / 1000);
    platform::notify(std::string{"AKENO STREAM "} + kAppVersion + " ready");

    std::vector<input::Event> events;
    events.reserve(64);
    for (;;)
    {
        const std::uint64_t now_ms = platform::monotonic_us() / 1000;
        events.clear();
        pad.poll(now_ms, events);
        app.set_controller_connected(pad.connected());
        for (const auto &event : events)
            app.handle(event);
        app.update(now_ms);
        if (app.needs_redraw())
        {
            const std::uint64_t start = platform::monotonic_us();
            app.render(surface, now_ms);
            const auto render_us = static_cast<std::uint32_t>(platform::monotonic_us() - start);
            display.present(surface);
            app.set_display_stats(display.presented_frames(), render_us, display.last_present_us());
        }
        display.wait_vblank();
    }
}
