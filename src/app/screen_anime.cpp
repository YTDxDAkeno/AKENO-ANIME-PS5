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
#include "anime/address.hpp"
#include "anime/playback_route.hpp"
#include "anime/session.hpp"
#include "anime/site_policy.hpp"
#include "anime/website.hpp"
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
    if (s.details_pending)
        meta += "Episodes load when opened";
    else
        meta += count_label(static_cast<int>(s.seasons.size()), "season", "seasons") +
                "  \xC2\xB7  " + count_label(anime::episode_total(s), "episode", "episodes");
    m.meta = meta;
    m.badge =
        checked_playable(s) ? "PLAYABLE" : anime::availability_label(anime::series_availability(s));
    if (!checked_playable(s) && anime::series_availability(s) == anime::Availability::unverified)
        m.badge = "NOT CHECKED";
    if (s.details_pending)
        m.badge = "NOT OPENED";
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

// "2026-10-11" (UTC) for a Unix time.
std::string format_date(std::uint64_t seconds)
{
    const std::time_t t = static_cast<std::time_t>(seconds);
    char text[16] = "";
    if (const std::tm *utc = std::gmtime(&t))
        std::strftime(text, sizeof(text), "%Y-%m-%d", utc);
    return text;
}

Pixel playback_color(anime::PlaybackClass c)
{
    switch (c)
    {
    case anime::PlaybackClass::verified:
    case anime::PlaybackClass::native:
    case anime::PlaybackClass::partial:
        return th::kSuccess;
    case anime::PlaybackClass::catalogue_only:
    case anime::PlaybackClass::browser_only:
        return th::kInfo;
    case anime::PlaybackClass::protected_media:
    case anime::PlaybackClass::auth_required:
    case anime::PlaybackClass::unsupported:
    case anime::PlaybackClass::unavailable:
        return th::kWarning;
    case anime::PlaybackClass::unknown:
        break;
    }
    return th::kTextMuted;
}

// A loaded provider's report, with what its episodes do in AKENO STREAM as
// the registry knows it (a website's unopened series are not "no video").
anime::ProviderReport provider_report(const anime::Registry &registry,
                                      const anime::ProviderState &state)
{
    anime::ProviderReport r = anime::report_for(state, anime::SourceClass::native_provider);
    const anime::PlaybackClass known = registry.playback_of(state.config.id);
    if (known != r.playback)
    {
        r.playback = known;
        r.verdict = anime::playback_class_detail(known);
    }
    if (const auto site = anime::known_site(state.config.address))
    {
        r.known_site = site->site;
        r.browser_hint = site->browser_hint;
    }
    return r;
}

// Opens a provider's page in the PS5 browser (Websites), saying so: what plays
// there is the website's own player, never AKENO STREAM's.
void open_on_website(App &app, const anime::ProviderConfig &provider, const std::string &page,
                     const std::string &title)
{
    WebSession session;
    session.url = page;
    session.title = title;
    if (const auto site = anime::known_site(page.empty() ? provider.address : page))
        app.toast(site->browser_hint, th::kInfo);
    else
        app.toast("Opening " + provider.name +
                      " in the PS5 browser - its own player, not AKENO STREAM's",
                  th::kInfo);
    app.open_web(std::move(session));
}

