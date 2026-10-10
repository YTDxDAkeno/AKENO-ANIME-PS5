// AKENO STREAM PS5 - Random controller input through the whole application.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drives the real App with thousands of random button presses (canned network
// answers, software decoder) and renders as it goes, under the sanitizers.
// 0.4.1 crashed on the console as soon as a seventh character was typed into
// the key field; a run like this reaches such paths without anyone planning
// them.
#include "app/app.hpp"
#include "canned_api.hpp"
#include "core/fs.hpp"
#include "net/http.hpp"
#include "platform/platform.hpp"
#include "software_sink.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <random>
#include <string>
#include <unistd.h>
#include <vector>

namespace
{
using namespace akeno;

void load_fonts(gfx::FontEngine &fonts)
{
    for (const auto &[weight, file] : {std::pair{gfx::Weight::regular, "Inter-Regular.ttf"},
                                       std::pair{gfx::Weight::semibold, "Inter-SemiBold.ttf"},
                                       std::pair{gfx::Weight::bold, "Inter-Bold.ttf"}})
        if (auto bytes = fs::read_bytes(fs::join(platform::app_dir(), "assets/fonts/") + file))
            fonts.load(weight, std::move(*bytes));
}

void run_monkey(unsigned seed, int steps)
{
    char dir_template[] = "/tmp/akeno-monkey-XXXXXX";
    ASSERT_NE(mkdtemp(dir_template), nullptr);
    setenv("AKENO_DATA_DIR", dir_template, 1);
    net::set_test_transport(test::canned_transport);

    gfx::FontEngine fonts;
    load_fonts(fonts);
    std::vector<gfx::Pixel> pixels(1920 * 1080);
    gfx::Surface surface{pixels.data(), 1920, 1080, 1920};

    AppConfig config;
    config.build = "monkey";
    auto record = std::make_shared<test::SinkRecord>();
    App *app_ptr = nullptr;
    config.make_sink = [&app_ptr, record]
    { return test::make_software_sink(app_ptr->frames(), record); };
    {
        App app(config, fonts);
        app_ptr = &app;
        std::uint64_t now = 1000;
        app.start(now);

        // Navigation and confirmation dominate, as with a real player.
        const input::Button weighted[] = {
            input::Button::up,     input::Button::down,    input::Button::left,
            input::Button::right,  input::Button::up,      input::Button::down,
            input::Button::left,   input::Button::right,   input::Button::cross,
            input::Button::cross,  input::Button::cross,   input::Button::circle,
            input::Button::circle, input::Button::square,  input::Button::triangle,
            input::Button::l1,     input::Button::r1,      input::Button::l2,
            input::Button::r2,     input::Button::options, input::Button::touchpad,
            input::Button::l3,     input::Button::r3,
        };
        // With a keyboard open, type like a person: mostly confirm and move,
        // sometimes delete or switch pages, rarely leave.
        const input::Button typing[] = {
            input::Button::cross, input::Button::cross,  input::Button::cross,
            input::Button::cross, input::Button::right,  input::Button::left,
            input::Button::up,    input::Button::down,   input::Button::r1,
            input::Button::l1,    input::Button::square, input::Button::triangle,
        };
        std::mt19937 random(seed);
        for (int step = 0; step < steps; ++step)
        {
            input::Button b = weighted[random() % std::size(weighted)];
            if (app.keyboard_active() && random() % 50 != 0)
                b = typing[random() % std::size(typing)];
            app.handle({b, random() % 5 == 0});
            now += 16 + random() % 200;
            app.update(now);
            if (step % 3 == 0 || app.needs_redraw())
                app.render(surface, now);
            if (app.jobs().pending() && step % 10 == 0)
                platform::sleep_us(2000);
        }
        app.player().stop();
    }
    net::set_test_transport({});
}
} // namespace

TEST(UiMonkey, SurvivesRandomControllerInput)
{
    for (unsigned seed : {1u, 2u, 3u})
        run_monkey(seed, 800);
}

TEST(UiMonkey, SurvivesTypingSecretsAndAddresses)
{
    // Long runs of confirm presses fill keyboards (keys, searches, addresses).
    for (unsigned seed : {77u, 78u})
        run_monkey(seed, 800);
}
