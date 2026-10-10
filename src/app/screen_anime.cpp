// AKENO STREAM PS5 - Anime mode: rows, series with seasons and episodes, search, providers.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Anime is a streaming front end over two kinds of sources: AniList for
// metadata (titles, artwork, seasons, episode titles from official pages) and
// episode providers for video (the bundled sample series, AKENO catalogues,
// PeerTube channels). A series shows a Play button only for episodes a
// provider supplies as DRM-free video, checked before it is offered as
// playable; AniList-only titles are labelled as catalogue entries and say
// which capability is missing.
#include "anime/session.hpp"
#include "app/browse.hpp"
#include "core/url.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <iterator>
#include <set>

namespace akeno
{
namespace th = ui::theme;
using ui::Glyph;
using ui::Icon;
using ui::Pixel;
using ui::Rect;

namespace
{
constexpr char kSeriesCard[] = "anime-series";
constexpr char kActionCard[] = "anime-action";
constexpr char kRecentCard[] = "anime-recent";

// ---------------------------------------------------------------------------
// Cards and labels.
Pixel availability_color(anime::Availability a)
{
    switch (a)
    {
    case anime::Availability::playable:
        return th::kSuccess;
    case anime::Availability::unverified:
        return th::kInfo;
    case anime::Availability::network_error:
        return th::kWarning;
    case anime::Availability::catalogue_only:
    case anime::Availability::episodes_indexed:
        return th::kTextMuted;
    default:
        return th::kError;
    }
}

bool checked_playable(const anime::Series &s)
{
    for (const anime::Season &season : s.seasons)
        for (const anime::Episode &e : season.episodes)
            if (e.availability == anime::Availability::playable)
                return true;
    return false;
}

std::string count_label(int n, const char *one, const char *many)
{
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

MediaItem series_card(const anime::Series &s)
{
    MediaItem m;
    m.provider = kSeriesCard;
    m.id = s.provider_id + "/" + s.id;
    m.kind = ItemKind::series;
    m.title = s.title;
    m.subtitle = s.provider_name;
    m.description = s.description;
    m.image_url = s.poster;
    m.banner_url = s.banner;
    m.portrait = true;
    m.genres = s.genres;
    m.accent = ui::accent_for(s.title);
    std::string meta;
    if (s.year)
        meta = std::to_string(s.year) + "  \xC2\xB7  ";
    meta += count_label(static_cast<int>(s.seasons.size()), "season", "seasons") + "  \xC2\xB7  " +
            count_label(anime::episode_total(s), "episode", "episodes");
    m.meta = meta;
    m.badge =
        checked_playable(s) ? "PLAYABLE" : anime::availability_label(anime::series_availability(s));
    if (!checked_playable(s) && anime::series_availability(s) == anime::Availability::unverified)
        m.badge = "NOT CHECKED";
    m.attribution = s.attribution;
    return m;
}

MediaItem action_card(const char *id, const char *title, const char *subtitle,
                      std::string description, std::uint32_t accent)
{
    MediaItem m;
    m.provider = kActionCard;
    m.id = id;
    m.kind = ItemKind::info;
    m.title = title;
    m.subtitle = subtitle;
    m.description = std::move(description);
    m.accent = accent;
    return m;
}

bool split_series_id(const std::string &id, std::string *provider, std::string *series)
{
    const std::size_t slash = id.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 >= id.size())
        return false;
    *provider = id.substr(0, slash);
    *series = id.substr(slash + 1);
    return true;
}

std::string clock_or_empty(double seconds)
{
    return seconds > 0.0 ? format_clock(seconds) : std::string{};
}

// ---------------------------------------------------------------------------
// A modal page of labelled lines and paragraphs (reports, explanations).
struct InfoLine
{
    std::string label; // empty: a paragraph
    std::string text;
    Pixel color = th::kTextSecondary;
};

class InfoScreen final : public Screen
{
  public:
    InfoScreen(App &app, std::string title, std::vector<InfoLine> lines, std::string action,
               std::function<void()> on_action)
        : Screen{app}, title_{std::move(title)}, lines_{std::move(lines)},
          action_{std::move(action)}, on_action_{std::move(on_action)}
    {
    }
    [[nodiscard]] bool modal() const override
    {
        return true;
    }
    void handle(input::Button b) override
    {
        switch (b)
        {
        case input::Button::up:
            first_ = std::max(0, first_ - 1);
            return;
        case input::Button::down:
            first_ = std::min(std::max(0, static_cast<int>(lines_.size()) - 1), first_ + 1);
            return;
        case input::Button::cross:
            if (on_action_)
            {
                auto run = std::move(on_action_); // pop() destroys this screen
                app_.pop();
                run();
                return;
            }
            app_.pop();
            return;
        case input::Button::circle:
            app_.pop();
            return;
        default:
            return;
        }
    }
    void render(ui::Painter &p, std::uint64_t) override
    {
        p.s.dim(p.s.bounds(), 180);
        const Rect panel{240, 140, 1440, 820};
        p.panel(panel, 32, th::kSurface);
        p.text(panel.x + 60, panel.y + 44, title_, th::kTitle, th::kText, panel.w - 120);
        int y = panel.y + 120;
        const int bottom = panel.bottom() - 40;
        for (std::size_t i = static_cast<std::size_t>(first_); i < lines_.size(); ++i)
        {
            const InfoLine &l = lines_[i];
            if (y > bottom - 40)
            {
                p.text_right(panel.right() - 60, bottom - 10, "More below - D-pad down", th::kSmall,
                             th::kTextMuted);
                break;
            }
            if (l.label.empty())
                y += p.wrapped(panel.x + 60, y, l.text, th::kBody, l.color, panel.w - 120,
                               std::max(1, (bottom - y) / 34)) +
                     14;
            else
            {
                p.text(panel.x + 60, y, l.label, th::kCaptionStrong, th::kTextMuted, 300);
                y += p.wrapped(panel.x + 380, y, l.text, th::kCaption, l.color, panel.w - 440, 4) +
                     12;
            }
        }
    }
    [[nodiscard]] std::vector<Hint> hints() const override
    {
        std::vector<Hint> h;
        if (on_action_)
            h.push_back({Glyph::cross, action_});
        h.push_back({Glyph::dpad, "Scroll"});
        h.push_back({Glyph::circle, on_action_ ? "Cancel" : "Close"});
        return h;
    }

  private:
    std::string title_;
    std::vector<InfoLine> lines_;
    std::string action_;
    std::function<void()> on_action_;
    int first_ = 0;
};

std::vector<InfoLine> report_lines(const anime::ProviderReport &r)
{
    std::vector<InfoLine> out;
    const Pixel verdict = r.importable ? th::kSuccess : th::kWarning;
    out.push_back({"", r.verdict, verdict});
    out.push_back({"What it is", anime::source_class_label(r.source), th::kText});
    if (!r.format.empty())
        out.push_back({"Format", r.format, th::kText});
    if (r.series || r.episodes)
        out.push_back({"Catalogue",
                       count_label(r.series, "series", "series") + ", " +
                           count_label(r.seasons, "season", "seasons") + ", " +
                           count_label(r.episodes, "episode", "episodes") + " (" +
                           std::to_string(r.with_media) + " name a video)",
                       th::kText});
    if (r.probed || r.drm || r.auth || r.region)
    {
        std::string checks =
            std::to_string(r.playable) + " playable of " + std::to_string(r.probed) + " checked";
        if (r.drm)
            checks += ", " + std::to_string(r.drm) + " DRM";
        if (r.auth)
            checks += ", " + std::to_string(r.auth) + " sign-in";
        if (r.region)
            checks += ", " + std::to_string(r.region) + " region";
        if (r.missing)
            checks += ", " + std::to_string(r.missing) + " missing";
        if (r.network)
            checks += ", " + std::to_string(r.network) + " unreachable";
        if (r.unsupported)
            checks += ", " + std::to_string(r.unsupported) + " unsupported";
        out.push_back({"Episodes checked", checks, r.playable ? th::kSuccess : th::kWarning});
    }
    for (std::size_t i = 0; i < r.errors.size() && i < 12; ++i)
        out.push_back(
            {"Error",
             (r.errors[i].path.empty() ? "" : r.errors[i].path + ": ") + r.errors[i].message,
             th::kError});
    for (std::size_t i = 0; i < r.warnings.size() && i < 8; ++i)
        out.push_back(
            {"Note",
             (r.warnings[i].path.empty() ? "" : r.warnings[i].path + ": ") + r.warnings[i].message,
             th::kTextMuted});
    return out;
}

// ---------------------------------------------------------------------------
// Anime mode's front page.
class AnimeHomeScreen final : public BrowseScreen
{
  public:
    explicit AnimeHomeScreen(App &app)
        : BrowseScreen{app, th::kAccentAnime,
                       [&app](const net::CancelFlag &c)
                       {
                           ShelvesResult home = app.anilist().home(c);
                           for (Shelf &s : home.shelves)
                           {
                               if (s.title == "Trending Now")
                                   s.title = "Trending Anime";
                               else if (s.title == "All-Time Popular")
                                   s.title = "Most Popular (AniList members)";
                           }
                           // The genre rows are a second request: their
                           // failure leaves the front page as it is.
                           ShelvesResult more = app.anilist().genre_rows(c);
                           for (Shelf &s : more.shelves)
                               home.shelves.push_back(std::move(s));
                           if (!home.ok && more.ok)
                           {
                               home.ok = true;
                               home.error.clear();
                           }
                           return home;
                       },
                       &app.anilist(), "Search Anime"}
    {
    }