std::vector<InfoLine> report_lines(const anime::ProviderReport &r)
{
    std::vector<InfoLine> out;
    const Pixel verdict = r.importable ? th::kSuccess : th::kWarning;
    out.push_back({"", r.verdict, verdict});
    if (r.playback != anime::PlaybackClass::unknown)
        out.push_back(
            {"In AKENO", anime::playback_class_label(r.playback), playback_color(r.playback)});
    // A known website: why, before the rest.
    const auto note = [](const anime::CatalogIssue &w) -> InfoLine
    { return {"Note", (w.path.empty() ? "" : w.path + ": ") + w.message, th::kTextMuted}; };
    if (!r.known_site.empty())
        for (std::size_t i = 0; i < r.warnings.size() && i < 8; ++i)
            out.push_back(note(r.warnings[i]));
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
    if (!r.capabilities.empty())
    {
        std::string offers;
        for (const std::string &c : r.capabilities)
            offers += (offers.empty() ? "" : ", ") + c;
        out.push_back({"Offers", offers, th::kText});
    }
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
    if (r.known_site.empty())
        for (std::size_t i = 0; i < r.warnings.size() && i < 8; ++i)
            out.push_back(note(r.warnings[i]));
    if (!r.browser_hint.empty() &&
        std::none_of(r.warnings.begin(), r.warnings.end(),
                     [&r](const anime::CatalogIssue &w) { return w.message == r.browser_hint; }))
        out.push_back({"On the website", r.browser_hint, th::kInfo});
    // A preview of what Import adds (after the problems, which matter more).
    for (const std::string &line : r.preview)
        out.push_back({"Series", line, th::kTextSecondary});
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
            else if (item.id.rfind("provider:", 0) == 0)
                app_.push(make_provider_home_screen(app_, item.id.substr(9)));
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
            "add-provider", "Add Streaming Provider", "Website, PeerTube, catalogue",
            "Enter a website's address. AKENO STREAM recognises PeerTube instances, websites with "
            "schema.org structured data, AKENO catalogues and PeerTube channels, and checks "
            "their episodes before anything is offered as playable.",
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
        // Each provider the user added opens its own home.
        for (const anime::ProviderConfig &c : registry.configs())
        {
            if (c.builtin)
                continue;
            const anime::PlaybackClass playback = registry.playback_of(c.id);
            const std::string kind = c.type == anime::ProviderType::website
                                         ? std::string{"Website provider"}
                                         : std::string{anime::provider_type_label(c.type)};
            MediaItem home =
                action_card("", c.name.c_str(),
                            (playback == anime::PlaybackClass::unknown
                                 ? kind
                                 : kind + "  \xC2\xB7  " + anime::playback_class_label(playback))
                                .c_str(),
                            std::string{"Its catalogue, search and what it offers. "} +
                                anime::playback_class_detail(playback),
                            ui::accent_for(c.name));
            home.id = std::string{"provider:"} + c.id;
            if (const anime::ProviderState *state = registry.state(c.id))
                for (const anime::Series &s : state->series)
                    if (!s.banner.empty() || !s.poster.empty())
                    {
                        home.image_url = s.banner.empty() ? s.poster : s.banner;
                        break;
                    }
            tools.items.push_back(std::move(home));
        }
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
        // A website provider's catalogue entry: its seasons and episodes load now.
        if (const anime::Series *s = shown();
            s && from_provider() && s->details_pending && !details_requested_)
        {
            details_requested_ = true;
            app_.load_anime_series(source_provider_, source_series_);
        }
        build_actions();
    }

    [[nodiscard]] bool animating() const override
    {
        return loading_ || app_.anime_loading() ||
               app_.anime_series_loading(source_provider_, source_series_);
    }

    void resumed() override
    {
        build_actions();
    }
    [[nodiscard]] std::string screen_id() const override
    {
        return "series:" + source_provider_ + "/" + source_series_;
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
        // "Play" only on an episode AKENO STREAM can play.
        std::vector<Hint> h{{Glyph::cross, zone_ != 2           ? "Select"
                                           : focused_playable() ? "Play"
                                                                : "Details"},
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
        retry,
        website,
        embed,
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

    [[nodiscard]] bool focused_playable() const
    {
        const anime::Series *s = shown();
        if (!s || !from_provider() || season_ >= static_cast<int>(s->seasons.size()))
            return false;
        const anime::Season &season = s->seasons[static_cast<std::size_t>(season_)];
        if (episode_ >= static_cast<int>(season.episodes.size()))
            return false;
        const anime::Episode &e = season.episodes[static_cast<std::size_t>(episode_)];
        return (e.media || !e.source_ref.empty()) &&
               (e.availability == anime::Availability::playable ||
                e.availability == anime::Availability::unverified);
    }

    // A website provider's series: its own page on the site (from the site's
    // data), for Open on Website.
    [[nodiscard]] const anime::ProviderConfig *website_provider() const
    {
        if (!from_provider())
            return nullptr;
        const anime::ProviderConfig *c = app_.anime_registry().config(source_provider_);
        return c && c->type == anime::ProviderType::website ? c : nullptr;
    }

    std::pair<std::string, Pixel> availability_summary() const
    {
        if (from_provider() && shown()->details_pending)
        {
            const std::string error = app_.anime_series_error(source_provider_, source_series_);
            if (!error.empty())
                return {"Could not load the episodes: " + error, th::kWarning};
            return {"Loading seasons and episodes from " + shown()->provider_name + "...",
                    th::kInfo};
        }
        if (from_provider())
        {
            const anime::Series &s = *shown();
            const int listed = anime::playable_episodes(s);
            // Nothing here plays in AKENO STREAM: say where it does, and never
            // offer Play.
            const anime::PlaybackClass playback =
                app_.anime_registry().playback_of(source_provider_);
            if (listed == 0 && (playback == anime::PlaybackClass::browser_only ||
                                playback == anime::PlaybackClass::catalogue_only))
                return {playback == anime::PlaybackClass::browser_only
                            ? "Website only  \xC2\xB7  episodes play on " + s.provider_name +
                                  "'s website, in the PS5 browser (Open on Website)"
                            : "Catalogue only  \xC2\xB7  no episode names a video AKENO STREAM "
                              "may play",
                        th::kInfo};
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
        if (!s->details_pending)
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
                // The same episode in its service's own player (PeerTube, an
                // embed the provider names), on AKENO's video-only page.
                if (anime::episode_embed(season.episodes[up->ref.episode]))
                    actions_.push_back({ActionKind::embed, "Video-only player", Icon::play});
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
        if (from_provider() && shown()->details_pending &&
            !app_.anime_series_error(source_provider_, source_series_).empty())
            actions_.push_back({ActionKind::retry, "Try Again", Icon::refresh});
        if (website_provider() && !shown()->source_ref.empty())
            actions_.push_back({ActionKind::website, "Open on Website", Icon::home});
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
        case ActionKind::embed:
            if (next_up_)
                app_.play_episode_embedded(source_provider_, source_series_, next_up_->ref,
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
        case ActionKind::retry:
            app_.load_anime_series(source_provider_, source_series_);
            return;
        case ActionKind::website:
            if (const anime::ProviderConfig *c = website_provider())
                open_on_website(app_, *c, shown()->source_ref, shown()->title);
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
                     report_lines(provider_report(app_.anime_registry(), *state)))
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
        if (s && s->details_pending)
        {
            p.panel({area.x, area.y, area.w, 150}, 20, gfx::with_alpha(th::kSurface, 220));
            const std::string error = app_.anime_series_error(source_provider_, source_series_);
            if (error.empty())
            {
                p.spinner(area.x + 60, area.y + 75, 18, now_ms, th::kAccentAnime);
                p.text(area.x + 100, area.y + 40, "Loading seasons and episodes...", th::kBody,
                       th::kTextSecondary);
                p.text(area.x + 100, area.y + 80,
                       "Read from " + s->provider_name +
                           "'s own pages; each video is checked "
                           "before it is offered as playable.",
                       th::kCaption, th::kTextMuted, area.w - 140);
            }
            else
                p.wrapped(area.x + 40, area.y + 34,
                          "The episodes could not be loaded: " + error +
                              " Choose Try Again, or open the site in Websites.",
                          th::kBody, th::kWarning, area.w - 80, 3);
            return;
        }
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
    bool details_requested_ = false;
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
            else if (static_cast<std::size_t>(row_ - 1) < app_.anime_registry().configs().size())
                app_.push(make_provider_home_screen(
                    app_, app_.anime_registry().configs()[static_cast<std::size_t>(row_ - 1)].id));
            return;
        case input::Button::options:
            if (row_ > 0)
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
    [[nodiscard]] std::string screen_id() const override
    {
        return "providers";
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
                       "A website's address: PeerTube instances, schema.org websites, AKENO "
                       "catalogues, PeerTube channels",
                       th::kCaption, sub);
            }
            else
            {
                const anime::ProviderConfig &c = configs[static_cast<std::size_t>(i - 1)];
                const anime::ProviderState *state = app_.anime_registry().state(c.id);
                p.icon(c.type == anime::ProviderType::peertube  ? Icon::tv
                       : c.type == anime::ProviderType::website ? Icon::home
                                                                : Icon::film,
                       row.x + 52, row.y + row.h / 2, 34, fg);
                p.text(row.x + 100, row.y + 16, c.name, th::kBodyStrong, fg, 700);
                std::string kind = anime::provider_type_label(c.type);
                if (const anime::WebsiteAdapter *a = c.type == anime::ProviderType::website
                                                         ? anime::find_website_adapter(c.adapter)
                                                         : nullptr)
                    kind = a->label();
                p.text(row.x + 100, row.y + 52,
                       kind + (c.builtin ? "  \xC2\xB7  built in"
                                         : "  \xC2\xB7  " + url::redact(c.address)),
                       th::kCaption, sub, 900);
                std::string status;
                Pixel color = sub;
                if (!c.enabled)
                    status = "Turned off";
                else if (!state)
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
                    status = count_label(r.series, "series", "series") +
                             (c.type == anime::ProviderType::website
                                  ? std::string{}
                                  : ", " + count_label(r.episodes, "episode", "episodes")) +
                             "  \xC2\xB7  " + std::to_string(r.playable) + " checked playable";
                    color = focused ? th::kTextOnAccent : (r.playable ? th::kSuccess : th::kInfo);
                }
                p.text_right(row.right() - 80, row.y + 34, status, th::kCaptionStrong, color);
            }
            y += row_h;
        }
        // Saved entries this version keeps but does not load (anime/provider_store.hpp).
        const auto &problems = app_.anime_registry().storage_problems();
        if (!problems.empty())
            p.wrapped(th::kMarginX, 312 + visible * row_h + 6,
                      std::to_string(problems.size()) +
                          (problems.size() == 1 ? " saved provider entry is"
                                                : " saved provider entries are") +
                          " kept but not loaded: " + problems.front() +
                          " Details in Settings > Diagnostics.",
                      th::kCaption, th::kWarning, th::kWidth - 2 * th::kMarginX, 2);
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        std::vector<Hint> h{{Glyph::cross, row_ == 0 ? "Add" : "Open"}};
        if (row_ > 0)
            h.push_back({Glyph::options, "Report"});
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
            for (const InfoLine &l : report_lines(provider_report(app_.anime_registry(), *state)))
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
// A provider's home: its catalogue, search, more pages, what it offers.
constexpr char kMoreCard[] = "anime-more";

class ProviderHomeScreen final : public Screen
{
  public:
    ProviderHomeScreen(App &app, std::string provider_id) : Screen{app}, id_{std::move(provider_id)}
    {
        app_.refresh_anime_provider(false, id_);
        rebuild();
    }
    ~ProviderHomeScreen() override
    {
        *alive_ = false;
    }

    void handle(input::Button b) override
    {
        if (!config())
        {
            if (b == input::Button::circle || b == input::Button::cross)
                app_.pop();
            return;
        }
        if (zone_ == 1)
        {
            if (b == input::Button::up && shelves_.focused_row() == 0)
            {
                zone_ = 0;
                return;
            }
            if (shelves_.handle(b))
                return;
            if (b == input::Button::cross)
            {
                if (const MediaItem *m = shelves_.focused())
                    open(*m);
                return;
            }
        }
        switch (b)
        {
        case input::Button::triangle:
            ask_search();
            return;
        case input::Button::options:
            show_details();
            return;
        case input::Button::square:
            ask_remove();
            return;
        case input::Button::circle:
            app_.pop();
            return;
        case input::Button::left:
            if (zone_ == 0)
                action_ = std::max(0, action_ - 1);
            return;
        case input::Button::right:
            if (zone_ == 0)
                action_ = std::min(static_cast<int>(actions_.size()) - 1, action_ + 1);
            return;
        case input::Button::down:
            if (zone_ == 0 && !shelves_.empty())
                zone_ = 1;
            return;
        case input::Button::cross:
            if (zone_ == 0 && action_ < static_cast<int>(actions_.size()))
                run(actions_[static_cast<std::size_t>(action_)].kind);
            return;
        default:
            return;
        }
    }

    void update(std::uint64_t) override
    {
        moving_ = shelves_.animate(app_.store().settings().reduce_motion);
        if (generation_ != app_.anime_generation() || more_ != app_.anime_more_loading(id_))
        {
            generation_ = app_.anime_generation();
            more_ = app_.anime_more_loading(id_);
            rebuild();
        }
    }

    [[nodiscard]] bool animating() const override
    {
        return moving_ || searching_ || checking_ || app_.anime_provider_loading(id_) ||
               app_.anime_more_loading(id_);
    }
    [[nodiscard]] std::string screen_id() const override
    {
        return "provider-home:" + id_;
    }
    [[nodiscard]] std::string focus_id() const override
    {
        const MediaItem *m = zone_ == 1 ? shelves_.focused() : nullptr;
        return m ? m->id : std::string{};
    }

    void render(ui::Painter &p, std::uint64_t now_ms) override
    {
        const anime::ProviderConfig *c = config();
        if (!c)
        {
            p.text(th::kMarginX, 150, "Provider removed", th::kTitle, th::kText);
            p.text(th::kMarginX, 220, "This provider is no longer in your list.", th::kBody,
                   th::kTextSecondary);
            return;
        }
        const anime::ProviderState *state = app_.anime_registry().state(id_);
        const int w = th::kWidth - 2 * th::kMarginX;
        p.text(th::kMarginX, 146, c->name, th::kTitle, th::kText, w - 300);
        p.text(th::kMarginX, 204, kind_line(*c), th::kCaption, th::kTextMuted, w);
        const auto [status, color] = status_line(*c, state);
        if (app_.anime_provider_loading(id_))
            p.spinner(th::kMarginX + 14, 256, 12, now_ms, th::kAccentAnime);
        p.status_line(th::kMarginX + (app_.anime_provider_loading(id_) ? 36 : 0), 240, color,
                      status, th::kBodyStrong, color);
        int x = th::kMarginX;
        // What its episodes do here, first: no Play buttons unless it plays.
        if (const anime::PlaybackClass playback = app_.anime_registry().playback_of(id_);
            playback != anime::PlaybackClass::unknown || checking_)
            x += p.chip(x, 292,
                        checking_ ? std::string{"Checking playback..."}
                                  : anime::playback_class_label(playback),
                        gfx::with_alpha(playback_color(playback), 230), th::kTextOnAccent) +
                 10;
        for (const std::string &line : capabilities(*c, state))
        {
            const int cw = p.measure(line, th::kSmall) + 32;
            if (x + cw > th::kWidth - th::kMarginX)
                break;
            x += p.chip(x, 292, line, gfx::with_alpha(th::kSurfaceHighlight, 230), th::kText) + 10;
        }
        int ax = th::kMarginX;
        for (std::size_t i = 0; i < actions_.size(); ++i)
            ax += p.button(ax, 340, actions_[i].label, zone_ == 0 && static_cast<int>(i) == action_,
                           th::kAccentAnime, actions_[i].icon) +
                  14;
        if (searching_)
            p.spinner(ax + 30, 372, 16, now_ms, th::kAccentAnime);
        const Rect area{th::kMarginX, 432, w, 570};
        if (!shelves_.empty())
        {
            shelves_.render(p, area, app_.images(), &app_.store(), zone_ == 1);
            return;
        }
        p.panel({area.x, area.y, area.w, 170}, 20, gfx::with_alpha(th::kSurface, 220));
        std::string message;
        if (!c->enabled)
            message = "This provider is turned off: it is kept but not loaded. Choose Turn On to "
                      "load its catalogue again.";
        else if (!state || app_.anime_provider_loading(id_))
            message = "Loading the catalogue from " + url_host(c->address) + "...";
        else if (!state->loaded)
            message = "The catalogue could not be loaded: " + state->error +
                      " Choose Check Again to retry.";
        else
            message = "The provider lists nothing right now.";
        p.wrapped(area.x + 40, area.y + 40, message, th::kBody, th::kTextSecondary, area.w - 80, 3);
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        std::vector<Hint> h{{Glyph::cross, zone_ == 1 ? "Open" : "Select"},
                            {Glyph::triangle, "Search"},
                            {Glyph::options, "Details"}};
        if (const anime::ProviderConfig *c = config(); c && !c->builtin)
            h.push_back({Glyph::square, "Remove"});
        h.push_back({Glyph::circle, "Back"});
        return h;
    }

  private:
    enum class ActionKind
    {
        search,
        more,
        check,
        toggle,
        website,
        details,
    };
    struct Action
    {
        ActionKind kind;
        std::string label;
        Icon icon;
    };

    [[nodiscard]] const anime::ProviderConfig *config() const
    {
        return app_.anime_registry().config(id_);
    }

    static std::string url_host(const std::string &address)
    {
        const auto u = url::parse(address);
        return u ? u->host : address;
    }

    static std::string kind_line(const anime::ProviderConfig &c)
    {
        std::string kind = anime::provider_type_label(c.type);
        if (c.type == anime::ProviderType::website)
            if (const anime::WebsiteAdapter *a = anime::find_website_adapter(c.adapter))
                kind = std::string{"Website provider  \xC2\xB7  "} + a->label();
        return kind + "  \xC2\xB7  " +
               (c.builtin ? std::string{"built in"} : url::redact(c.address));
    }

    std::pair<std::string, Pixel> status_line(const anime::ProviderConfig &c,
                                              const anime::ProviderState *state) const
    {
        if (!c.enabled)
            return {"Turned off", th::kTextMuted};
        if (!state || app_.anime_provider_loading(id_))
            return {c.known_series > 0
                        ? "Loading...  \xC2\xB7  " +
                              count_label(c.known_series, "series", "series") + " at the last check"
                        : std::string{"Loading..."},
                    th::kInfo};
        if (!state->loaded)
            return {"Not loaded: " + state->error, th::kWarning};
        const anime::ProviderReport r =
            anime::report_for(*state, anime::SourceClass::native_provider);
        std::string line = count_label(static_cast<int>(state->series.size()), "series", "series");
        if (state->total > static_cast<int>(state->series.size()))
            line += " of " + std::to_string(state->total);
        else if (!state->next_page.empty())
            line += " so far";
        const bool opened = std::any_of(state->series.begin(), state->series.end(),
                                        [](const anime::Series &s) { return !s.details_pending; });
        line += opened ? "  \xC2\xB7  " + std::to_string(r.playable) + " episodes checked playable"
                       : std::string{"  \xC2\xB7  episodes load when a series is opened"};
        return {line, r.playable ? th::kSuccess : th::kInfo};
    }

    [[nodiscard]] std::vector<std::string> capabilities(const anime::ProviderConfig &c,
                                                        const anime::ProviderState *state) const
    {
        if (c.type == anime::ProviderType::website)
            if (const anime::WebsiteAdapter *a = anime::find_website_adapter(c.adapter))
            {
                anime::SiteCapabilities caps = a->capabilities(c);
                caps.pagination = state && !state->next_page.empty();
                caps.native_playback =
                    state &&
                    anime::report_for(*state, anime::SourceClass::native_provider).playable > 0;
                // A catalogue-only or website-only site claims seasons and
                // episode lists only when an opened series has them.
                const anime::PlaybackClass playback = app_.anime_registry().playback_of(id_);
                if (playback == anime::PlaybackClass::catalogue_only ||
                    playback == anime::PlaybackClass::browser_only)
                {
                    bool seasons = false, episodes = false;
                    if (state)
                        for (const anime::Series &s : state->series)
                            if (!s.details_pending)
                            {
                                seasons = seasons || !s.seasons.empty();
                                episodes = episodes || anime::episode_total(s) > 0;
                            }
                    caps.seasons = seasons;
                    caps.episodes = episodes;
                }
                return anime::capability_lines(caps);
            }
        if (!state || !state->loaded)
            return {};
        return anime::report_for(*state, anime::SourceClass::native_provider).capabilities;
    }

    [[nodiscard]] bool site_search() const
    {
        const anime::ProviderConfig *c = config();
        if (!c || c->type != anime::ProviderType::website)
            return false;
        const anime::WebsiteAdapter *a = anime::find_website_adapter(c->adapter);
        return a && a->capabilities(*c).search;
    }

    void rebuild()
    {
        const anime::ProviderConfig *c = config();
        actions_.clear();
        if (!c)
            return;
        const anime::ProviderState *state = app_.anime_registry().state(id_);
        actions_.push_back({ActionKind::search, "Search", Icon::search});
        if (state && !state->next_page.empty())
            actions_.push_back({ActionKind::more,
                                app_.anime_more_loading(id_) ? "Loading More..." : "Load More",
                                Icon::forward});
        actions_.push_back({ActionKind::check, "Check Again", Icon::refresh});
        if (c->type == anime::ProviderType::website)
        {
            actions_.push_back({ActionKind::website, "Open Website", Icon::home});
            actions_.push_back(
                {ActionKind::toggle, c->enabled ? "Turn Off" : "Turn On", Icon::stop});
        }
        actions_.push_back({ActionKind::details, "Details", Icon::info});
        action_ = std::clamp(action_, 0, static_cast<int>(actions_.size()) - 1);

        std::vector<Shelf> rows;
        const std::vector<anime::Series> empty;
        const std::vector<anime::Series> &series = state ? state->series : empty;
        if (!query_.empty())
        {
            Shelf results{"Results for \"" + query_ + "\" (" + std::to_string(results_.size()) +
                              ")",
                          {},
                          true};
            for (const std::string &sid : results_)
                if (const anime::Series *s = app_.anime_registry().find_series(id_, sid))
                    results.items.push_back(series_card(*s));
            if (results.items.empty())
            {
                MediaItem none =
                    action_card("none", "Nothing found", "Try another title",
                                "The provider found nothing for this search.", 0x76819a);
                results.items.push_back(std::move(none));
            }
            rows.push_back(std::move(results));
        }
        Shelf playable{"Available to Watch", {}, true};
        for (const anime::Series &s : series)
            if (checked_playable(s))
                playable.items.push_back(series_card(s));
        if (!playable.items.empty())
            rows.push_back(std::move(playable));
        constexpr std::size_t kPerRow = 12;
        for (std::size_t i = 0; i < series.size(); i += kPerRow)
        {
            const std::size_t end = std::min(series.size(), i + kPerRow);
            Shelf row{i == 0 ? "Catalogue"
                             : "Catalogue " + std::to_string(i + 1) + "\xE2\x80\x93" +
                                   std::to_string(end),
                      {},
                      true};
            for (std::size_t k = i; k < end; ++k)
            {
                MediaItem card = series_card(series[k]);
                card.subtitle = card.meta; // the provider is this screen's
                row.items.push_back(std::move(card));
            }
            rows.push_back(std::move(row));
        }
        if (state && !state->next_page.empty() && !rows.empty())
        {
            MediaItem more =
                action_card("more", app_.anime_more_loading(id_) ? "Loading..." : "Load More",
                            "The next page of the catalogue",
                            "Lists the next page of " + c->name + "'s catalogue.", 0x35c79a);
            more.provider = kMoreCard;
            more.portrait = true;
            rows.back().items.push_back(std::move(more));
        }
        shelves_.set(std::move(rows), true);
        if (zone_ == 1 && shelves_.empty())
            zone_ = 0;
        app_.mark_dirty();
    }

    void run(ActionKind kind)
    {
        const anime::ProviderConfig *c = config();
        if (!c)
            return;
        switch (kind)
        {
        case ActionKind::search:
            ask_search();
            return;
        case ActionKind::more:
            app_.load_more_anime(id_);
            return;
        case ActionKind::check:
            app_.refresh_anime_provider(true, id_);
            recheck();
            app_.toast("Checking " + c->name + " again...", th::kInfo);
            return;
        case ActionKind::website:
            open_on_website(app_, *c, c->address, c->name);
            return;
        case ActionKind::toggle:
        {
            const bool on = !c->enabled;
            const std::string name = c->name;
            if (!app_.anime_registry().set_enabled(id_, on))
            {
                app_.toast(app_.anime_registry().last_error(), th::kWarning);
                return;
            }
            if (on)
                app_.refresh_anime_provider(false, id_);
            app_.toast(on ? name + " turned on" : name + " turned off (kept, not loaded)",
                       th::kInfo);
            rebuild();
            return;
        }
        case ActionKind::details:
            show_details();
            return;
        }
    }

    void open(const MediaItem &m)
    {
        if (m.provider == kMoreCard)
        {
            app_.load_more_anime(id_);
            return;
        }
        if (m.provider == kActionCard)
            return;
        app_.open_item(m);
    }

    void ask_search()
    {
        const anime::ProviderConfig *c = config();
        if (!c)
            return;
        app_.open_keyboard("Search " + c->name, query_, 80, false,
                           [this, alive = alive_](bool ok, const std::string &text)
                           {
                               if (!*alive || !ok)
                                   return;
                               search(text);
                           });
    }

    void search(const std::string &text)
    {
        query_ = text;
        results_.clear();
        if (query_.empty())
        {
            rebuild();
            return;
        }
        if (!site_search())
        {
            // Titles listed so far (a catalogue, PeerTube, or a site without search).
            for (const anime::SeriesHit &hit : app_.anime_registry().search(query_, {}))
                if (hit.series->provider_id == id_)
                    results_.push_back(hit.series->id);
            rebuild();
            zone_ = shelves_.empty() ? 0 : 1;
            return;
        }
        searching_ = true;
        const anime::ProviderConfig provider = *config();
        const anime::ProviderContext context = app_.anime_registry().context;
        const std::string query = query_;
        const int token = ++token_;
        App &app = app_;
        app_.jobs().run(
            [provider, context, query, token, alive = alive_, &app, this]
            {
                anime::SiteListing found =
                    anime::search_website(provider, query, context, net::make_cancel_flag());
                app.jobs().post(
                    [found = std::move(found), token, alive, this]() mutable
                    {
                        if (!*alive || token != token_)
                            return;
                        searching_ = false;
                        if (!found.ok)
                        {
                            app_.toast("Search failed: " + found.error, th::kWarning);
                            rebuild();
                            return;
                        }
                        for (const anime::Series &s : found.series)
                            results_.push_back(s.id);
                        app_.anime_registry().merge_series(id_, std::move(found.series), nullptr);
                        rebuild();
                        zone_ = 1;
                        shelves_.focus(0, 0);
                    });
            });
    }

    void show_details()
    {
        const anime::ProviderConfig *c = config();
        if (!c)
            return;
        std::vector<InfoLine> lines;
        lines.push_back({"Kind", anime::provider_type_label(c->type), th::kText});
        if (c->type == anime::ProviderType::website)
        {
            const anime::WebsiteAdapter *a = anime::find_website_adapter(c->adapter);
            lines.push_back({"Adapter", a ? a->label() : c->adapter, th::kText});
            lines.push_back({"Homepage", url::redact(c->address), th::kTextSecondary});
            if (!c->original_url.empty() && c->original_url != c->address)
                lines.push_back({"Entered as", url::redact(c->original_url), th::kTextSecondary});
            lines.push_back(
                {"Status", c->enabled ? "On" : "Turned off (kept, not loaded)", th::kText});
            lines.push_back({"Saved in", anime::kWebsiteProvidersFile, th::kTextMuted});
        }
        else
            lines.push_back(
                {"Address", c->builtin ? c->address : url::redact(c->address), th::kTextSecondary});
        if (const anime::ProviderState *state = app_.anime_registry().state(id_))
            for (const InfoLine &l : report_lines(provider_report(app_.anime_registry(), *state)))
                if (!l.label.empty())
                    lines.push_back(l);
        // What verified it: the player's own measurements on this console.
        if (const anime::NativePlayback *n = app_.anime_registry().native_playback(id_))
        {
            lines.push_back({"Verified", format_date(n->at) + ": " + n->episode, th::kSuccess});
            lines.push_back({"Decoder", n->decoder, th::kTextSecondary});
            lines.push_back({"Measured",
                             std::to_string(n->frames) + " pictures presented, " +
                                 std::to_string(n->audio_frames) + " audio frames; " + n->video +
                                 (n->audio.empty() ? "" : ", " + n->audio),
                             th::kTextSecondary});
        }
        lines.push_back({"",
                         "An episode is offered as playable only when its video file was "
                         "checked: DRM-free MP4, MPEG-TS, Matroska or HLS. Web players, "
                         "protected streams and sign-ins stay in the browser.",
                         th::kTextMuted});
        app_.push(std::make_unique<InfoScreen>(app_, c->name, std::move(lines), "", nullptr));
    }

    // Check Again on a website provider runs the whole check again - catalogue,
    // a few series, their videos - and keeps what it found its episodes do.
    void recheck()
    {
        const anime::ProviderConfig *c = config();
        if (!c || c->type != anime::ProviderType::website || checking_)
            return;
        checking_ = true;
        const std::string address = c->original_url.empty() ? c->address : c->original_url;
        const std::string name = c->name;
        const anime::ProviderContext context = app_.anime_registry().context;
        App &app = app_;
        app_.jobs().run(
            [address, name, context, alive = alive_, &app, this]
            {
                anime::ProviderReport report =
                    anime::check_source(address, name, context, net::make_cancel_flag(), nullptr);
                app.jobs().post(
                    [report = std::move(report), alive, this]
                    {
                        if (!*alive)
                            return;
                        checking_ = false;
                        app_.anime_registry().set_playback(id_, report.playback);
                        // No answer: say so now; the last full check stays saved.
                        const bool silent = report.source == anime::SourceClass::unreachable ||
                                            report.playback == anime::PlaybackClass::unavailable;
                        const anime::PlaybackClass now =
                            silent ? anime::PlaybackClass::unavailable
                                   : app_.anime_registry().playback_of(id_);
                        app_.toast(name_of() + ": " + anime::playback_class_label(now),
                                   playback_color(now));
                        rebuild();
                    });
            });
    }

    [[nodiscard]] std::string name_of() const
    {
        const anime::ProviderConfig *c = config();
        return c ? c->name : std::string{};
    }

    void ask_remove()
    {
        const anime::ProviderConfig *c = config();
        if (!c || c->builtin)
        {
            app_.toast("Built-in providers stay", th::kInfo);
            return;
        }
        const std::string id = id_, name = c->name;
        app_.push(make_confirm_screen(
            app_, "Remove \"" + name + "\"?",
            "Its series leave Anime. Your watch history keeps its entries; they say the series "
            "is no longer offered.",
            "Remove",
            [&app = app_, id]
            {
                if (!app.anime_registry().remove(id))
                {
                    app.toast(app.anime_registry().last_error(), th::kWarning);
                    return;
                }
                app.toast("Provider removed", th::kInfo);
            }));
    }

    std::string id_;
    std::vector<Action> actions_;
    ShelfView shelves_;
    std::string query_;
    std::vector<std::string> results_; // series ids of the last search
    int zone_ = 0;
    int action_ = 0;
    int token_ = 0;
    bool searching_ = false;
    bool checking_ = false;
    bool moving_ = false;
    bool more_ = false;
    std::uint64_t generation_ = static_cast<std::uint64_t>(-1);
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

// ---------------------------------------------------------------------------
// Add Streaming Provider: a website's address -> detect -> capabilities and
// a preview -> add -> the provider's home.
class AddProviderScreen final : public Screen
{
  public:
    AddProviderScreen(App &app, std::string address, std::string name)
        : Screen{app}, address_{std::move(address)}, name_{std::move(name)}
    {
        if (!address_.empty())
            detect();
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
            row_ = row_ == 4 ? 2 : std::max(0, row_ - 1);
            return;
        case input::Button::down:
            row_ = std::min(row_ >= 3 ? row_ : row_ + 1, 4);
            return;
        case input::Button::left:
            if (row_ == 4)
                row_ = 3;
            return;
        case input::Button::right:
            if (row_ == 3)
                row_ = 4;
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
        return detecting_;
    }
    [[nodiscard]] std::string screen_id() const override
    {
        return "add-provider";
    }
    [[nodiscard]] std::string focus_id() const override
    {
        static const char *const kRows[] = {"address", "name", "anilist-id", "detect"};
        if (detecting_)
            return "detecting";
        if (row_ < 4)
            return kRows[row_];
        return !existing_.empty()   ? "open-provider"
               : browser_report()   ? "open-in-websites"
               : catalogue_report() ? "add-as-catalogue"
                                    : "add-provider";
    }

    void render(ui::Painter &p, std::uint64_t now_ms) override
    {
        p.text(th::kMarginX, 146, "Add Streaming Provider", th::kTitle, th::kText);
        p.wrapped(th::kMarginX, 208,
                  "Enter a website's address. AKENO STREAM recognises PeerTube instances, websites "
                  "that describe their catalogue with schema.org structured data, AKENO "
                  "catalogues (JSON) and PeerTube channels, and lists their series here. Any other "
                  "website stays in Websites.",
                  th::kBody, th::kTextSecondary, 1720, 2);
        const struct
        {
            const char *label;
            std::string value;
            const char *placeholder;
        } fields[3] = {{"Website address", address_, "https://video.blender.org"},
                       {"Name (optional)", name_, "Uses the site's own name"},
                       {"AniList id (optional)",
                        anilist_id_ ? std::to_string(anilist_id_) : std::string{},
                        "Only for a PeerTube channel that is one series"}};
        int y = 300;
        const int lw = 820;
        for (int i = 0; i < 3; ++i)
        {
            const bool focused = row_ == i;
            p.text(th::kMarginX, y, fields[i].label, th::kCaptionStrong,
                   focused ? th::kText : th::kTextMuted);
            const Rect box{th::kMarginX, y + 32, lw, 64};
            p.s.fill_rounded(box, 16, focused ? th::kText : th::kSurfaceRaised);
            if (fields[i].value.empty())
            {
                // An example, clearly not something entered: "e.g." in front,
                // a muted colour on either background and an EXAMPLE tag.
                const Pixel hint = focused ? hex_pixel(0x6b7488) : th::kTextMuted;
                const bool example = i == 0;
                const int tx = p.text(box.x + 24, box.y + 16,
                                      example ? std::string{"e.g. "} + fields[i].placeholder
                                              : std::string{fields[i].placeholder},
                                      th::kBody, hint, lw - 200);
                if (example)
                    p.chip(box.x + 24 + tx + 16, box.y + 16, "EXAMPLE",
                           focused ? hex_pixel(0xdfe3ec) : th::kSurfaceHighlight, hint);
            }
            else
                p.text(box.x + 24, box.y + 16, fields[i].value, th::kBody,
                       focused ? th::kTextOnAccent : th::kText, lw - 48);
            y += 118;
        }
        int bx = th::kMarginX;
        bx += p.button(bx, y + 6, detecting_ ? "Detecting..." : "Detect Provider", row_ == 3,
                       th::kAccentAnime, Icon::search) +
              18;
        const bool can_add = report_ && report_->importable;
        p.button(bx, y + 6,
                 !existing_.empty()   ? "Open Provider"
                 : browser_report()   ? "Open in Websites"
                 : catalogue_report() ? "Add as Catalogue"
                                      : "Add Provider",
                 row_ == 4,
                 can_add || !existing_.empty() ? th::kSuccess
                 : browser_report()            ? th::kInfo
                                               : th::kTextMuted,
                 browser_report()    ? Icon::home
                 : existing_.empty() ? Icon::check
                                     : Icon::play);
        // Where the flow is.
        const char *steps[] = {"1  Address", "2  Detect", "3  Review", "4  Add"};
        const int done = address_.empty() ? 0 : !report_ ? 1 : can_add ? 3 : 2;
        int sx = th::kMarginX;
        for (int i = 0; i < 4; ++i)
            sx += p.chip(sx, y + 104, steps[i],
                         i < done    ? gfx::with_alpha(th::kSuccess, 200)
                         : i == done ? gfx::with_alpha(th::kAccentAnime, 220)
                                     : th::kSurfaceRaised,
                         i <= done ? th::kTextOnAccent : th::kTextMuted) +
                  10;
        render_report(p, now_ms,
                      {th::kMarginX + lw + 60, 300,
                       th::kWidth - th::kMarginX - (th::kMarginX + lw + 60), 690});
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        return {{Glyph::cross, row_ < 3 ? "Edit"
                               : row_ == 3
                                   ? "Detect"
                                   : (existing_.empty() && !browser_report() ? "Add" : "Open")},
                {Glyph::dpad, "Choose"},
                {Glyph::circle, "Back"}};
    }

  private:
    // A website that cannot be a provider but plays in the PS5 browser
    // (AnimePahe): it can be opened there - its own player, not AKENO's.
    [[nodiscard]] bool browser_report() const
    {
        return report_ && !report_->importable && existing_.empty() &&
               report_->playback == anime::PlaybackClass::browser_only &&
               !report_->browser_hint.empty();
    }

    // A report whose provider can be added as a catalogue only: series and
    // details, no Play buttons.
    [[nodiscard]] bool catalogue_report() const
    {
        return report_ && report_->importable && !anime::plays_natively(report_->playback) &&
               report_->playback != anime::PlaybackClass::unknown;
    }

    static Pixel hex_pixel(std::uint32_t rgb)
    {
        return gfx::rgba(static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8),
                         static_cast<std::uint8_t>(rgb), 255);
    }

    void render_report(ui::Painter &p, std::uint64_t now_ms, const Rect &panel)
    {
        p.panel(panel, 24, gfx::with_alpha(th::kSurface, 230));
        const int x = panel.x + 32;
        const int w = panel.w - 64;
        if (detecting_)
        {
            p.text(x, panel.y + 24, "Detecting...", th::kHeading, th::kText);
            p.spinner(x + 24, panel.y + 120, 20, now_ms, th::kAccentAnime);
            p.wrapped(x + 64, panel.y + 100,
                      "Reading the site, looking for an adapter that recognises it, listing its "
                      "catalogue and checking up to 12 episodes' videos.",
                      th::kBody, th::kTextSecondary, w - 64, 4);
            return;
        }
        if (!report_)
        {
            p.text(x, panel.y + 24, "How it works", th::kHeading, th::kText);
            const char *lines[] = {
                "1. Enter the homepage of a website, PeerTube instance or catalogue.",
                "2. Detect: an adapter must recognise what the site publishes - a page that "
                "merely loads is not a provider.",
                "3. Review what it offers: catalogue, search, seasons, episodes, and how many "
                "checked episodes play in AKENO STREAM's own player.",
                "4. Add it: its home opens, with its catalogue and search.",
                "Protected streams (DRM), sign-ins and web players are never played here."};
            int y = panel.y + 90;
            for (const char *line : lines)
                y += p.wrapped(x, y, line, th::kBody, th::kTextSecondary, w, 3) + 14;
            return;
        }
        const anime::ProviderReport &r = *report_;
        const bool ok = r.importable || !existing_.empty();
        std::string heading =
            !existing_.empty() ? "Already one of your providers"
            : catalogue_report()
                ? (r.playback == anime::PlaybackClass::browser_only ? "Website only"
                                                                    : "Catalogue only")
            : r.importable ? (r.adapter.empty() ? "A streaming provider" : "Recognised")
                           : "Not a streaming provider";
        if (r.importable && !r.name.empty())
            heading += ": " + r.name;
        p.text(x, panel.y + 24, heading, th::kHeading, ok ? th::kSuccess : th::kWarning, w);
        int y = panel.y + 84;
        // Posters of the first series: what Add brings in (a known website's
        // reasons come first instead).
        if (!r.preview_art.empty() && r.importable && r.known_site.empty())
        {
            int px = x;
            for (std::size_t i = 0; i < r.preview_art.size() && i < 5; ++i)
            {
                const Rect art{px, y, 92, 138};
                const gfx::Image *img = r.preview_art[i].empty()
                                            ? nullptr
                                            : app_.images().get(r.preview_art[i], art.w, art.h);
                if (img)
                    p.s.draw_image(*img, art.x, art.y, 255, 10);
                else
                    p.placeholder(art, i < r.preview.size() ? r.preview[i] : std::string{},
                                  ui::accent_for(i < r.preview.size() ? r.preview[i] : "x"), 10);
                px += art.w + 14;
            }
            y += 156;
        }
        std::vector<InfoLine> lines;
        if (!r.adapter_label.empty())
            lines.push_back({"Recognised by", r.adapter_label, th::kText});
        for (InfoLine &l : report_lines(r))
            lines.push_back(std::move(l));
        for (const std::string &e : r.evidence)
            lines.push_back({"Found", e, th::kTextMuted});
        for (const InfoLine &l : lines)
        {
            if (y > panel.bottom() - 46)
                break;
            if (l.label.empty())
                y += p.wrapped(x, y, l.text, th::kBody, l.color, w, 4) + 10;
            else
            {
                p.text(x, y, l.label, th::kCaptionStrong, th::kTextMuted, 210);
                y += p.wrapped(x + 228, y, l.text, th::kCaption, l.color, w - 228, 3) + 8;
            }
        }
    }

    void activate()
    {
        auto alive = alive_;
        switch (row_)
        {
        case 0:
            app_.open_keyboard("Website address",
                               address_.empty() ? std::string{"https://"} : address_,
                               anime::kMaxAddressLength, false,
                               [this, alive](bool ok, const std::string &text)
                               {
                                   if (!ok || !*alive)
                                       return;
                                   set_address(text);
                               });
            return;
        case 1:
            app_.open_keyboard("Provider name (empty: the site's own)", name_, 60, false,
                               [this, alive](bool ok, const std::string &text)
                               {
                                   if (ok && *alive)
                                       name_ = text;
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
            if (!existing_.empty())
            {
                open_existing();
                return;
            }
            detect();
            return;
        default:
            if (!existing_.empty())
                open_existing();
            else if (browser_report())
            {
                WebSession session;
                session.url = address_;
                session.title = report_->known_site;
                // The browser opens it directly: AKENO STREAM sends the site nothing.
                session.check_first = false;
                app_.toast(report_->browser_hint, th::kInfo);
                app_.open_web(std::move(session));
            }
            else
                add();
            return;
        }
    }

    void set_address(const std::string &typed)
    {
        report_.reset();
        existing_.clear();
        if (typed.empty() || typed == "https://" || typed == "http://")
        {
            address_.clear();
            return;
        }
        const anime::SiteAddress a = anime::normalize_site_address(typed);
        if (!a.ok)
        {
            address_ = typed;
            app_.toast(a.why, th::kWarning);
            return;
        }
        address_ = a.url;
        detect();
    }

    // An address that is already a provider opens that provider's home.
    bool already_added(const std::string &address)
    {
        const anime::ProviderConfig *c = app_.anime_registry().find_by_address(address);
        if (!c)
            return false;
        existing_ = c->id;
        open_existing();
        return true;
    }

    void open_existing()
    {
        const std::string id = existing_;
        const anime::ProviderConfig *c = app_.anime_registry().config(id);
        if (!c)
        {
            existing_.clear();
            return;
        }
        if (!app_.showing(this))
        {
            // Not on screen (another tab is): Open Provider waits for the user.
            row_ = 4;
            app_.mark_dirty();
            return;
        }
        App &app = app_;
        app.toast("Already added - opening " + c->name, th::kInfo);
        app.pop(); // destroys this screen
        app.push(make_provider_home_screen(app, id));
    }

    void detect()
    {
        if (detecting_)
            return;
        const anime::SiteAddress a = anime::normalize_site_address(address_);
        if (!a.ok)
        {
            app_.toast(a.why, th::kWarning);
            row_ = 0;
            return;
        }
        address_ = a.url;
        if (already_added(address_))
            return;
        detecting_ = true;
        report_.reset();
        App &app = app_;
        const std::string address = address_, name = name_;
        const anime::ProviderContext context = app_.anime_registry().context;
        app_.jobs().run(
            [address, name, context, alive = alive_, &app, this]
            {
                anime::ProviderConfig config;
                anime::ProviderReport report =
                    anime::check_source(address, name, context, net::make_cancel_flag(), &config);
                app.jobs().post(
                    [report = std::move(report), config, alive, this]() mutable
                    {
                        if (!*alive)
                            return;
                        detecting_ = false;
                        config_ = config;
                        report_ = std::move(report);
                        // The address led to a site that is already a provider.
                        if (!config_.address.empty() && already_added(config_.address))
                            return;
                        if (report_->importable)
                            row_ = 4;
                        app_.mark_dirty();
                    });
            });
    }

    void add()
    {
        if (!report_ || !report_->importable)
        {
            app_.toast(report_ ? "This address cannot be used as a provider (see the report)"
                               : "Detect the provider first",
                       th::kWarning);
            row_ = report_ ? row_ : 3;
            return;
        }
        anime::ProviderConfig config = config_;
        if (!name_.empty())
            config.name = name_;
        config.anilist_id = anilist_id_;
        std::string why, existing, added;
        if (!app_.anime_registry().add(config, &why, &existing, &added))
        {
            if (!existing.empty())
            {
                existing_ = existing;
                open_existing();
                return;
            }
            app_.toast(why, th::kWarning);
            return;
        }
        App &app = app_;
        app.refresh_anime(false);
        app.toast("Provider added: " + config.name, th::kSuccess);
        app.pop(); // destroys this screen
        app.push(make_provider_home_screen(app, added));
    }

    std::string address_, name_;
    std::string existing_; // the provider that already has this address
    int anilist_id_ = 0;
    int row_ = 0;
    bool detecting_ = false;
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

std::unique_ptr<Screen> make_provider_home_screen(App &app, std::string provider_id)
{
    return std::make_unique<ProviderHomeScreen>(app, std::move(provider_id));
}

std::unique_ptr<Screen> make_add_provider_screen(App &app, std::string address, std::string name)
{
    // An address that is already a provider opens that provider's home.
    if (const anime::SiteAddress a = anime::normalize_site_address(address);
        a.ok && !address.empty())
        if (const anime::ProviderConfig *c = app.anime_registry().find_by_address(a.url))
        {
            app.toast("Already added - opening " + c->name, th::kInfo);
            return std::make_unique<ProviderHomeScreen>(app, c->id);
        }
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
            std::string why, existing, added;
            if (app.anime_registry().add(config, &why, &existing, &added))
            {
                app.refresh_anime(false);
                app.toast("Provider added: " + config.name, th::kSuccess);
                app.push(make_provider_home_screen(app, added));
            }
            else if (!existing.empty())
                app.push(make_provider_home_screen(app, existing));
            else
                app.toast(why, th::kWarning);
        };
    const bool catalogue = report.importable && !anime::plays_natively(report.playback) &&
                           report.playback != anime::PlaybackClass::unknown;
    app.push(std::make_unique<InfoScreen>(
        app,
        catalogue           ? "A catalogue (no playback here)"
        : report.importable ? "A native streaming provider"
                            : "Not a streaming provider",
        std::move(lines), catalogue ? "Add as Catalogue" : "Add to Anime", std::move(import)));
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
