// AKENO STREAM PS5 - Discover: open video platforms playable in the app.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "providers/discover.hpp"

namespace akeno
{
Discover::Discover(PeerTube &peertube, InternetArchive &archive)
    : peertube_{peertube}, archive_{archive}
{
}

ProviderInfo Discover::info() const
{
    ProviderInfo info;
    info.id = "discover";
    info.name = "Discover";
    info.tagline = "Free and open video you can play right here: PeerTube and the Internet "
                   "Archive.";
    info.attribution = "PeerTube search by Sepia Search (Framasoft). Films from the Internet "
                       "Archive (archive.org).";
    for (const ProviderInfo &part : {peertube_.info(), archive_.info()})
        for (Capability c : part.capabilities)
        {
            c.name = part.name + ": " + c.name;
            info.capabilities.push_back(std::move(c));
        }
    return info;
}

ShelvesResult Discover::home(const net::CancelFlag &cancel)
{
    ShelvesResult result;
    ShelvesResult peertube = peertube_.home(cancel);
    ShelvesResult archive = archive_.home(cancel);
    for (auto *part : {&peertube, &archive})
        for (auto &shelf : part->shelves)
            result.shelves.push_back(std::move(shelf));
    result.ok = peertube.ok || archive.ok;
    if (!result.ok)
        result.error = peertube.error.empty() ? archive.error : peertube.error;
    else if (!peertube.ok)
        result.error = "PeerTube: " + peertube.error; // shown as a note
    else if (!archive.ok)
        result.error = "Internet Archive: " + archive.error;
    return result;
}

std::vector<MediaItem> Discover::interleave(std::vector<MediaItem> a, std::vector<MediaItem> b)
{
    std::vector<MediaItem> out;
    out.reserve(a.size() + b.size());
    for (std::size_t i = 0; i < a.size() || i < b.size(); ++i)
    {
        if (i < a.size())
            out.push_back(std::move(a[i]));
        if (i < b.size())
            out.push_back(std::move(b[i]));
    }
    return out;
}

ItemsResult Discover::search(const std::string &query, const net::CancelFlag &cancel)
{
    ItemsResult peertube = peertube_.search(query, cancel);
    ItemsResult archive = archive_.search(query, cancel);
    ItemsResult result;
    result.ok = peertube.ok || archive.ok;
    result.items = interleave(std::move(peertube.items), std::move(archive.items));
    if (!result.ok)
        result.error = peertube.error.empty() ? archive.error : peertube.error;
    return result;
}

DetailsResult Discover::details(const MediaItem &item, const net::CancelFlag &cancel)
{
    if (item.provider == "peertube")
        return peertube_.details(item, cancel);
    if (item.provider == "archive")
        return archive_.details(item, cancel);
    return {true, {}, item, {}};
}
} // namespace akeno