    void handle(input::Button b) override
    {
        const MediaItem *item = shelves_.focused();
        switch (b)
        {
        case input::Button::triangle:
            app_.push(make_anime_search_screen(app_));
            return;
        case input::Button::square:
            if (item)
                toggle_watchlist(*item);
            return;
        case input::Button::options:
            app_.push(make_anime_providers_screen(app_));
            return;
        default:
            BrowseScreen::handle(b);
        }
    }

    void update(std::uint64_t now_ms) override
    {
        if (!requested_)
        {
            requested_ = true;
            app_.refresh_anime(false);
        }
        if (generation_ != app_.anime_generation())
        {
            generation_ = app_.anime_generation();
            rebuild(true);
        }
        BrowseScreen::update(now_ms);
    }

    [[nodiscard]] bool animating() const override
    {
        return BrowseScreen::animating() || app_.anime_loading();
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        std::vector<Hint> h{{Glyph::cross, "Select"}, {Glyph::triangle, "Search"}};
        const MediaItem *item = shelves_.focused();
        if (item && (item->provider == kSeriesCard || item->provider == "anilist"))
            h.push_back({Glyph::square, "Watchlist"});
        h.push_back({Glyph::options, "Providers"});
        return h;
    }

  protected:
    void activate(const MediaItem &item) override
    {
        if (item.provider == kActionCard)
        {
            if (item.id == std::string{"search"})
                app_.push(make_anime_search_screen(app_));
            else if (item.id == std::string{"add-provider"})
                app_.push(make_add_provider_screen(app_));
            else if (item.id == std::string{"providers"})
                app_.push(make_anime_providers_screen(app_));
            else if (item.id == std::string{"crunchyroll-native"})
                app_.push(make_crunchyroll_native_screen(app_));
            return;
        }
        app_.open_item(item);
    }

    std::vector<Shelf> local_before() override
    {
        std::vector<Shelf> out;
        anime::Registry &registry = app_.anime_registry();
        // AniList titles a provider plays get the PLAYABLE badge; the rest
        // keep theirs (episode count, AIRING): catalogue entries.
        for (Shelf &shelf : remote_)
            for (MediaItem &m : shelf.items)
                if (m.provider == "anilist")
                    for (const anime::Series *s :
                         registry.series_for_anilist(std::atoi(m.id.c_str())))
                        if (checked_playable(*s))
                            m.badge = "PLAYABLE";

        Shelf resume{"Continue Watching", {}, false};
        Shelf recent{"Recently Watched", {}, false};
        std::vector<std::string> recent_series;
        for (const HistoryEntry &h : app_.store().history())
        {
            if (h.item.provider != anime::kEpisodeItemProvider)
                continue;
            if (h.position > 0.0 && !h.finished() && resume.items.size() < 20)
                resume.items.push_back(h.item);
            // One card per series: its latest episode.
            std::string p, s, season, e;
            if (anime::split_episode_key(h.item.id, &p, &s, &season, &e) &&
                std::find(recent_series.begin(), recent_series.end(), p + "/" + s) ==
                    recent_series.end() &&
                recent.items.size() < 20)
            {
                recent_series.push_back(p + "/" + s);
                recent.items.push_back(h.item);
            }
        }
        if (!resume.items.empty())
            out.push_back(std::move(resume));

        Shelf available{"Available to Watch", {}, true};
        for (const anime::Series *s : registry.available())
            available.items.push_back(series_card(*s));
        if (!available.items.empty())
            out.push_back(std::move(available));

        Shelf updated{"Recently Updated", {}, true};
        for (const anime::Series *s : registry.recently_updated(20))
            updated.items.push_back(series_card(*s));
        if (!updated.items.empty())
            out.push_back(std::move(updated));

        Shelf watchlist{"My Watchlist", {}, true};
        for (const anime::WatchlistEntry &w : app_.anime_library().watchlist())
        {
            if (!w.provider_id.empty())
            {
                if (const anime::Series *s = registry.find_series(w.provider_id, w.series_id))
                {
                    watchlist.items.push_back(series_card(*s));
                    continue;
                }
            }
            MediaItem m;
            m.provider = w.provider_id.empty() ? "anilist" : kSeriesCard;
            m.id = w.provider_id.empty() ? std::to_string(w.anilist_id)
                                         : w.provider_id + "/" + w.series_id;
            m.kind = ItemKind::series;
            m.title = w.title;
            m.image_url = w.poster;
            m.portrait = true;
            m.accent = ui::accent_for(w.title);
            m.subtitle = w.provider_id.empty() ? "AniList" : "Provider not loaded";
            m.description = w.provider_id.empty()
                                ? "A catalogue title on your watchlist."
                                : "Its provider is not loaded (offline or removed).";
            watchlist.items.push_back(std::move(m));
        }
        if (!watchlist.items.empty())
            out.push_back(std::move(watchlist));
        if (!recent.items.empty())
            out.push_back(std::move(recent));

        Shelf tools{"Your Anime Sources", {}, false};
        tools.items.push_back(action_card(
            "search", "Search Anime", "Titles, filters, availability",
            "Search AniList's catalogue and your providers at once, by English or romanised "
            "title, with year, genre, status and availability filters.",
            0xff7a3d));
        tools.items.push_back(action_card(
            "add-provider", "Add Streaming Provider", "Catalogue, PeerTube channel",
            "Connect an AKENO catalogue (JSON) or a PeerTube channel or playlist. AKENO checks "
            "its episodes before anything is offered as playable.",
            0x35c79a));
        const int provider_count = static_cast<int>(registry.configs().size());
        tools.items.push_back(action_card(
            "providers", "Streaming Providers",
            provider_count == 1 ? "1 provider"
                                : (std::to_string(provider_count) + " providers").c_str(),
            "What each provider offers, what was checked, and why episodes play or not.",
            0x5aa9ff));
        tools.items.push_back(action_card(
            "crunchyroll-native", "Crunchyroll", "Native integration status",
            "What a native Crunchyroll integration needs and why protected episodes do not play "
            "in AKENO STREAM.",
            0xf47521));
        out.push_back(std::move(tools));
        return out;
    }

    std::vector<Shelf> local_after() override
    {
        Shelf open{"Open Animated Films (playable)", {}, false};
        for (const auto &m : OpenCatalog::public_streams())
            if (m.attribution.find("Blender") != std::string::npos)
                open.items.push_back(m);
        return {open};
    }

    void render_empty_hero(ui::Painter &p) override
    {
        p.text(th::kMarginX, kHeroTop + 20, "ANIME", th::kCaptionStrong, accent_);
        p.text(th::kMarginX, kHeroTop + 56, "Series, seasons, episodes", th::kDisplay, th::kText);
    }

  private:
    void toggle_watchlist(const MediaItem &item)
    {
        anime::WatchlistEntry e;
        if (item.provider == kSeriesCard)
        {
            if (!split_series_id(item.id, &e.provider_id, &e.series_id))
                return;
            if (const anime::Series *s =
                    app_.anime_registry().find_series(e.provider_id, e.series_id))
                e.anilist_id = s->anilist_id;
        }
        else if (item.provider == "anilist")
            e.anilist_id = std::atoi(item.id.c_str());
        else
            return;
        e.title = item.title;
        e.poster = item.image_url;
        const bool on = app_.anime_library().toggle_watchlist(e);
        app_.toast(on ? "Added to My Watchlist" : "Removed from My Watchlist",
                   on ? th::kSuccess : th::kInfo);
        rebuild(true);
    }

    bool requested_ = false;
    std::uint64_t generation_ = static_cast<std::uint64_t>(-1);
};

// ---------------------------------------------------------------------------
// A series: artwork, details, Play / Continue, seasons and episodes.
class SeriesScreen final : public Screen
{
  public:
    // From an episode provider.
    SeriesScreen(App &app, std::string provider_id, std::string series_id)
        : Screen{app}, source_provider_{std::move(provider_id)},
          source_series_{std::move(series_id)}
    {
        app_.refresh_anime(false);
        sync_card();
    }
    // From AniList: catalogue data, played by a provider only when one maps it.
    SeriesScreen(App &app, MediaItem item) : Screen{app}, card_{std::move(item)}
    {
        anilist_id_ = std::atoi(card_.id.c_str());
        app_.refresh_anime(false);
        fetch_anilist();
    }
    ~SeriesScreen() override
    {
        *alive_ = false;
    }

    void handle(input::Button b) override
    {
        const anime::Series *s = shown();
        switch (b)
        {
        case input::Button::circle:
            app_.pop();
            return;
        case input::Button::square:
            toggle_watchlist();
            return;
        case input::Button::options:
            show_source_info();
            return;
        case input::Button::triangle:
            app_.push(make_anime_search_screen(app_));
            return;
        default:
            break;
        }
        const int seasons = s ? static_cast<int>(s->seasons.size()) : 0;
        const int episodes =
            s && season_ < seasons
                ? static_cast<int>(s->seasons[static_cast<std::size_t>(season_)].episodes.size())
                : 0;
        switch (zone_)
        {
        case 0: // actions
            if (b == input::Button::left)
                action_ = std::max(0, action_ - 1);
            else if (b == input::Button::right)
                action_ = std::min(static_cast<int>(actions_.size()) - 1, action_ + 1);
            else if (b == input::Button::down && seasons > 0)
                zone_ = 1;
            else if (b == input::Button::cross && action_ < static_cast<int>(actions_.size()))
                run_action(actions_[static_cast<std::size_t>(action_)].kind);
            return;
        case 1: // seasons
            if (b == input::Button::left && season_ > 0)
                select_season(season_ - 1);
            else if (b == input::Button::right && season_ + 1 < seasons)
                select_season(season_ + 1);
            else if (b == input::Button::up)
                zone_ = 0;
            else if ((b == input::Button::down || b == input::Button::cross) && episodes > 0)
                zone_ = 2;
            return;
        default: // episodes
            if (b == input::Button::up)
            {
                if (episode_ == 0)
                    zone_ = 1;
                else
                    --episode_;
            }
            else if (b == input::Button::down && episode_ + 1 < episodes)
                ++episode_;
            else if (b == input::Button::left && season_ > 0)
                select_season(season_ - 1);
            else if (b == input::Button::right && season_ + 1 < seasons)
                select_season(season_ + 1);
            else if (b == input::Button::cross && episodes > 0)
                activate_episode(static_cast<std::size_t>(episode_));
            return;
        }
    }

