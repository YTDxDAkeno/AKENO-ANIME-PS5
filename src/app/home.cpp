// AKENO STREAM PS5 - The rows of Home.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/home.hpp"

#include "app/browse.hpp"
#include "media/remux.hpp"
#include "providers/discover.hpp"

#include <algorithm>
#include <set>

namespace akeno
{
namespace
{
// Adds items to a row once each (by key), up to kHomeRowItems.
class RowBuilder
{
  public:
    RowBuilder(std::string title, bool portrait = false) : shelf_{std::move(title), {}, portrait}
    {
    }
    void add(const MediaItem &item)
    {
        if (shelf_.items.size() < kHomeRowItems && seen_.insert(item.key()).second)
            shelf_.items.push_back(item);
    }
    // Cards that lead somewhere (actions, sections) always fit.
    void add_card(MediaItem item)
    {
        if (seen_.insert(item.key()).second)
            shelf_.items.push_back(std::move(item));
    }
    [[nodiscard]] bool empty() const noexcept
    {
        return shelf_.items.empty();
    }
    Shelf take()
    {
        return std::move(shelf_);
    }

  private:
    Shelf shelf_;
    std::set<std::string> seen_;
};

MediaItem action(const char *id, const char *title, const char *subtitle, const char *description,
                 std::uint32_t accent)
{
    MediaItem m = mode_card(id, title, subtitle, description, accent);
    m.provider = "action";
    return m;
}

bool visible_site(const web::Website &w)
{
    return !w.private_site;
}
} // namespace

std::vector<Shelf> home_shelves(const HomeContent &c)
{
    std::vector<Shelf> out;
    const auto keep = [&out](RowBuilder &row)
    {
        if (!row.empty())
            out.push_back(row.take());
    };

    RowBuilder resume{"Continue Watching"};
    for (const HistoryEntry &e : c.continue_watching)
        resume.add(e.item);
    keep(resume);

    // Videos opened before, then YouTube's trending videos (with a key), then
    // the ways to watch that need no key.
    RowBuilder youtube{"YouTube"};
    for (const HistoryEntry &e : c.history)
        if (e.item.provider == "youtube" && e.item.kind == ItemKind::video)
            youtube.add(e.item);
    for (const MediaItem &m : c.youtube)
        youtube.add(m);
    youtube.add_card(action(kHomeYouTubeLink, "Play a YouTube Link", "Video or playlist",
                            "Type or paste a YouTube link or video ID. It plays in YouTube's "
                            "official embedded player inside AKENO STREAM.",
                            0xff3d5a));
    youtube.add_card(action(kHomeYouTubeSite, "youtube.com", "In the browser",
                            "The full YouTube website in the PS5 browser inside AKENO STREAM, "
                            "with your sign-in on Google's own page.",
                            0xff6b5a));
    if (!c.youtube_configured)
        youtube.add_card(mode_card("youtube", "Browse YouTube", "Set up search",
                                   "Trending videos, search and channels need a free YouTube "
                                   "Data API key of your own. YouTube mode shows the five steps.",
                                   0x9b3d4a));
    keep(youtube);

    // AniList's posters; the anime streaming service opens as a website.
    RowBuilder anime{"Anime", !c.anime.empty()};
    for (const MediaItem &m : c.anime)
        anime.add(m);
    anime.add_card(mode_card(
        "crunchyroll", "Crunchyroll", "Website in the browser",
        "crunchyroll.com in the PS5 browser inside AKENO STREAM with its own sign-in, and what "
        "this console's browser can and cannot play.",
        0xf47521));
    keep(anime);

    RowBuilder mine{"My Websites"};
    for (const web::Website &w : c.websites)
        if (w.pinned && visible_site(w))
            mine.add(site_card(w));
    keep(mine);

    RowBuilder discover{"Discover"};
    for (const MediaItem &m : c.discover)
        discover.add(m);
    for (const MediaItem &m : c.open_streams)
        discover.add(m);
    discover.add_card(mode_card("open", "Open Streams", "DRM-free",
                                "Public test streams, open movies and the HLS links in your "
                                "streams.json.",
                                0x4f8cff));
    keep(discover);

    // Files played before (USB drives included), the media folder, then the
    // clips that ship with the app.
    RowBuilder local{"Local Library"};
    for (const HistoryEntry &e : c.history)
        if (e.item.provider == "local")
            local.add(e.item);
    for (const MediaItem &m : c.local_files)
        local.add(m);
    for (const MediaItem &m : c.bundled)
        local.add(m);
    keep(local);

    // Newest first; entries without a clock keep the order they were added in.
    std::vector<const web::Website *> added;
    for (const web::Website &w : c.websites)
        if (visible_site(w))
            added.push_back(&w);
    std::reverse(added.begin(), added.end());
    std::stable_sort(added.begin(), added.end(), [](const web::Website *a, const web::Website *b)
                     { return a->added > b->added; });
    RowBuilder recent{"Recently Added Websites"};
    for (std::size_t i = 0; i < added.size() && i < kHomeRecentWebsites; ++i)
        recent.add(site_card(*added[i]));
    keep(recent);

    RowBuilder favorites{"Favorites"};
    for (const MediaItem &m : c.favorites)
        favorites.add(m);
    keep(favorites);
    return out;
}

std::vector<MediaItem> first_row(const ShelvesResult &result, std::size_t limit)
{
    for (const Shelf &s : result.shelves)
        if (!s.items.empty())
        {
            std::vector<MediaItem> items = s.items;
            if (items.size() > limit)
                items.resize(limit);
            return items;
        }
    return {};
}

std::vector<MediaItem> discover_row(const ShelvesResult &result, std::size_t limit)
{
    const auto first_of = [&result](const char *provider) -> std::vector<MediaItem>
    {
        for (const Shelf &s : result.shelves)
            if (!s.items.empty() && s.items.front().provider == provider)
                return s.items;
        return {};
    };
    std::vector<MediaItem> items = Discover::interleave(first_of("peertube"), first_of("archive"));
    if (items.empty())
        items = first_row(result, limit);
    if (items.size() > limit)
        items.resize(limit);
    return items;
}

std::vector<MediaItem> local_media_items(const std::string &directory, std::size_t limit)
{
    std::vector<MediaItem> out;
    const auto list = fs::list(directory, nullptr, 500);
    if (!list)
        return out;
    for (const fs::Entry &e : *list)
    {
        if (out.size() >= limit)
            break;
        if (!e.directory && !e.name.empty() && e.name[0] != '.' &&
            media::is_playable_extension(e.name))
            out.push_back(local_file_item(directory, e));
    }
    return out;
}
} // namespace akeno
