// AKENO STREAM PS5 - The offline fixture website for website-provider tests and screenshots.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// tests/fixtures/website-schema with AKENO's self-test clips, answered at
// https://anime.fixture.test through the net test transport: pages, artwork,
// subtitles and video files, nothing from the internet. Range requests are
// answered with the whole file, like a server without range support.
#pragma once

#include "net/http.hpp"

#include <optional>
#include <string>
#include <vector>

namespace akeno::test
{
inline constexpr char kFixtureSite[] = "https://anime.fixture.test";

// Copies the site and the clips into a fresh temporary folder; returns it.
std::string make_fixture_site(const std::string &source_root);
// The answer for a request to kFixtureSite; nullopt for any other address.
std::optional<net::Response> serve_fixture_site(const std::string &site_root,
                                                const net::Request &request);
// Paths (with their query) the site was asked for since the last clear.
std::vector<std::string> fixture_site_requests();
void clear_fixture_site_requests();
} // namespace akeno::test