    void update(std::uint64_t) override
    {
        if (generation_ != app_.anime_generation())
        {
            generation_ = app_.anime_generation();
            resolve_sources();
            sync_card();
        }
        build_actions();
    }

    [[nodiscard]] bool animating() const override
    {
        return loading_ || app_.anime_loading();
    }

    void resumed() override
    {
        build_actions();
    }

    void render(ui::Painter &p, std::uint64_t now_ms) override
    {
        hero_art(p, app_, card_, 95);
        const anime::Series *s = shown();
        // Poster.
        const Rect poster{th::kMarginX, 150, 236, 354};
        const gfx::Image *art = card_.image_url.empty()
                                    ? nullptr
                                    : app_.images().get(card_.image_url, poster.w, poster.h);
        if (art)
            p.s.draw_image(*art, poster.x, poster.y, 255, th::kCardRadius);
        else
            p.placeholder(poster, card_.title, card_.accent, th::kCardRadius);

        const int tx = poster.right() + 48;
        const int tw = th::kWidth - th::kMarginX - tx;
        int y = 146;
        p.text(tx, y, card_.title, th::kTitle, th::kText, tw);
        y += p.line_height(th::kTitle) + 2;
        if (!card_.subtitle.empty())
        {
            p.text(tx, y, card_.subtitle, th::kCaption, th::kTextMuted, tw);
            y += p.line_height(th::kCaption) + 4;
        }
        if (!card_.meta.empty())
        {
            p.text(tx, y, card_.meta, th::kBody, th::kTextSecondary, tw);
            y += p.line_height(th::kBody) + 6;
        }
        // Where the episodes come from, and whether they play.
        const auto [line, color] = availability_summary();
        p.status_line(tx, y, color, line, th::kBodyStrong, color);
        y += p.line_height(th::kBodyStrong) + 8;
        if (!card_.description.empty())
            p.wrapped(tx, y, card_.description, th::kCaption, th::kTextSecondary,
                      std::min(tw, 1240), 3, 2);
        else if (loading_)
            p.spinner(tx + 24, y + 24, 16, now_ms, th::kAccentAnime);

        // Actions.
        int ax = tx;
        const int ay = 446;
        for (std::size_t i = 0; i < actions_.size(); ++i)
            ax += p.button(ax, ay, actions_[i].label, zone_ == 0 && static_cast<int>(i) == action_,
                           th::kAccentAnime, actions_[i].icon) +
                  16;

        // Seasons.
        const int sy = 548;
        if (s && !s->seasons.empty())
        {
            int sx = th::kMarginX;
            const int first = std::max(0, season_ - 5);
            for (int i = first; i < static_cast<int>(s->seasons.size()); ++i)
            {
                const anime::Season &season = s->seasons[static_cast<std::size_t>(i)];
                const std::string label =
                    season.title.empty() ? "Season " + std::to_string(season.number) : season.title;
                const bool selected = i == season_;
                const bool focused = selected && zone_ == 1;
                const int w = std::min(p.measure(label, th::kBodyStrong) + 48, 420);
                if (sx + w > th::kWidth - th::kMarginX)
                    break;
                const Rect chip{sx, sy, w, 52};
                p.s.fill_rounded(chip, 26,
                                 focused    ? th::kText
                                 : selected ? gfx::with_alpha(th::kAccentAnime, 230)
                                            : gfx::with_alpha(th::kSurfaceHighlight, 220));
                p.text_center(chip.x + chip.w / 2, chip.y + 10, label, th::kBodyStrong,
                              focused ? th::kTextOnAccent : th::kText, w - 24);
                sx += w + 14;
            }
        }
        render_episodes(p, now_ms, s);
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        std::vector<Hint> h{{Glyph::cross, zone_ == 2 ? "Play" : "Select"},
                            {Glyph::dpad, "Seasons and episodes"}};
        h.push_back({Glyph::square, in_watchlist() ? "Remove from Watchlist" : "Watchlist"});
        h.push_back({Glyph::options, "Source details"});
        h.push_back({Glyph::circle, "Back"});
        return h;
    }

  private:
    enum class ActionKind
    {
        play,
        watchlist,
        next_source,
        add_provider,
        anilist_page,
        source_info,
    };
    struct Action
    {
        ActionKind kind;
        std::string label;
        Icon icon;
    };

    // The series on screen: the provider's (playable) or AniList's (catalogue).
    [[nodiscard]] const anime::Series *shown() const
    {
        if (!source_provider_.empty())
            if (const anime::Series *s =
                    app_.anime_registry().find_series(source_provider_, source_series_))
                return s;
        return catalog_ ? &*catalog_ : nullptr;
    }

    [[nodiscard]] bool from_provider() const
    {
        return !source_provider_.empty() &&
               app_.anime_registry().find_series(source_provider_, source_series_);
    }

    std::pair<std::string, Pixel> availability_summary() const
    {
        if (from_provider())
        {
            const anime::Series &s = *shown();
            const int listed = anime::playable_episodes(s);
            int checked = 0;
            for (const anime::Season &season : s.seasons)
                for (const anime::Episode &e : season.episodes)
                    if (e.availability == anime::Availability::playable)
                        ++checked;
            if (checked > 0)
                return {"Playable from " + s.provider_name + "  \xC2\xB7  " +
                            std::to_string(checked) + " of " + std::to_string(listed) +
                            " episodes checked",
                        th::kSuccess};
            const anime::Availability best = anime::series_availability(s);
            if (best == anime::Availability::unverified)
                return {"Offered by " + s.provider_name + "  \xC2\xB7  episodes not checked yet",
                        th::kInfo};
            return {s.provider_name + ": " + anime::availability_label(best),
                    availability_color(best)};
        }
        if (!source_provider_.empty() && app_.anime_loading())
            return {"Loading the provider...", th::kInfo};
        if (!source_provider_.empty())
            return {"Its provider is not loaded (offline or removed)", th::kWarning};
        if (loading_ && !catalog_)
            return {"Loading from AniList...", th::kInfo};
        return {"Catalogue only  \xC2\xB7  no connected provider offers this title's episodes",
                th::kTextMuted};
    }

    void sync_card()
    {
        const anime::Series *s = shown();
        if (!s)
            return;
        // Provider data first; AniList fills what the provider leaves out.
        if (from_provider())
        {
            const MediaItem from = series_card(*s);
            card_.title = s->title;
            card_.subtitle = s->alt_titles.empty() ? s->provider_name : s->alt_titles.front();
            if (!s->poster.empty() || card_.image_url.empty())
                card_.image_url = from.image_url;
            if (!s->banner.empty())
                card_.banner_url = from.banner_url;
            if (!s->description.empty() || card_.description.empty())
                card_.description = s->description;
            if (!s->genres.empty())
                card_.genres = s->genres;
            card_.accent = from.accent;
        }
        std::string meta;
        const auto add = [&meta](const std::string &part)
        {
            if (part.empty())
                return;
            if (!meta.empty())
                meta += "  \xC2\xB7  ";
            meta += part;
        };
        const anime::Series *meta_source = catalog_ ? &*catalog_ : s;
        if (meta_source->score > 0)
            add("\xE2\x98\x85 " + std::to_string(meta_source->score) + "%");
        if (s->year || meta_source->year)
            add(std::to_string(s->year ? s->year : meta_source->year));
        add(count_label(static_cast<int>(s->seasons.size()), "season", "seasons"));
        int announced = 0;
        for (const anime::Season &season : s->seasons)
            announced += std::max(season.episode_count, static_cast<int>(season.episodes.size()));
        if (announced > 0)
            add(count_label(announced, "episode", "episodes"));
        std::string genres;
        for (std::size_t i = 0; i < card_.genres.size() && i < 3; ++i)
            genres += (i ? ", " : "") + card_.genres[i];
        add(genres);
        card_.meta = meta;
        season_ = std::clamp(season_, 0, std::max(0, static_cast<int>(s->seasons.size()) - 1));
        build_actions();
    }

    // Providers that map this AniList title (explicit ids only).
    void resolve_sources()
    {
        if (anilist_id_ <= 0)
            return;
        sources_.clear();
        std::vector<int> ids{anilist_id_};
        if (catalog_)
            for (const anime::Season &season : catalog_->seasons)
                if (season.anilist_id > 0 &&
                    std::find(ids.begin(), ids.end(), season.anilist_id) == ids.end())
                    ids.push_back(season.anilist_id);
        for (const int id : ids)
            for (const anime::Series *s : app_.anime_registry().series_for_anilist(id))
                if (std::find_if(sources_.begin(), sources_.end(), [s](const auto &x)
                                 { return x.first == s->provider_id && x.second == s->id; }) ==
                    sources_.end())
                    sources_.emplace_back(s->provider_id, s->id);
        if (source_provider_.empty() && !sources_.empty())
        {
            source_provider_ = sources_.front().first;
            source_series_ = sources_.front().second;
            // Open on the season that is the AniList entry the user chose.
            if (const anime::Series *s = shown())
                for (std::size_t i = 0; i < s->seasons.size(); ++i)
                    if (s->seasons[i].anilist_id == anilist_id_)
                        season_ = static_cast<int>(i);
        }
    }

