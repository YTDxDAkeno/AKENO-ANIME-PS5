// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_model.hpp"

namespace akeno {
void Model::set_controller(bool connected) noexcept {
    controller_connected = connected;
    status.controller = connected ? Capability::ready : Capability::unavailable;
}

void Model::navigate(Nav action) noexcept {
    if (details) {
        if (action == Nav::back || action == Nav::confirm) details = false;
        return;
    }
    switch (action) {
    case Nav::tab_left: {
        const auto p = static_cast<unsigned>(page);
        page = static_cast<Page>((p + page_count - 1) % page_count);
        focus = 0;
        break;
    }
    case Nav::tab_right: {
        const auto p = static_cast<unsigned>(page);
        page = static_cast<Page>((p + 1) % page_count);
        focus = 0;
        break;
    }
    case Nav::up: focus = (focus + row_count - 1) % row_count; break;
    case Nav::down: focus = (focus + 1) % row_count; break;
    case Nav::confirm: details = true; break;
    case Nav::back: page = Page::home; focus = 0; break;
    }
}

std::string_view page_title(Page page) noexcept {
    switch (page) {
    case Page::home: return "START";
    case Page::catalog: return "CATALOG";
    case Page::library: return "MY LIST";
    case Page::account: return "ACCOUNT";
    case Page::diagnostics: return "DIAGNOSTICS";
    }
    return "UNKNOWN";
}

std::string_view capability_label(Capability value) noexcept {
    switch (value) {
    case Capability::ready: return "READY";
    case Capability::unavailable: return "UNAVAILABLE";
    case Capability::unverified: return "NOT TESTED";
    }
    return "UNKNOWN";
}
} // namespace akeno
