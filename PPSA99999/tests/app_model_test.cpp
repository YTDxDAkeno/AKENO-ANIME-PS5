// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_model.hpp"
#include <cassert>
#include <string_view>
using namespace akeno;
int main() {
    Model m;
    assert(m.page == Page::home);
    assert(m.status.catalog == Capability::unavailable);
    assert(m.status.authentication == Capability::unavailable);
    assert(m.status.protected_video == Capability::unavailable);
    m.set_controller(true);
    assert(m.controller_connected);
    assert(m.status.controller == Capability::ready);
    m.navigate(Nav::tab_right);
    assert(m.page == Page::catalog);
    m.navigate(Nav::tab_left);
    assert(m.page == Page::home);
    m.navigate(Nav::tab_left);
    assert(m.page == Page::diagnostics);
    m.navigate(Nav::down);
    assert(m.focus == 1);
    m.navigate(Nav::confirm);
    assert(m.details);
    m.navigate(Nav::tab_right);
    assert(m.page == Page::diagnostics); // modal captures input
    m.navigate(Nav::back);
    assert(!m.details);
    m.navigate(Nav::back);
    assert(m.page == Page::home);
    assert(m.focus == 0);
    m.set_controller(false);
    assert(m.status.controller == Capability::unavailable);
    assert(page_title(Page::account) == std::string_view{"KONTO"});
    return 0;
}
