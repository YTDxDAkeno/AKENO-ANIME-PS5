// AKENO STREAM PS5 - Discover: open video platforms playable in the app.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Joins PeerTube and the Internet Archive behind one browse and search
// surface; details and playback go to the provider an item came from.
#pragma once

#include "providers/internet_archive.hpp"
#include "providers/peertube.hpp"

namespace akeno
{
class Discover final : public Provider
{
  public:
    Discover(PeerTube &peertube, InternetArchive &archive);

    [[nodiscard]] ProviderInfo info() const override;
    ShelvesResult home(const net::CancelFlag &cancel) override;
    [[nodiscard]] bool can_search() const override
    {
        return true;
    }
    ItemsResult search(const std::string &query, const net::CancelFlag &cancel) override;
    DetailsResult details(const MediaItem &item, const net::CancelFlag &cancel) override;

    // Results of two searches, alternating so both sources show up first.
    static std::vector<MediaItem> interleave(std::vector<MediaItem> a, std::vector<MediaItem> b);

  private:
    PeerTube &peertube_;
    InternetArchive &archive_;
};
} // namespace akeno
