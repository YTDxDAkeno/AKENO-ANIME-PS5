// AKENO STREAM PS5 - Screen factories.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/app.hpp"

#include <memory>

namespace akeno
{
std::unique_ptr<Screen> make_home_screen(App &app);
std::unique_ptr<Screen> make_anime_screen(App &app);
std::unique_ptr<Screen> make_youtube_screen(App &app);
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
} // namespace akeno
