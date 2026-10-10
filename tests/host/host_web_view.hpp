// AKENO STREAM PS5 - Scripted stand-in for the console's embedded browser.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The host build has no system browser. This stand-in follows the console
// implementation's state machine (prepare, open, running, finished) and lets
// tests decide how it behaves and see what the app asked it to open.
#pragma once

#include "platform/web_view.hpp"

#include <string>
#include <vector>

namespace akeno::test
{
struct WebViewScript
{
    bool available = true;
    std::string unavailable_reason = "no system browser on the build machine";
    bool refuse_open = false;
    // Updates after which the "user" closes the browser; -1 keeps it open
    // until the app closes it.
    int finish_after_updates = 30;
    int result_code = 0;
    std::vector<platform::WebOpenRequest> opened;
    int closes_requested = 0;
};

// The script every stand-in consults. Tests reset it with reset_web_view_script().
WebViewScript &web_view_script();
void reset_web_view_script();
} // namespace akeno::test
