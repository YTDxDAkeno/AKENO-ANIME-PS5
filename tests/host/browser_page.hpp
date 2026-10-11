// AKENO STREAM PS5 - Runs AKENO's video-only page in a real browser (Playwright).
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// tests/browser/video_only.cjs with Node.js and Playwright, opt-in with
// AKENO_BROWSER_TESTS=1 (NODE_PATH pointing at the directory that holds the
// playwright package). Used by BrowserPage.* and the real-network test.
#pragma once

#include <string>

namespace akeno::test
{
bool browser_tests_enabled() noexcept;
// Runs one scenario against the page at address; returns the script's exit
// code (0: every check passed) and appends its output.
int run_browser_page(const std::string &scenario, const std::string &address, std::string *output);
} // namespace akeno::test