    void fetch_anilist()
    {
        loading_ = true;
        auto alive = alive_;
        App &app = app_;
        const int id = anilist_id_;
        app_.jobs().run(
            [id, alive, &app, this]
            {
                AniList::SeriesResult result = app.anilist().series(id, net::make_cancel_flag());
                app.jobs().post(
                    [result = std::move(result), alive, this]() mutable
                    {
                        if (!*alive)
                            return;
                        loading_ = false;
                        if (!result.ok)
                        {
                            app_.toast("AniList: " + result.error, th::kWarning);
                            return;
                        }
                        catalog_ = std::move(result.series);
                        if (source_provider_.empty())
                            season_ = result.focus_season;
                        resolve_sources();
                        if (!catalog_->description.empty() &&
                            (card_.description.empty() || !from_provider()))
                            card_.description = catalog_->description;
                        if (card_.banner_url.empty())
                            card_.banner_url = catalog_->banner;
                        sync_card();
                        ensure_season_episodes();
                        app_.mark_dirty();
                    });
            });
    }

    // AniList seasons other than the opened one load their episode titles
    // when they are selected.
    void ensure_season_episodes()
    {
        if (from_provider() || !catalog_ || season_ >= static_cast<int>(catalog_->seasons.size()))
            return;
        anime::Season &season = catalog_->seasons[static_cast<std::size_t>(season_)];
        if (!season.episodes.empty() || season.anilist_id <= 0 ||
            requested_seasons_.count(season.anilist_id))
            return;
        requested_seasons_.insert(season.anilist_id);
        loading_ = true;
        auto alive = alive_;
        App &app = app_;
        const int id = season.anilist_id;
        app_.jobs().run(
            [id, alive, &app, this]
            {
                std::vector<anime::Episode> episodes =
                    app.anilist().season_episodes(id, net::make_cancel_flag());
                app.jobs().post(
                    [episodes = std::move(episodes), id, alive, this]() mutable
                    {
                        if (!*alive)
                            return;
                        loading_ = false;
                        if (!catalog_)
                            return;
                        for (anime::Season &s : catalog_->seasons)
                            if (s.anilist_id == id)
                                s.episodes = std::move(episodes);
                        app_.mark_dirty();
                    });
            });
    }

    void select_season(int index)
    {
        season_ = index;
        episode_ = 0;
        ensure_season_episodes();
        app_.mark_dirty();
    }

    [[nodiscard]] anime::WatchlistEntry watch_entry() const
    {
        anime::WatchlistEntry e;
        if (from_provider())
        {
            e.provider_id = source_provider_;
            e.series_id = source_series_;
        }
        e.anilist_id = anilist_id_ > 0 ? anilist_id_ : (shown() ? shown()->anilist_id : 0);
        e.title = card_.title;
        e.poster = card_.image_url;
        return e;
    }

    [[nodiscard]] bool in_watchlist() const
    {
        return app_.anime_library().in_watchlist(watch_entry());
    }

    void toggle_watchlist()
    {
        const bool on = app_.anime_library().toggle_watchlist(watch_entry());
        app_.toast(on ? "Added to My Watchlist" : "Removed from My Watchlist",
                   on ? th::kSuccess : th::kInfo);
        build_actions();
    }

    void build_actions()
    {
        actions_.clear();
        if (from_provider())
        {
            const anime::Series &s = *shown();
            if (const auto up = anime::next_up(s, app_.store()))
            {
                const anime::Season &season = s.seasons[up->ref.season];
                const std::string label =
                    anime::episode_label(season, season.episodes[up->ref.episode]);
                actions_.push_back({ActionKind::play,
                                    up->resume
                                        ? "Continue " + label + "  " + format_clock(up->position)
                                        : "Play " + label,
                                    Icon::play});
                next_up_ = up;
            }
            else
                next_up_.reset();
        }
        actions_.push_back({ActionKind::watchlist,
                            in_watchlist() ? "In Watchlist" : "Add to Watchlist",
                            in_watchlist() ? Icon::check : Icon::star_outline});
        if (sources_.size() > 1)
            actions_.push_back(
                {ActionKind::next_source, "Source: " + shown()->provider_name, Icon::refresh});
        if (!from_provider())
            actions_.push_back({ActionKind::add_provider, "Add a Provider", Icon::spark});
        if (from_provider())
            actions_.push_back({ActionKind::source_info, "Source details", Icon::info});
        if (anilist_id_ > 0)
            actions_.push_back({ActionKind::anilist_page, "AniList page", Icon::qr});
        action_ = std::clamp(action_, 0, std::max(0, static_cast<int>(actions_.size()) - 1));
    }

    void run_action(ActionKind kind)
    {
        switch (kind)
        {
        case ActionKind::play:
            if (next_up_)
                app_.play_episode(source_provider_, source_series_, next_up_->ref,
                                  next_up_->resume ? next_up_->position : 0.0);
            return;
        case ActionKind::watchlist:
            toggle_watchlist();
            return;
        case ActionKind::next_source:
        {
            auto it =
                std::find_if(sources_.begin(), sources_.end(), [this](const auto &x)
                             { return x.first == source_provider_ && x.second == source_series_; });
            it = (it == sources_.end() || it + 1 == sources_.end()) ? sources_.begin() : it + 1;
            source_provider_ = it->first;
            source_series_ = it->second;
            season_ = episode_ = 0;
            sync_card();
            return;
        }
        case ActionKind::add_provider:
            app_.push(make_add_provider_screen(app_));
            return;
        case ActionKind::source_info:
            show_source_info();
            return;
        case ActionKind::anilist_page:
        {
            MediaItem link = card_;
            link.external_url = "https://anilist.co/anime/" + std::to_string(anilist_id_);
            link.external_label = "AniList page";
            app_.show_qr(link);
            return;
        }
        }
    }

    void show_source_info()
    {
        std::vector<InfoLine> lines;
        if (from_provider())
        {
            const anime::ProviderState *state = app_.anime_registry().state(source_provider_);
            const anime::Series &s = *shown();
            lines.push_back({"Provider", s.provider_name, th::kText});
            if (const anime::ProviderConfig *c = app_.anime_registry().config(source_provider_))
            {
                lines.push_back({"Kind", anime::provider_type_label(c->type), th::kText});
                lines.push_back({"Address", url::redact(c->address), th::kTextSecondary});
            }
            if (!s.licence.empty())
                lines.push_back({"Licence", s.licence, th::kText});
            if (!s.attribution.empty())
                lines.push_back({"Credit", s.attribution, th::kTextSecondary});
            if (s.anilist_id)
                lines.push_back(
                    {"AniList",
                     "Mapped to AniList entry " + std::to_string(s.anilist_id) + " by the provider",
                     th::kTextSecondary});
            if (state)
                for (const InfoLine &l :
                     report_lines(anime::report_for(*state, anime::SourceClass::native_provider)))
                    if (!l.label.empty())
                        lines.push_back(l);
            lines.push_back({"",
                             "Episodes are checked before they are offered as playable: the "
                             "first bytes must be a DRM-free MP4, MPEG-TS, Matroska file or "
                             "HLS playlist. A listing or an HTTP 200 alone is not enough.",
                             th::kTextMuted});
        }
        else
        {
            lines.push_back({"",
                             "AniList describes this title: its seasons, episode counts and the "
                             "episode titles it knows from official streaming pages. AniList "
                             "provides no video.",
                             th::kTextSecondary});
            lines.push_back(
                {"Episode provider", "None of your providers offers this title.", th::kWarning});
            lines.push_back({"",
                             "To play episodes in AKENO STREAM, a provider must supply them as "
                             "DRM-free video and name this title's AniList id (anilistId in an "
                             "AKENO catalogue, or the AniList id set for a PeerTube channel). "
                             "Titles licensed to services with DRM (Crunchyroll and similar) "
                             "cannot play here.",
                             th::kTextSecondary});
        }
        app_.push(std::make_unique<InfoScreen>(app_, card_.title, std::move(lines), "", nullptr));
    }

    void activate_episode(std::size_t index)
    {
        const anime::Series *s = shown();
        if (!s)
            return;
        const anime::Season &season = s->seasons[static_cast<std::size_t>(season_)];
        if (index >= season.episodes.size())
            return;
        const anime::Episode &e = season.episodes[index];
        if (from_provider())
        {
            app_.play_episode(source_provider_, source_series_,
                              {static_cast<std::size_t>(season_), index}, -1.0);
            return;
        }
        if (!e.link.empty())
        {
            MediaItem link;
            link.provider = "link";
            link.id = e.link;
            link.title = anime::episode_heading(season, e);
            link.subtitle = e.link_label;
            link.external_url = e.link;
            link.external_label = "Official page (" + e.link_label + ")";
            link.description = e.availability_detail;
            app_.show_qr(link);
            return;
        }
        app_.toast(e.availability_detail.empty() ? "This episode is not playable here"
                                                 : e.availability_detail,
                   th::kInfo);
    }

