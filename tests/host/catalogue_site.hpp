// AKENO STREAM PS5 - An offline catalogue-only website at a known site's address.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Answers https://www.miruro.to through the net test transport with a few
// small, made-up pages: schema.org TVSeries in an ItemList on the homepage,
// and series pages that list no seasons or episodes - the shape AKENO
// STREAM 1.1.0-rc.1 found on the console (49 series, 0 seasons, 0 episodes).
// None of it is Miruro's data: titles, descriptions and paths are invented
// for the tests, and nothing here is fetched from the internet.
#pragma once

#include "net/http.hpp"

#include <optional>
#include <string>
#include <vector>

namespace akeno::test
{
inline constexpr char kCatalogueSite[] = "https://www.miruro.to";

// The answer for a request to kCatalogueSite; nullopt for any other address.
std::optional<net::Response> serve_catalogue_site(const net::Request &request);
// Paths (with their query) the site was asked for since the last clear.
std::vector<std::string> catalogue_site_requests();
void clear_catalogue_site_requests();
} // namespace akeno::test
