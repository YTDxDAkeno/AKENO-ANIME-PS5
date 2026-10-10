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
#include "gfx/image_decode.hpp"
#include "media/native/native_sink.hpp"
#include "net/http.hpp"
#include "platform/display.hpp"
#include "platform/pad.hpp"
#include "platform/platform.hpp"

#include <string>
#include <string_view>
#include <vector>

using namespace akeno;

namespace
{
// Returning from main ends this launch context badly; stay alive instead.
[[noreturn]] void halt(const std::string &message)
{
    platform::notify(message);
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

// The launch artwork (the same picture as sce_sys/pic1.dds) behind the startup
// steps. Missing or unreadable artwork leaves the plain text splash.
gfx::Image load_splash_art()
{
    const std::string path = fs::join(platform::app_dir(), "assets/brand/akeno-banner-1920.jpg");
    if (auto bytes = fs::read_bytes(path, gfx::kMaxImageBytes))
    {
        gfx::Image art = gfx::decode_image(bytes->data(), bytes->size());
        if (art.valid() && art.width == platform::Display::kWidth &&
            art.height == platform::Display::kHeight)
            return art;
    }
    return {};
}

void draw_splash(gfx::Surface &surface, gfx::FontEngine &fonts, const gfx::Image &art,
                 std::string_view status)
{
    if (art.valid())
    {
        // The artwork carries the AKENO STREAM logo; progress sits below it.
        surface.draw_image(art, 0, 0);
        surface.dim({0, 900, surface.width(), surface.height() - 900}, 150);
        fonts.draw(surface, 100, 952, status, {30, gfx::Weight::regular, gfx::hex(0xe8eaf3)});
        fonts.draw(surface, 100, 1004, kAppVersion, {22, gfx::Weight::regular, gfx::hex(0xb6bdd0)});
        return;
    }
    surface.clear(gfx::hex(0x0b0e17));
    fonts.draw(surface, 96, 430, "AKENO STREAM", {72, gfx::Weight::bold, gfx::hex(0xffffff)});
    fonts.draw(surface, 100, 540, status, {30, gfx::Weight::regular, gfx::hex(0x9aa3b8)});
    fonts.draw(surface, 100, 1000, kAppVersion, {22, gfx::Weight::regular, gfx::hex(0x5d6680)});
}

// One startup step: named for the crash report and shown on the splash.
void step(platform::Display &display, gfx::Surface &surface, gfx::FontEngine &fonts,
          const gfx::Image &art, const char *stage, std::string_view status)
{
    platform::set_stage(stage);
    draw_splash(surface, fonts, art, status);
    display.present(surface);
}
} // namespace

int main()
{
    // First: anything that goes wrong from here on is reported with its stage.
    platform::install_crash_reporter();
    platform::set_stage("startup: display");
    platform::notify(std::string{"AKENO STREAM "} + kAppVersion + " starting");

    // The display comes up first, in the same order as the earlier versions
    // that ran on the console, so every later step can show progress.
    platform::Display display;
    std::string error;
    if (!display.open(&error))
        halt("AKENO STREAM could not open the display: " + error);

    platform::set_stage("startup: frame buffer");
    std::vector<gfx::Pixel> pixels(static_cast<std::size_t>(platform::Display::kWidth) *
                                   platform::Display::kHeight);
    gfx::Surface surface{pixels.data(), platform::Display::kWidth, platform::Display::kHeight,
                         platform::Display::kWidth};
    gfx::FontEngine fonts; // the built-in pixel font until the typefaces load
    gfx::Image splash;     // the launch artwork once loaded (freed before the app starts)
    step(display, surface, fonts, splash, "startup: fonts", "Loading fonts...");
    load_fonts(fonts);
    platform::set_stage("startup: artwork");
    splash = load_splash_art();

    step(display, surface, fonts, splash, "startup: network", "Starting network...");
    net::Client::global_init();

    step(display, surface, fonts, splash, "startup: controller", "Connecting the controller...");
    platform::Pad pad;
    (void)pad.open();

    step(display, surface, fonts, splash, "startup: interface", "Preparing the interface...");
    splash = {};
    AppConfig config;
    config.version = kAppVersion;
    config.build = build_label();
    App *app_ptr = nullptr;
    config.make_sink = [&app_ptr] { return media::make_native_sink(app_ptr->frames()); };
    config.set_volume = [](int percent)
    { media::set_native_volume(static_cast<unsigned>(percent < 0 ? 0 : percent)); };

    App app(config, fonts);
    app_ptr = &app;
    platform::set_stage("startup: settings and history");
    app.start(platform::monotonic_us() / 1000);
    platform::set_stage("startup: first frame");
    app.render(surface, platform::monotonic_us() / 1000);
    display.present(surface);
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