    void render_episodes(ui::Painter &p, std::uint64_t now_ms, const anime::Series *s)
    {
        const int top = 624;
        const int row_h = 124;
        const Rect area{th::kMarginX, top, th::kWidth - 2 * th::kMarginX, 3 * row_h};
        if (!s || s->seasons.empty())
        {
            if (loading_ || app_.anime_loading())
                p.spinner(th::kWidth / 2, top + 100, 26, now_ms, th::kAccentAnime);
            return;
        }
        const anime::Season &season = s->seasons[static_cast<std::size_t>(season_)];
        if (season.episodes.empty())
        {
            p.panel({area.x, area.y, area.w, 150}, 20, gfx::with_alpha(th::kSurface, 220));
            if (loading_)
            {
                p.spinner(area.x + 60, area.y + 75, 18, now_ms, th::kAccentAnime);
                p.text(area.x + 100, area.y + 58, "Loading episodes...", th::kBody,
                       th::kTextSecondary);
                return;
            }
            const std::string count =
                season.episode_count > 0
                    ? count_label(season.episode_count, "episode", "episodes") + " announced. "
                    : std::string{};
            p.wrapped(area.x + 40, area.y + 34,
                      count + (from_provider() ? "The provider lists no episodes for this season."
                                               : "AniList lists no episode titles for this season, "
                                                 "and no connected provider offers its episodes."),
                      th::kBody, th::kTextSecondary, area.w - 80, 3);
            return;
        }
        const int n = static_cast<int>(season.episodes.size());
        const int first = std::clamp(episode_ - 1, 0, std::max(0, n - 3));
        for (int i = first; i < n && i < first + 3; ++i)
        {
            const anime::Episode &e = season.episodes[static_cast<std::size_t>(i)];
            const Rect row{area.x, top + (i - first) * row_h, area.w, row_h - 12};
            const bool focused = zone_ == 2 && i == episode_;
            p.s.fill_rounded(row, 18,
                             focused ? gfx::with_alpha(th::kSurfaceHighlight, 245)
                                     : gfx::with_alpha(th::kSurface, 215));
            if (focused)
                p.focus_ring(row, 18, th::kText);
            // Thumbnail.
            const Rect thumb{row.x + 12, row.y + 10, 160, 90};
            const gfx::Image *img =
                e.thumbnail.empty() ? nullptr : app_.images().get(e.thumbnail, thumb.w, thumb.h);
            if (img)
                p.s.draw_image(*img, thumb.x, thumb.y, 255, 10);
            else
                p.placeholder(thumb, anime::episode_label(season, e), ui::accent_for(s->title), 10);
            const std::string key = anime::episode_key(s->provider_id, s->id, season.id, e.id);
            const anime::EpisodeProgress progress = anime::progress_of(app_.store(), key);
            if (progress.started && !progress.watched && progress.duration > 0.0)
                p.progress({thumb.x, thumb.bottom() - 6, thumb.w, 6},
                           progress.position / progress.duration, th::kAccentAnime,
                           gfx::rgba(0, 0, 0, 150));
            // Text.
            const int x = thumb.right() + 24;
            const int w = row.right() - x - 300;
            const std::string heading = e.title.empty()
                                            ? anime::episode_label(season, e)
                                            : anime::episode_label(season, e) + "  " + e.title;
            p.text(x, row.y + 10, heading, th::kBodyStrong, th::kText, w);
            std::string facts;
            const auto add = [&facts](const std::string &part)
            {
                if (part.empty())
                    return;
                if (!facts.empty())
                    facts += "  \xC2\xB7  ";
                facts += part;
            };
            add(clock_or_empty(e.duration));
            add(e.air_date);
            if (e.media)
            {
                std::string audio, subs;
                for (const anime::AudioTrack &a : e.media->audio)
                    audio += (audio.empty() ? "" : ", ") + (a.label.empty() ? a.language : a.label);
                for (const anime::SubtitleTrack &t : e.media->subtitles)
                    subs += (subs.empty() ? "" : ", ") + t.label;
                if (!audio.empty())
                    add("Audio: " + audio);
                if (!subs.empty())
                    add("CC " + subs);
            }
            if (!e.link_label.empty())
                add(e.link_label);
            p.text(x, row.y + 44, facts, th::kCaption, th::kTextMuted, w);
            if (!e.description.empty())
                p.text(x, row.y + 74, e.description, th::kCaption, th::kTextSecondary, w);
            // Status on the right.
            const int rx = row.right() - 24;
            if (progress.watched)
                p.text_right(rx, row.y + 12, "Watched", th::kCaptionStrong, th::kSuccess);
            else if (progress.started)
                p.text_right(rx, row.y + 12, "Resume " + format_clock(progress.position),
                             th::kCaptionStrong, th::kAccentAnime);
            const bool playable = e.availability == anime::Availability::playable ||
                                  e.availability == anime::Availability::unverified;
            const std::string status = playable && from_provider()
                                           ? (focused ? "Cross: Play" : "Play")
                                           : anime::availability_label(e.availability);
            p.text_right(rx, row.y + 48, status, th::kCaptionStrong,
                         playable && from_provider() ? th::kSuccess
                                                     : availability_color(e.availability));
            if (focused && !playable)
                p.text_right(rx, row.y + 78, e.link.empty() ? "" : "Cross: official page (QR)",
                             th::kSmall, th::kTextMuted);
        }
        if (n > 3)
            p.text_right(area.right(), top - 30,
                         std::to_string(episode_ + 1) + " / " + std::to_string(n), th::kSmall,
                         th::kTextMuted);
    }

    MediaItem card_;
    int anilist_id_ = 0;
    std::optional<anime::Series> catalog_;
    std::string source_provider_, source_series_;
    std::vector<std::pair<std::string, std::string>> sources_;
    std::set<int> requested_seasons_;
    std::vector<Action> actions_;
    std::optional<anime::NextUp> next_up_;
    int zone_ = 0;
    int action_ = 0;
    int season_ = 0;
    int episode_ = 0;
    bool loading_ = false;
    std::uint64_t generation_ = static_cast<std::uint64_t>(-1);
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

// ---------------------------------------------------------------------------
// Anime search: AniList and the providers, with filters.
const char *kGenres[] = {"Action",        "Adventure",    "Comedy", "Drama",         "Fantasy",
                         "Horror",        "Mahou Shoujo", "Mecha",  "Music",         "Mystery",
                         "Psychological", "Romance",      "Sci-Fi", "Slice of Life", "Sports",
                         "Supernatural",  "Thriller"};

class AnimeSearchScreen final : public Screen
{
  public:
    explicit AnimeSearchScreen(App &app) : Screen{app}
    {
        app_.refresh_anime(false);
        show_start();
    }
    ~AnimeSearchScreen() override
    {
        *alive_ = false;
    }

    void handle(input::Button b) override
    {
        if (zone_ == 2)
        {
            if (b == input::Button::up && results_.focused_row() == 0)
            {
                zone_ = 1;
                return;
            }
            if (results_.handle(b))
                return;
            if (b == input::Button::cross)
            {
                if (const MediaItem *m = results_.focused())
                    open(*m);
                return;
            }
        }
        switch (b)
        {
        case input::Button::triangle:
            ask();
            return;
        case input::Button::circle:
            app_.pop();
            return;
        case input::Button::up:
            zone_ = std::max(0, zone_ - 1);
            return;
        case input::Button::down:
            zone_ = std::min(results_.empty() ? 1 : 2, zone_ + 1);
            return;
        case input::Button::left:
            if (zone_ == 1)
                filter_ = std::max(0, filter_ - 1);
            return;
        case input::Button::right:
            if (zone_ == 1)
                filter_ = std::min(3, filter_ + 1);
            return;
        case input::Button::cross:
            if (zone_ == 0)
                ask();
            else if (zone_ == 1)
                choose_filter(filter_);
            return;
        case input::Button::square:
            if (!app_.anime_library().searches().empty())
            {
                app_.anime_library().clear_searches();
                app_.toast("Search history cleared", th::kInfo);
                if (query_.empty())
                    show_start();
            }
            return;
        default:
            return;
        }
    }

    void update(std::uint64_t) override
    {
        moving_ = results_.animate(app_.store().settings().reduce_motion);
        if (generation_ != app_.anime_generation())
        {
            generation_ = app_.anime_generation();
            if (query_.empty())
                show_start();
            else
                compose();
        }
    }
    [[nodiscard]] bool animating() const override
    {
        return searching_ || moving_;
    }

