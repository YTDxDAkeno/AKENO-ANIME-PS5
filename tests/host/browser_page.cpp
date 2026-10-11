// AKENO STREAM PS5 - Runs AKENO's video-only page in a real browser (Playwright).
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "browser_page.hpp"

#include <sys/wait.h>

#include <cstdio>
#include <cstdlib>

namespace akeno::test
{
bool browser_tests_enabled() noexcept
{
    const char *on = std::getenv("AKENO_BROWSER_TESTS");
    return on && std::string{on} == "1";
}

int run_browser_page(const std::string &scenario, const std::string &address, std::string *output)
{
    // Addresses come from the page server ("http://127.0.0.1:<port>/s/<hex>/watch?..."),
    // never with a quote; refuse anything else rather than quote it.
    if (address.find('\'') != std::string::npos || scenario.find('\'') != std::string::npos)
        return -1;
    const char *root = std::getenv("AKENO_SOURCE_ROOT");
    const std::string command = "node '" + std::string{root ? root : "."} +
                                "/tests/browser/video_only.cjs' '" + scenario + "' '" + address +
                                "' 2>&1";
    FILE *pipe = popen(command.c_str(), "r");
    if (!pipe)
        return -1;
    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe))
        *output += buffer;
    const int status = pclose(pipe);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
} // namespace akeno::test
