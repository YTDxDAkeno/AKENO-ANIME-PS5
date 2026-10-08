// Akeno Anime - offline-first PS5 homebrew UI model.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string_view>

namespace akeno {

enum class Page : std::uint8_t { home, catalog, library, account, diagnostics };
enum class Nav : std::uint8_t { tab_left, tab_right, up, down, confirm, back };
enum class Capability : std::uint8_t { ready, unavailable, unverified };

struct RuntimeStatus {
    Capability graphics = Capability::unverified;
    Capability controller = Capability::unverified;
    Capability catalog = Capability::unavailable;
    Capability authentication = Capability::unavailable;
    Capability protected_video = Capability::unavailable;
};

struct Model {
    Page page = Page::home;
    unsigned focus = 0;
    bool details = false;
    bool controller_connected = false;
    RuntimeStatus status{};

    void navigate(Nav action) noexcept;
    void set_controller(bool connected) noexcept;
    static constexpr unsigned page_count = 5;
    static constexpr unsigned row_count = 3;
};

std::string_view page_title(Page page) noexcept;
std::string_view capability_label(Capability value) noexcept;

} // namespace akeno