    void render(ui::Painter &p, std::uint64_t now_ms) override
    {
        p.text(th::kMarginX, 150, "Search Anime", th::kTitle, th::kText);
        const Rect field{th::kMarginX, 224, 1100, 72};
        p.s.fill_rounded(field, 36, zone_ == 0 ? th::kText : th::kSurfaceRaised);
        p.icon(Icon::search, field.x + 44, field.y + 36, 30,
               zone_ == 0 ? th::kTextOnAccent : th::kTextSecondary);
        p.text(field.x + 80, field.y + 18,
               query_.empty() ? "English or romanised title, part of a title..." : query_,
               th::kBody,
               zone_ == 0 ? th::kTextOnAccent : (query_.empty() ? th::kTextMuted : th::kText),
               field.w - 120);
        // Filters.
        const std::string labels[4] = {
            "Year: " + (filters_.year ? std::to_string(filters_.year) : std::string{"Any"}),
            "Genre: " + (filters_.genre.empty() ? std::string{"Any"} : filters_.genre),
            "Status: " + status_label(filters_.status),
            "Availability: " + availability_filter_label()};
        int x = th::kMarginX;
        for (int i = 0; i < 4; ++i)
        {
            const bool focused = zone_ == 1 && i == filter_;
            const int w = p.measure(labels[i], th::kCaptionStrong) + 40;
            const Rect chip{x, 318, w, 46};
            p.s.fill_rounded(chip, 23,
                             focused ? th::kText : gfx::with_alpha(th::kSurfaceHighlight, 220));
            p.text_center(chip.x + w / 2, chip.y + 10, labels[i], th::kCaptionStrong,
                          focused ? th::kTextOnAccent : th::kText);
            x += w + 12;
        }
        if (searching_)
            p.spinner(th::kWidth - th::kMarginX - 30, 260, 18, now_ms, th::kAccentAnime);
        if (!note_.empty())
            p.text(th::kMarginX, 382, note_, th::kCaption, th::kTextMuted,
                   th::kWidth - 2 * th::kMarginX);
        if (!results_.empty())
            results_.render(p, {th::kMarginX, 420, th::kWidth - 2 * th::kMarginX, 580},
                            app_.images(), &app_.store(), zone_ == 2);
        else if (!query_.empty() && !searching_)
            p.text(th::kMarginX, 460, "Nothing found for \"" + query_ + "\".", th::kHeading,
                   th::kTextSecondary);
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        std::vector<Hint> h{{Glyph::cross, zone_ == 1 ? "Change filter" : "Select"},
                            {Glyph::triangle, "Type a title"}};
        if (!app_.anime_library().searches().empty())
            h.push_back({Glyph::square, "Clear history"});
        h.push_back({Glyph::circle, "Back"});
        return h;
    }

  private:
    static std::string status_label(const std::string &status)
    {
        if (status == "releasing")
            return "Airing";
        if (status == "finished")
            return "Finished";
        if (status == "upcoming")
            return "Upcoming";
        return "Any";
    }
    [[nodiscard]] std::string availability_filter_label() const
    {
        switch (filters_.availability)
        {
        case anime::SearchFilters::Availability::playable:
            return "Playable here";
        case anime::SearchFilters::Availability::catalogue:
            return "Catalogue only";
        default:
            return "All";
        }
    }

    void ask()
    {
        app_.open_keyboard("Search anime", query_, 80, false,
                           [this, alive = alive_](bool ok, const std::string &text)
                           {
                               if (!*alive || !ok)
                                   return;
                               query_ = text;
                               if (query_.empty())
                               {
                                   show_start();
                                   return;
                               }
                               app_.anime_library().remember_search(query_);
                               run();
                           });
    }

    void choose_filter(int index)
    {
        ChoiceModel model;
        auto alive = alive_;
        if (index == 0)
        {
            const std::time_t t = static_cast<std::time_t>(platform::wall_clock_seconds());
            std::tm *utc = std::gmtime(&t);
            const int current = utc && utc->tm_year + 1900 > 2000 ? utc->tm_year + 1900 : 2026;
            model.options = [current]
            {
                std::vector<ChoiceOption> out{{"Any year", ""}};
                for (int y = current + 1; y >= 1980; --y)
                    out.push_back({std::to_string(y), ""});
                return out;
            };
            model.selected = [this, current]
            { return filters_.year ? current + 1 - filters_.year + 1 : 0; };
            model.choose = [this, alive, current](int i)
            {
                if (!*alive)
                    return;
                filters_.year = i == 0 ? 0 : current + 2 - i;
                rerun();
            };
        }
        else if (index == 1)
        {
            model.options = []
            {
                std::vector<ChoiceOption> out{{"Any genre", ""}};
                for (const char *g : kGenres)
                    out.push_back({g, ""});
                return out;
            };
            model.selected = [this]
            {
                for (std::size_t i = 0; i < std::size(kGenres); ++i)
                    if (filters_.genre == kGenres[i])
                        return static_cast<int>(i + 1);
                return 0;
            };
            model.choose = [this, alive](int i)
            {
                if (!*alive)
                    return;
                filters_.genre = i == 0 ? std::string{} : kGenres[i - 1];
                rerun();
            };
        }
        else if (index == 2)
        {
            static const char *ids[] = {"", "releasing", "finished", "upcoming"};
            model.options = []
            {
                return std::vector<ChoiceOption>{
                    {"Any status", ""}, {"Airing", ""}, {"Finished", ""}, {"Upcoming", ""}};
            };
            model.selected = [this]
            {
                for (int i = 0; i < 4; ++i)
                    if (filters_.status == ids[i])
                        return i;
                return 0;
            };
            model.choose = [this, alive](int i)
            {
                if (!*alive)
                    return;
                filters_.status = ids[i];
                rerun();
            };
        }
        else
        {
            model.options = []
            {
                return std::vector<ChoiceOption>{
                    {"All titles", "Catalogue and playable"},
                    {"Playable here", "Series a provider plays in AKENO STREAM"},
                    {"Catalogue only", "AniList titles without an episode provider"}};
            };
            model.selected = [this] { return static_cast<int>(filters_.availability); };
            model.choose = [this, alive](int i)
            {
                if (!*alive)
                    return;
                filters_.availability = static_cast<anime::SearchFilters::Availability>(i);
                rerun();
            };
        }
        model.footer = [] { return std::string{}; };
        static const char *titles[] = {"Year", "Genre", "Status", "Availability"};
        app_.push(make_choice_screen(app_, titles[index], "Applies to AniList and your providers",
                                     std::move(model)));
    }

    void rerun()
    {
        if (query_.empty() && !filters_.year && filters_.genre.empty() && filters_.status.empty())
            show_start();
        else
            run();
    }

    // Before a search: recent searches, suggestions and what can be played.
    void show_start()
    {
        anilist_.clear();
        std::vector<Shelf> rows;
        Shelf history{"Recent Searches", {}, false};
        for (const std::string &q : app_.anime_library().searches())
        {
            MediaItem m;
            m.provider = kRecentCard;
            m.id = q;
            m.kind = ItemKind::info;
            m.title = q;
            m.subtitle = "Search again";
            m.accent = ui::accent_for(q);
            history.items.push_back(std::move(m));
        }
        if (!history.items.empty())
            rows.push_back(std::move(history));
        Shelf available{"Available to Watch", {}, true};
        for (const anime::Series *s : app_.anime_registry().available())
            available.items.push_back(series_card(*s));
        if (!available.items.empty())
            rows.push_back(std::move(available));
        note_ = "Triangle: type a title. Results show what plays in AKENO STREAM apart from "
                "catalogue-only titles.";
        results_.set(std::move(rows));
    }

    void run()
    {
        searching_ = true;
        note_.clear();
        anilist_.clear();
        compose();
        if (filters_.availability == anime::SearchFilters::Availability::playable)
        {
            searching_ = false;
            return; // AniList titles are only listed when a provider plays them
        }
        AniList::Filters f;
        f.year = filters_.year;
        f.genre = filters_.genre;
        f.status = filters_.status == "releasing"  ? "RELEASING"
                   : filters_.status == "finished" ? "FINISHED"
                   : filters_.status == "upcoming" ? "NOT_YET_RELEASED"
                                                   : "";
        auto alive = alive_;
        App &app = app_;
        const std::string query = query_;
        const int token = ++token_;
        app_.jobs().run(
            [query, f, alive, token, &app, this]
            {
                ItemsResult result = app.anilist().search(query, f, net::make_cancel_flag());
                app.jobs().post(
                    [result = std::move(result), alive, token, this]() mutable
                    {
                        if (!*alive || token != token_)
                            return;
                        searching_ = false;
                        if (!result.ok)
                            note_ = "AniList could not be searched: " + result.error +
                                    " Your providers were searched.";
                        anilist_ = std::move(result.items);
                        compose();
                    });
            });
    }

    void compose()
    {
        anime::Registry &registry = app_.anime_registry();
        Shelf playable{"Available to Watch", {}, true};
        std::vector<std::string> shown;
        if (filters_.availability != anime::SearchFilters::Availability::catalogue)
        {
            anime::SearchFilters f = filters_;
            f.availability = anime::SearchFilters::Availability::playable;
            for (const anime::SeriesHit &hit : registry.search(query_, f))
            {
                playable.items.push_back(series_card(*hit.series));
                shown.push_back(hit.series->provider_id + "/" + hit.series->id);
            }
        }
        Shelf catalogue{"Catalogue only (AniList)", {}, true};
        for (const MediaItem &m : anilist_)
        {
            // An AniList title a provider plays is listed with that provider.
            bool mapped = false;
            for (const anime::Series *s : registry.series_for_anilist(std::atoi(m.id.c_str())))
                if (anime::playable_episodes(*s) > 0)
                {
                    mapped = true;
                    const std::string key = s->provider_id + "/" + s->id;
                    if (filters_.availability != anime::SearchFilters::Availability::catalogue &&
                        std::find(shown.begin(), shown.end(), key) == shown.end())
                    {
                        playable.items.push_back(series_card(*s));
                        shown.push_back(key);
                    }
                }
            if (!mapped && filters_.availability != anime::SearchFilters::Availability::playable)
                catalogue.items.push_back(m);
        }
        std::vector<Shelf> rows;
        playable.title = "Available to Watch (" + std::to_string(playable.items.size()) + ")";
        catalogue.title =
            "Catalogue only - AniList (" + std::to_string(catalogue.items.size()) + ")";
        if (!playable.items.empty())
            rows.push_back(std::move(playable));
        if (!catalogue.items.empty())
            rows.push_back(std::move(catalogue));
        // Suggestions from provider titles for a partial query.
        if (!query_.empty())
        {
            Shelf suggest{"Did you mean", {}, false};
            for (const std::string &title : registry.suggestions(query_, 6))
                if (anime::normalize_title(title) != anime::normalize_title(query_))
                {
                    MediaItem m;
                    m.provider = kRecentCard;
                    m.id = title;
                    m.kind = ItemKind::info;
                    m.title = title;
                    m.subtitle = "Search for this title";
                    m.accent = ui::accent_for(title);
                    suggest.items.push_back(std::move(m));
                }
            if (!suggest.items.empty())
                rows.push_back(std::move(suggest));
        }
        results_.set(std::move(rows), true);
        if (zone_ == 2 && results_.empty())
            zone_ = 1;
        app_.mark_dirty();
    }

