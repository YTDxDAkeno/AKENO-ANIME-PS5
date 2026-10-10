// AKENO STREAM PS5 - Screen factories.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/app.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace akeno
{
std::unique_ptr<Screen> make_home_screen(App &app);
std::unique_ptr<Screen> make_anime_screen(App &app);
std::unique_ptr<Screen> make_youtube_screen(App &app);
std::unique_ptr<Screen> make_discover_screen(App &app);
std::unique_ptr<Screen> make_library_screen(App &app);
std::unique_ptr<Screen> make_settings_screen(App &app);
std::unique_ptr<Screen> make_details_screen(App &app, MediaItem item, Provider *provider,
                                            ui::Pixel accent);
std::unique_ptr<Screen> make_search_screen(App &app, Provider &provider, ui::Pixel accent,
                                           std::string title);
std::unique_ptr<Screen> make_player_screen(App &app, MediaItem item);
std::unique_ptr<Screen> make_qr_screen(App &app, MediaItem item);
std::unique_ptr<Screen> make_status_screen(App &app, ProviderInfo info);
std::unique_ptr<Screen> make_diagnostics_screen(App &app);
std::unique_ptr<Screen> make_licenses_screen(App &app);
std::unique_ptr<Screen> make_open_streams_screen(App &app);
std::unique_ptr<Screen> make_sources_screen(App &app);
std::unique_ptr<Screen> make_source_screen(App &app, SourceEntry entry);
// A modal question: Cross runs on_confirm, Circle closes.
std::unique_ptr<Screen> make_confirm_screen(App &app, std::string title, std::string message,
                                            std::string confirm_label,
                                            std::function<void()> on_confirm);

// Websites mode, the embedded browser and its tests (screen_websites.cpp,
// screen_browser.cpp).
std::unique_ptr<Screen> make_websites_screen(App &app);
std::unique_ptr<Screen> make_browser_screen(App &app, WebSession session);
std::unique_ptr<Screen> make_browser_tests_screen(App &app);
std::unique_ptr<Screen> make_crunchyroll_screen(App &app);

// A modal list of choices: Cross runs one (the menu closes first), Circle closes.
struct MenuOption
{
    std::string label;
    std::string detail;
    std::function<void()> run;
};
std::unique_ptr<Screen> make_menu_screen(App &app, std::string title, std::string subtitle,
                                         std::vector<MenuOption> options);

// Recording what worked: each row cycles Not tested / Works / Does not work.
struct RecordRow
{
    std::string label;
    std::string hint;
    web::Mark mark = web::Mark::untested;
};
std::unique_ptr<Screen>
make_record_screen(App &app, std::string title, std::string note, std::vector<RecordRow> rows,
                   std::function<void(const std::vector<RecordRow> &)> save);
} // namespace akeno