    void open(const MediaItem &m)
    {
        if (m.provider == kRecentCard)
        {
            query_ = m.id;
            app_.anime_library().remember_search(query_);
            run();
            return;
        }
        app_.open_item(m);
    }

    std::string query_;
    anime::SearchFilters filters_;
    std::vector<MediaItem> anilist_;
    ShelfView results_;
    std::string note_;
    int zone_ = 0;
    int filter_ = 0;
    int token_ = 0;
    bool searching_ = false;
    bool moving_ = false;
    std::uint64_t generation_ = static_cast<std::uint64_t>(-1);
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

// ---------------------------------------------------------------------------
// Anime > Streaming Providers.
class ProvidersScreen final : public Screen
{
  public:
    explicit ProvidersScreen(App &app) : Screen{app}
    {
        app_.refresh_anime(false);
    }

    void handle(input::Button b) override
    {
        const int count = static_cast<int>(app_.anime_registry().configs().size()) + 1;
        switch (b)
        {
        case input::Button::up:
            row_ = std::max(0, row_ - 1);
            return;
        case input::Button::down:
            row_ = std::min(count - 1, row_ + 1);
            return;
        case input::Button::cross:
            if (row_ == 0)
                app_.push(make_add_provider_screen(app_));
            else
                details(static_cast<std::size_t>(row_ - 1));
            return;
        case input::Button::square:
            if (row_ > 0)
                remove(static_cast<std::size_t>(row_ - 1));
            return;
        case input::Button::triangle:
            app_.refresh_anime(true);
            app_.toast("Checking every provider again...", th::kInfo);
            return;
        case input::Button::circle:
            app_.pop();
            return;
        default:
            return;
        }
    }

    [[nodiscard]] bool animating() const override
    {
        return app_.anime_loading();
    }

    void render(ui::Painter &p, std::uint64_t now_ms) override
    {
        p.text(th::kMarginX, 150, "Streaming Providers", th::kTitle, th::kText);
        p.wrapped(th::kMarginX, 214,
                  "Where Anime's episodes come from. AniList supplies titles and artwork only; "
                  "these providers supply the video. Each is checked: an episode is offered as "
                  "playable only when its first bytes are DRM-free media.",
                  th::kBody, th::kTextSecondary, 1500, 2);
        const auto &configs = app_.anime_registry().configs();
        const int count = static_cast<int>(configs.size()) + 1;
        const int row_h = 112;
        const int visible = 6;
        const int first = std::clamp(row_ - visible / 2, 0, std::max(0, count - visible));
        int y = 312;
        for (int i = first; i < count && i < first + visible; ++i)
        {
            const Rect row{th::kMarginX, y, th::kWidth - 2 * th::kMarginX, row_h - 12};
            const bool focused = i == row_;
            p.s.fill_rounded(row, 18, focused ? th::kText : gfx::with_alpha(th::kSurface, 225));
            const Pixel fg = focused ? th::kTextOnAccent : th::kText;
            const Pixel sub = focused ? th::kTextOnAccent : th::kTextMuted;
            if (i == 0)
            {
                p.icon(Icon::spark, row.x + 52, row.y + row.h / 2, 34,
                       focused ? th::kTextOnAccent : th::kSuccess);
                p.text(row.x + 100, row.y + 16, "Add Streaming Provider", th::kBodyStrong, fg);
                p.text(row.x + 100, row.y + 52,
                       "An AKENO catalogue (JSON) or a PeerTube channel or playlist", th::kCaption,
                       sub);
            }
            else
            {
                const anime::ProviderConfig &c = configs[static_cast<std::size_t>(i - 1)];
                const anime::ProviderState *state = app_.anime_registry().state(c.id);
                p.icon(c.type == anime::ProviderType::peertube ? Icon::tv : Icon::film, row.x + 52,
                       row.y + row.h / 2, 34, fg);
                p.text(row.x + 100, row.y + 16, c.name, th::kBodyStrong, fg, 700);
                p.text(row.x + 100, row.y + 52,
                       std::string{anime::provider_type_label(c.type)} +
                           (c.builtin ? "  \xC2\xB7  built in"
                                      : "  \xC2\xB7  " + url::redact(c.address)),
                       th::kCaption, sub, 900);
                std::string status;
                Pixel color = sub;
                if (!state)
                {
                    status = "Loading...";
                    p.spinner(row.right() - 40, row.y + row.h / 2, 14, now_ms, fg);
                }
                else if (!state->loaded)
                {
                    status = state->error;
                    color = focused ? th::kTextOnAccent : th::kWarning;
                }
                else
                {
                    const anime::ProviderReport r =
                        anime::report_for(*state, anime::SourceClass::native_provider);
                    status = count_label(r.series, "series", "series") + ", " +
                             count_label(r.episodes, "episode", "episodes") + "  \xC2\xB7  " +
                             std::to_string(r.playable) + " checked playable";
                    color = focused ? th::kTextOnAccent : (r.playable ? th::kSuccess : th::kInfo);
                }
                p.text_right(row.right() - 80, row.y + 34, status, th::kCaptionStrong, color);
            }
            y += row_h;
        }
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        std::vector<Hint> h{{Glyph::cross, row_ == 0 ? "Add" : "Report"}};
        if (row_ > 0)
        {
            const auto &configs = app_.anime_registry().configs();
            if (static_cast<std::size_t>(row_ - 1) < configs.size() &&
                !configs[static_cast<std::size_t>(row_ - 1)].builtin)
                h.push_back({Glyph::square, "Remove"});
        }
        h.push_back({Glyph::triangle, "Check again"});
        h.push_back({Glyph::circle, "Back"});
        return h;
    }

  private:
    void details(std::size_t index)
    {
        const auto &configs = app_.anime_registry().configs();
        if (index >= configs.size())
            return;
        const anime::ProviderConfig c = configs[index];
        std::vector<InfoLine> lines;
        lines.push_back({"Kind", anime::provider_type_label(c.type), th::kText});
        lines.push_back(
            {"Address", c.builtin ? c.address : url::redact(c.address), th::kTextSecondary});
        if (const anime::ProviderState *state = app_.anime_registry().state(c.id))
            for (const InfoLine &l :
                 report_lines(anime::report_for(*state, anime::SourceClass::native_provider)))
                lines.push_back(l);
        else
            lines.push_back({"", "Not loaded yet.", th::kTextMuted});
        app_.push(std::make_unique<InfoScreen>(app_, c.name, std::move(lines), "", nullptr));
    }

    void remove(std::size_t index)
    {
        const auto &configs = app_.anime_registry().configs();
        if (index >= configs.size() || configs[index].builtin)
        {
            app_.toast("Built-in providers stay", th::kInfo);
            return;
        }
        const std::string id = configs[index].id, name = configs[index].name;
        app_.push(make_confirm_screen(
            app_, "Remove \"" + name + "\"?",
            "Its series leave Anime. Your watch history keeps its entries; they say the "
            "series is no longer offered.",
            "Remove",
            [&app = app_, id]
            {
                app.anime_registry().remove(id);
                app.toast("Provider removed", th::kInfo);
            }));
        row_ = std::max(0, row_ - 1);
    }

    int row_ = 0;
};

// ---------------------------------------------------------------------------
// Add Streaming Provider: name, address, optional AniList id; test, import.
class AddProviderScreen final : public Screen
{
  public:
    AddProviderScreen(App &app, std::string address, std::string name)
        : Screen{app}, address_{std::move(address)}, name_{std::move(name)}
    {
        if (!address_.empty())
            test();
    }
    ~AddProviderScreen() override
    {
        *alive_ = false;
    }

    void handle(input::Button b) override
    {
        switch (b)
        {
        case input::Button::up:
            row_ = std::max(0, row_ - 1);
            return;
        case input::Button::down:
            row_ = std::min(4, row_ + 1);
            return;
        case input::Button::cross:
            activate();
            return;
        case input::Button::circle:
            app_.pop();
            return;
        default:
            return;
        }
    }
    [[nodiscard]] bool animating() const override
    {
        return testing_;
    }

    void render(ui::Painter &p, std::uint64_t now_ms) override
    {
        p.text(th::kMarginX, 150, "Add Streaming Provider", th::kTitle, th::kText);
        p.wrapped(th::kMarginX, 214,
                  "A provider is a catalogue or API that lists series and episodes with DRM-free "
                  "video: an AKENO catalogue (JSON, see docs/ANIME_PROVIDERS.md) or a PeerTube "
                  "channel or playlist. A website's homepage is not a provider: it stays in "
                  "Websites.",
                  th::kBody, th::kTextSecondary, 1700, 3);
        const struct
        {
            const char *label;
            std::string value;
            const char *placeholder;
        } fields[3] = {{"Provider name", name_, "My Anime Source"},
                       {"Address", address_, "https://example.com/catalog.json"},
                       {"AniList id (optional)",
                        anilist_id_ ? std::to_string(anilist_id_) : std::string{},
                        "For a PeerTube channel that is one series"}};
        int y = 346;
        const int lw = 800;
        for (int i = 0; i < 3; ++i)
        {
            const bool focused = row_ == i;
            p.text(th::kMarginX, y, fields[i].label, th::kCaptionStrong, th::kTextMuted);
            const Rect box{th::kMarginX, y + 32, lw, 64};
            p.s.fill_rounded(box, 16, focused ? th::kText : th::kSurfaceRaised);
            p.text(box.x + 24, box.y + 16,
                   fields[i].value.empty() ? fields[i].placeholder : fields[i].value, th::kBody,
                   focused ? th::kTextOnAccent
                           : (fields[i].value.empty() ? th::kTextMuted : th::kText),
                   lw - 48);
            y += 122;
        }
        int bx = th::kMarginX;
        bx += p.button(bx, y + 10, testing_ ? "Testing..." : "Test Connection", row_ == 3,
                       th::kAccentAnime, Icon::refresh) +
              18;
        p.button(bx, y + 10, "Import Provider", row_ == 4,
                 report_ && report_->importable ? th::kSuccess : th::kTextMuted, Icon::check);
        if (testing_)
            p.spinner(th::kMarginX + lw + 90, 420, 24, now_ms, th::kAccentAnime);

        // The compatibility report.
        const Rect panel{th::kMarginX + lw + 60, 346,
                         th::kWidth - th::kMarginX - (th::kMarginX + lw + 60), 620};
        p.panel(panel, 24, gfx::with_alpha(th::kSurface, 230));
        p.text(panel.x + 32, panel.y + 24, "Compatibility report", th::kHeading, th::kText);
        if (!report_)
        {
            p.wrapped(panel.x + 32, panel.y + 90,
                      "Test the connection: AKENO STREAM reads the address, recognises its "
                      "format, lists its series and episodes and checks up to 12 episodes' "
                      "videos.",
                      th::kBody, th::kTextMuted, panel.w - 64, 5);
            return;
        }
        int ry = panel.y + 84;
        for (const InfoLine &l : report_lines(*report_))
        {
            if (ry > panel.bottom() - 50)
                break;
            if (l.label.empty())
                ry += p.wrapped(panel.x + 32, ry, l.text, th::kBody, l.color, panel.w - 64, 4) + 12;
            else
            {
                p.text(panel.x + 32, ry, l.label, th::kCaptionStrong, th::kTextMuted, 220);
                ry +=
                    p.wrapped(panel.x + 260, ry, l.text, th::kCaption, l.color, panel.w - 292, 3) +
                    10;
            }
        }
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        return {{Glyph::cross, row_ < 3    ? "Edit"
                               : row_ == 3 ? "Test"
                                           : "Import"},
                {Glyph::dpad, "Choose"},
                {Glyph::circle, "Back"}};
    }

  private:
    void activate()
    {
        auto alive = alive_;
        switch (row_)
        {
        case 0:
            app_.open_keyboard("Provider name", name_, 60, false,
                               [this, alive](bool ok, const std::string &text)
                               {
                                   if (ok && *alive)
                                       name_ = text;
                               });
            return;
        case 1:
            app_.open_keyboard("Catalogue, channel or playlist address",
                               address_.empty() ? std::string{"https://"} : address_, 2048, false,
                               [this, alive](bool ok, const std::string &text)
                               {
                                   if (!ok || !*alive)
                                       return;
                                   address_ = text;
                                   report_.reset();
                               });
            return;
        case 2:
            app_.open_keyboard("AniList id (digits, empty: none)",
                               anilist_id_ ? std::to_string(anilist_id_) : std::string{}, 9, false,
                               [this, alive](bool ok, const std::string &text)
                               {
                                   if (ok && *alive)
                                       anilist_id_ = std::atoi(text.c_str());
                               });
            return;
        case 3:
            test();
            return;
        default:
            import();
            return;
        }
    }

    void test()
    {
        if (testing_)
            return;
        if (address_.empty() || address_ == "https://")
        {
            app_.toast("Enter the provider's address first", th::kWarning);
            row_ = 1;
            return;
        }
        testing_ = true;
        report_.reset();
        auto alive = alive_;
        App &app = app_;
        const std::string address = address_, name = name_;
        const anime::ProviderContext context = app_.anime_registry().context;
        app_.jobs().run(
            [address, name, context, alive, &app, this]
            {
                anime::ProviderConfig config;
                anime::ProviderReport report =
                    anime::check_source(address, name, context, net::make_cancel_flag(), &config);
                app.jobs().post(
                    [report = std::move(report), config, alive, this]() mutable
                    {
                        if (!*alive)
                            return;
                        testing_ = false;
                        report_ = std::move(report);
                        config_ = config;
                        if (name_.empty() && !report_->name.empty())
                            name_ = report_->name;
                        if (report_->importable)
                            row_ = 4;
                        app_.mark_dirty();
                    });
            });
    }

    void import()
    {
        if (!report_ || !report_->importable)
        {
            app_.toast(report_ ? "This address cannot be used as a provider (see the report)"
                               : "Test the connection first",
                       th::kWarning);
            return;
        }
        anime::ProviderConfig config = config_;
        if (!name_.empty())
            config.name = name_;
        config.anilist_id = anilist_id_;
        std::string why;
        if (!app_.anime_registry().add(config, &why))
        {
            app_.toast(why, th::kWarning);
            return;
        }
        app_.refresh_anime(false);
        app_.toast("Provider added: " + config.name, th::kSuccess);
        app_.pop();
    }

    std::string address_, name_;
    int anilist_id_ = 0;
    int row_ = 0;
    bool testing_ = false;
    std::optional<anime::ProviderReport> report_;
    anime::ProviderConfig config_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};
} // namespace

std::unique_ptr<Screen> make_anime_screen(App &app)
{
    return std::make_unique<AnimeHomeScreen>(app);
}

std::unique_ptr<Screen> make_series_screen(App &app, std::string provider_id, std::string series_id)
{
    return std::make_unique<SeriesScreen>(app, std::move(provider_id), std::move(series_id));
}

std::unique_ptr<Screen> make_anilist_series_screen(App &app, MediaItem item)
{
    return std::make_unique<SeriesScreen>(app, std::move(item));
}

std::unique_ptr<Screen> make_anime_search_screen(App &app)
{
    return std::make_unique<AnimeSearchScreen>(app);
}

std::unique_ptr<Screen> make_anime_providers_screen(App &app)
{
    return std::make_unique<ProvidersScreen>(app);
}

std::unique_ptr<Screen> make_add_provider_screen(App &app, std::string address, std::string name)
{
    return std::make_unique<AddProviderScreen>(app, std::move(address), std::move(name));
}

void show_provider_report(App &app, const anime::ProviderReport &report,
                          const anime::ProviderConfig &config)
{
    std::vector<InfoLine> lines = report_lines(report);
    std::function<void()> import;
    if (report.importable && !config.address.empty())
        import = [&app, config]
        {
            std::string why;
            if (app.anime_registry().add(config, &why))
            {
                app.refresh_anime(false);
                app.toast("Provider added: " + config.name + " - see Anime", th::kSuccess);
            }
            else
                app.toast(why, th::kWarning);
        };
    app.push(std::make_unique<InfoScreen>(
        app, report.importable ? "A native streaming provider" : "Not a streaming provider",
        std::move(lines), "Add to Anime", std::move(import)));
}

std::unique_ptr<Screen> make_crunchyroll_native_screen(App &app)
{
    std::vector<InfoLine> lines = {
        {"",
         "Crunchyroll's website and sign-in work in Websites. A native Crunchyroll mode - "
         "account, watchlist, catalogue and episodes inside AKENO STREAM's own player - is not "
         "available, for these reasons:",
         th::kText},
        {"Account",
         "Not available. Crunchyroll publishes no public API, developer programme or "
         "device sign-in for third-party apps; its own apps use private credentials. "
         "Signing in on crunchyroll.com in the browser gives AKENO STREAM no token, and "
         "AKENO never copies browser cookies or sessions into its player.",
         th::kError},
        {"Watchlist", "Not available without an authorized account connection.", th::kError},
        {"Catalogue",
         "Available as metadata: AniList lists the titles licensed to Crunchyroll, "
         "their seasons and the episode titles from Crunchyroll's pages. Anime shows "
         "them as catalogue entries.",
         th::kSuccess},
        {"Playback",
         "Not supported. Crunchyroll delivers episodes with DRM (Widevine / "
         "PlayReady). Playing them requires a licensed content decryption module; the "
         "PS5's licensed DRM is available to Sony-approved apps, not to homebrew, and "
         "AKENO STREAM will not use extracted or unlicensed keys. The browser on this "
         "console reported no usable DRM system, which is why episodes stop with "
         "KAT-6005 there.",
         th::kError},
        {"What is missing",
         "A partner agreement with Crunchyroll (API access and client "
         "credentials for an authorized sign-in), and a licensed DRM module "
         "the app may use on the PS5. Neither can be added from a homebrew "
         "app.",
         th::kWarning},
        {"",
         "Protected episodes stay labelled as unsupported until an authorized playback test "
         "succeeds on the console. The Playback Lab (Websites) measures the browser's DRM "
         "support.",
         th::kTextMuted},
    };
    return std::make_unique<InfoScreen>(app, "Crunchyroll native integration", std::move(lines), "",
                                        nullptr);
}
} // namespace akeno
