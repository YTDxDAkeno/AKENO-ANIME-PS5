// AKENO STREAM PS5 - Home, Anime, YouTube, search, details and sheets.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/screens.hpp"
#include "app/shelf_view.hpp"
#include "core/fs.hpp"
#include "gfx/qr.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cstdio>

namespace akeno
{
namespace th = ui::theme;
using ui::Glyph;
using ui::Icon;
using ui::Pixel;
using ui::Rect;

namespace
{
constexpr int kHeroTop = 150;
constexpr int kShelvesTop = 530;

// Wide artwork behind the hero text, fading into the background on the left,
// top and bottom edges. Without artwork a soft accent glow keeps the area alive.
void hero_art(ui::Painter &p, App &app, const MediaItem &item, std::uint8_t opacity = 235)
{
    const std::string &url = !item.banner_url.empty() ? item.banner_url : item.image_url;
    const Rect art{760, 0, th::kWidth - 760, 600};
    const gfx::Image *image = url.empty() ? nullptr : app.images().get(url, art.w, art.h);
    if (image)
    {
        p.s.draw_image_faded(*image, art.x, art.y, opacity, 560, 150, 280);
        return;
    }
    const Pixel glow = gfx::hex(item.accent ? item.accent : ui::accent_for(item.title));
    p.s.fill_circle(1500, 230, 420, gfx::with_alpha(glow, 14));
    p.s.fill_circle(1560, 200, 260, gfx::with_alpha(glow, 18));
}

void hero_text(ui::Painter &p, const MediaItem &item, Pixel accent)
{
    int y = kHeroTop + 20;
    if (!item.subtitle.empty())
    {
        p.text(th::kMarginX, y, item.subtitle, th::kCaptionStrong, accent, 900);
        y += p.line_height(th::kCaptionStrong) + 4;
    }
    p.text(th::kMarginX, y, item.title, th::kDisplay, th::kText, 1000);
    y += p.line_height(th::kDisplay) + 8;
    if (!item.meta.empty())
    {
        p.text(th::kMarginX, y, item.meta, th::kBody, th::kTextSecondary, 1000);
        y += p.line_height(th::kBody) + 10;
    }
    if (!item.description.empty())
        y +=
            p.wrapped(th::kMarginX, y, item.description, th::kBody, th::kTextSecondary, 880, 3, 2) +
            8;
    int x = th::kMarginX;
    for (std::size_t i = 0; i < item.genres.size() && i < 5; ++i)
        x += p.chip(x, y, item.genres[i], gfx::with_alpha(th::kSurfaceHighlight, 220), th::kText) +
             10;
}

void spinner_panel(ui::Painter &p, std::uint64_t now_ms, Pixel accent, std::string_view label)
{
    p.spinner(th::kWidth / 2, 700, 34, now_ms, accent);
    p.text_center(th::kWidth / 2, 760, label, th::kBody, th::kTextSecondary);
}

void error_panel(ui::Painter &p, const std::string &message, Pixel accent)
{
    const Rect r{th::kMarginX, 600, th::kWidth - 2 * th::kMarginX, 260};
    p.panel(r, th::kPanelRadius, gfx::with_alpha(th::kSurface, 235));
    p.icon(Icon::alert, r.x + 70, r.y + 80, 64, th::kWarning);
    p.text(r.x + 130, r.y + 50, "Something went wrong", th::kHeading, th::kText);
    p.wrapped(r.x + 130, r.y + 102, message, th::kBody, th::kTextSecondary, r.w - 200, 3);
    p.hint(r.x + 130, r.bottom() - 60, Glyph::cross, "Try again");
    (void)accent;
}

MediaItem mode_card(const char *id, const char *title, const char *subtitle,
                    const char *description, std::uint32_t accent)
{
    MediaItem m;
    m.provider = "mode";
    m.id = id;
    m.kind = ItemKind::info;
    m.title = title;
    m.subtitle = subtitle;
    m.description = description;
    m.accent = accent;
    return m;
}

// ---------------------------------------------------------------------------
// Generic browse screen: hero + shelves, loaded asynchronously.
class BrowseScreen : public Screen
{
  public:
    using Loader = std::function<ShelvesResult(const net::CancelFlag &)>;

    BrowseScreen(App &app, Pixel accent, Loader loader, Provider *provider,
                 std::string search_title)
        : Screen{app}, accent_{accent}, loader_{std::move(loader)}, provider_{provider},
          search_title_{std::move(search_title)}
    {
    }

    void load()
    {
        if (!loader_ || loading_)
            return;
        loading_ = true;
        error_.clear();
        auto loader = loader_;
        auto cancel = net::make_cancel_flag();
        cancel_ = cancel;
        App &app = app_;
        auto alive = alive_;
        app_.jobs().run(
            [loader, cancel, &app, alive, this]
            {
                ShelvesResult result = loader(cancel);
                app.jobs().post(
                    [result = std::move(result), alive, this]() mutable
                    {
                        if (!*alive)
                            return;
                        loading_ = false;
                        loaded_ = true;
                        if (!result.ok)
                        {
                            error_ = result.error;
                            app_.report_error("browse", result.error);
                            return;
                        }
                        if (!result.error.empty())
                            app_.toast(result.error, th::kWarning);
                        remote_ = std::move(result.shelves);
                        rebuild(true);
                    });
            });
    }

    ~BrowseScreen() override
    {
        *alive_ = false;
        if (cancel_)
            cancel_->store(true);
    }

    void handle(input::Button b) override
    {
        if (shelves_.handle(b))
            return;
        const MediaItem *item = shelves_.focused();
        switch (b)
        {
        case input::Button::cross:
            if (!error_.empty() && shelves_.empty())
            {
                loaded_ = false;
                load();
            }
            else if (item)
                activate(*item);
            break;
        case input::Button::triangle:
            if (provider_ && provider_->can_search())
                app_.push(make_search_screen(app_, *provider_, accent_, search_title_));
            break;
        case input::Button::square:
            if (item && item->provider != "mode" && item->provider != "link")
            {
                const bool fav = app_.store().toggle_favorite(*item);
                app_.toast(fav ? "Added to Favorites" : "Removed from Favorites",
                           fav ? th::kSuccess : th::kInfo);
                if (rebuild_on_favorite_)
                    rebuild(true);
            }
            break;
        case input::Button::options:
            if (provider_)
                app_.show_provider_status(*provider_);
            break;
        case input::Button::circle:
            if (app_.mode() != Mode::home)
                app_.pop();
            break;
        default:
            break;
        }
    }

    void update(std::uint64_t now_ms) override
    {
        (void)now_ms;
        if (!loaded_ && !loading_ && loader_)
            load();
        moving_ = shelves_.animate(app_.store().settings().reduce_motion);
    }

    [[nodiscard]] bool animating() const override
    {
        return loading_ || moving_;
    }

    void render(ui::Painter &p, std::uint64_t now_ms) override
    {
        const MediaItem *item = shelves_.focused();
        if (item)
        {
            hero_art(p, app_, *item);
            hero_text(p, *item, accent_);
        }
        else
        {
            render_empty_hero(p);
        }
        if (!shelves_.empty())
            shelves_.render(p, {th::kMarginX, kShelvesTop, th::kWidth - 2 * th::kMarginX, 470},
                            app_.images(), &app_.store(), true);
        if (loading_ && shelves_.empty())
            spinner_panel(p, now_ms, accent_, "Loading...");
        else if (loading_)
            p.spinner(th::kWidth - th::kMarginX - 30, kShelvesTop + 24, 16, now_ms, accent_);
        if (!error_.empty() && shelves_.empty())
            error_panel(p, error_, accent_);
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        std::vector<Hint> h{{Glyph::cross, "Select"}};
        if (provider_ && provider_->can_search())
            h.push_back({Glyph::triangle, "Search"});
        if (shelves_.focused() && shelves_.focused()->provider != "mode")
            h.push_back({Glyph::square, "Favorite"});
        if (provider_)
            h.push_back({Glyph::options, "Service info"});
        return h;
    }

    void resumed() override
    {
        rebuild(true);
    }

  protected:
    virtual void activate(const MediaItem &item)
    {
        app_.open_item(item);
    }
    // Shelves shown before/after the loaded ones (Home: continue watching).
    virtual std::vector<Shelf> local_before()
    {
        return {};
    }
    virtual std::vector<Shelf> local_after()
    {
        return {};
    }
    virtual void render_empty_hero(ui::Painter &p)
    {
        (void)p;
    }

    void rebuild(bool keep)
    {
        std::vector<Shelf> all = local_before();
        for (const auto &s : remote_)
            all.push_back(s);
        for (auto &s : local_after())
            all.push_back(std::move(s));
        shelves_.set(std::move(all), keep);
        app_.mark_dirty();
    }

    Pixel accent_;
    Loader loader_;
    Provider *provider_;
    std::string search_title_;
    ShelfView shelves_;
    std::vector<Shelf> remote_;
    bool loading_ = false;
    bool loaded_ = false;
    bool moving_ = false;
    bool rebuild_on_favorite_ = false;
    std::string error_;
    net::CancelFlag cancel_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

// ---------------------------------------------------------------------------
class HomeScreen final : public BrowseScreen
{
  public:
    explicit HomeScreen(App &app) : BrowseScreen{app, th::kAccentHome, nullptr, nullptr, ""}
    {
        rebuild_on_favorite_ = true;
        rebuild(false);
        loaded_ = true;
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        std::vector<Hint> h{{Glyph::cross, "Select"}};
        if (shelves_.focused() && shelves_.focused()->provider != "mode")
            h.push_back({Glyph::square, "Favorite"});
        return h;
    }

  protected:
    std::vector<Shelf> local_before() override
    {
        std::vector<Shelf> out;
        Shelf resume{"Continue Watching", {}, false};
        for (const auto &e : app_.store().continue_watching())
            resume.items.push_back(e.item);
        if (!resume.items.empty())
            out.push_back(std::move(resume));
        Shelf modes{"Explore", {}, false};
        modes.items.push_back(
            mode_card("anime", "Anime", "Mode",
                      "Browse trending, seasonal and top-rated anime from the AniList catalogue, "
                      "with official places to watch each title.",
                      0xff7a3d));
        modes.items.push_back(
            mode_card("youtube", "YouTube", "Mode",
                      "Search and browse YouTube with the official Data API and your own API key.",
                      0xff3d5a));
        modes.items.push_back(mode_card("library", "My Library", "Mode",
                                        "Play your own MP4, MKV and TS files from console storage.",
                                        0x35c79a));
        modes.items.push_back(mode_card(
            "open", "Open Streams", "DRM-free",
            "Public test streams, open movies and the HLS links in your streams.json.", 0x4f8cff));
        modes.items.push_back(mode_card(
            "crunchyroll", "Crunchyroll", "Status",
            "Why Crunchyroll cannot be integrated, and how to watch it legitimately.", 0xf47521));
        out.push_back(std::move(modes));
        return out;
    }

    std::vector<Shelf> local_after() override
    {
        std::vector<Shelf> out;
        out.push_back({"Open Movies & Test Streams", OpenCatalog::public_streams(), false});
        out.push_back({"Offline Test Clips", app_.open_catalog().bundled(), false});
        if (!app_.store().favorites().empty())
            out.push_back({"Favorites", app_.store().favorites(), false});
        return out;
    }
};

// ---------------------------------------------------------------------------
class OpenStreamsScreen final : public BrowseScreen
{
  public:
    explicit OpenStreamsScreen(App &app)
        : BrowseScreen{app, th::kAccentHome, [&app](const net::CancelFlag &c)
                       { return app.open_catalog().home(c); }, &app.open_catalog(), ""}
    {
    }
    [[nodiscard]] std::vector<Hint> hints() const override
    {
        return {{Glyph::cross, "Select"},
                {Glyph::square, "Favorite"},
                {Glyph::options, "Service info"},
                {Glyph::circle, "Back"}};
    }
};

// ---------------------------------------------------------------------------
class AnimeScreen final : public BrowseScreen
{
  public:
    explicit AnimeScreen(App &app)
        : BrowseScreen{app, th::kAccentAnime, [&app](const net::CancelFlag &c)
                       { return app.anilist().home(c); }, &app.anilist(), "Search Anime"}
    {
    }

  protected:
    std::vector<Shelf> local_after() override
    {
        Shelf open{"Open Animated Films (playable)", {}, false};
        for (const auto &m : OpenCatalog::public_streams())
            if (m.attribution.find("Blender") != std::string::npos)
                open.items.push_back(m);
        Shelf services{"Streaming Services", {}, false};
        services.items.push_back(
            mode_card("crunchyroll", "Crunchyroll", "Not integrated - see why",
                      "Crunchyroll offers no API or sign-in for independent apps and protects "
                      "video with DRM. Select to see the details and legal alternatives.",
                      0xf47521));
        return {open, services};
    }
};

// ---------------------------------------------------------------------------
class YouTubeScreen final : public BrowseScreen
{
  public:
    explicit YouTubeScreen(App &app)
        : BrowseScreen{app, th::kAccentYouTube, [&app](const net::CancelFlag &c)
                       { return app.youtube().home(c); }, &app.youtube(), "Search YouTube"}
    {
    }

    void handle(input::Button b) override
    {
        if (!app_.youtube().configured())
        {
            if (b == input::Button::cross)
                enter_key();
            else if (b == input::Button::square)
                load_key_file();
            else if (b == input::Button::options)
                app_.show_provider_status(app_.youtube());
            return;
        }
        if (b == input::Button::cross && !error_.empty() && shelves_.empty())
        {
            loaded_ = false;
            load();
            return;
        }
        BrowseScreen::handle(b);
    }

    void update(std::uint64_t now_ms) override
    {
        if (!app_.youtube().configured())
            return;
        BrowseScreen::update(now_ms);
    }

    void render(ui::Painter &p, std::uint64_t now_ms) override
    {
        if (app_.youtube().configured())
        {
            BrowseScreen::render(p, now_ms);
            return;
        }
        // Setup guide.
        p.text(th::kMarginX, kHeroTop + 10, "YouTube", th::kDisplay, th::kText);
        p.wrapped(th::kMarginX, kHeroTop + 110,
                  "Browse and search YouTube with Google's official YouTube Data API. It needs "
                  "your own free API "
                  "key - AKENO does not ship one, and never asks for your Google password.",
                  th::kBody, th::kTextSecondary, 1100, 3);
        const Rect steps{th::kMarginX, 340, 1110, 600};
        p.panel(steps);
        p.text(steps.x + 40, steps.y + 30, "Get a key in about five minutes", th::kHeading,
               th::kText);
        const char *lines[] = {
            "1.  On a computer, open console.cloud.google.com and create a project.",
            "2.  Enable \"YouTube Data API v3\" for it (APIs & Services > Library).",
            "3.  Create an API key (APIs & Services > Credentials > Create credentials).",
            "4.  Recommended: restrict the key to the YouTube Data API v3.",
            "5.  Enter it here with the on-screen keyboard, or copy it into",
            "     /download0/akeno/youtube-key.txt over FTP and press Square.",
        };
        int y = steps.y + 96;
        for (const char *line : lines)
        {
            p.text(steps.x + 40, y, line, th::kBody, th::kTextSecondary, steps.w - 80);
            y += 50;
        }
        int x = steps.x + 40;
        const int button_y = steps.bottom() - 100;
        x += p.button(x, button_y, "Enter API key", true, th::kAccentYouTube, Icon::key) + 20;
        p.button(x, button_y, "Load key file", false, th::kAccentYouTube, Icon::folder);
        // Capability summary on the right.
        const Rect info{steps.right() + 34, 340, th::kWidth - th::kMarginX - steps.right() - 34,
                        600};
        p.panel(info);
        p.text(info.x + 32, info.y + 30, "What works", th::kHeading, th::kText);
        int iy = info.y + 92;
        for (const auto &c : app_.youtube().info().capabilities)
        {
            if (iy > info.bottom() - 70)
                break;
            const Pixel dot = c.support == Support::available     ? th::kSuccess
                              : c.support == Support::needs_setup ? th::kWarning
                                                                  : th::kError;
            p.status_line(info.x + 32, iy, dot, c.name, th::kBodyStrong, th::kText);
            iy += 38;
            iy +=
                p.wrapped(info.x + 54, iy, c.detail, th::kCaption, th::kTextMuted, info.w - 86, 2) +
                14;
        }
        (void)now_ms;
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        if (!app_.youtube().configured())
            return {{Glyph::cross, "Enter API key"},
                    {Glyph::square, "Load key file"},
                    {Glyph::options, "Service info"}};
        return BrowseScreen::hints();
    }

    void resumed() override
    {
        if (app_.youtube().configured() && !loaded_ && !loading_)
            load();
        BrowseScreen::resumed();
    }

  private:
    void enter_key()
    {
        app_.open_keyboard("YouTube Data API key", "", 39, true,
                           [this](bool ok, const std::string &text)
                           {
                               if (ok)
                                   accept_key(text);
                           });
    }

    void load_key_file()
    {
        const auto text = fs::read_text(fs::join(platform::data_dir(), "youtube-key.txt"), 4096);
        if (!text)
        {
            app_.toast("No youtube-key.txt in " + platform::data_dir(), th::kWarning);
            return;
        }
        std::string key = *text;
        while (!key.empty() && (key.back() == '\n' || key.back() == '\r' || key.back() == ' '))
            key.pop_back();
        accept_key(key);
        if (app_.youtube().configured())
            (void)fs::remove_file(fs::join(platform::data_dir(), "youtube-key.txt"));
    }

    void accept_key(const std::string &key)
    {
        if (!plausible_youtube_key(key))
        {
            app_.toast(
                "That does not look like a YouTube API key (39 characters, starts with AIza).",
                th::kError);
            return;
        }
        app_.store().set_youtube_api_key(key);
        app_.toast("YouTube API key saved", th::kSuccess);
        loaded_ = false;
        error_.clear();
        load();
    }
};

// ---------------------------------------------------------------------------
class SearchScreen final : public Screen
{
  public:
    SearchScreen(App &app, Provider &provider, Pixel accent, std::string title)
        : Screen{app}, provider_{provider}, accent_{accent}, title_{std::move(title)}
    {
        ask();
    }
    ~SearchScreen() override
    {
        *alive_ = false;
        if (cancel_)
            cancel_->store(true);
    }

    void handle(input::Button b) override
    {
        if (results_.handle(b))
            return;
        switch (b)
        {
        case input::Button::cross:
            if (const MediaItem *item = results_.focused())
                app_.open_item(*item);
            else
                ask();
            break;
        case input::Button::triangle:
            ask();
            break;
        case input::Button::square:
            if (const MediaItem *item = results_.focused())
            {
                const bool fav = app_.store().toggle_favorite(*item);
                app_.toast(fav ? "Added to Favorites" : "Removed from Favorites",
                           fav ? th::kSuccess : th::kInfo);
            }
            break;
        case input::Button::circle:
            app_.pop();
            break;
        default:
            break;
        }
    }

    void update(std::uint64_t) override
    {
        moving_ = results_.animate(app_.store().settings().reduce_motion);
    }
    [[nodiscard]] bool animating() const override
    {
        return searching_ || moving_;
    }

    void render(ui::Painter &p, std::uint64_t now_ms) override
    {
        p.text(th::kMarginX, kHeroTop + 10, title_, th::kTitle, th::kText);
        const Rect field{th::kMarginX, kHeroTop + 90, 1100, 72};
        p.s.fill_rounded(field, 36, th::kSurfaceRaised);
        p.icon(Icon::search, field.x + 44, field.y + 36, 30, th::kTextSecondary);
        p.text(field.x + 80, field.y + 18, query_.empty() ? "Press Triangle to type" : query_,
               th::kBody, query_.empty() ? th::kTextMuted : th::kText, field.w - 120);
        if (searching_)
            spinner_panel(p, now_ms, accent_, "Searching...");
        else if (!error_.empty())
            error_panel(p, error_, accent_);
        else if (searched_ && results_.empty())
            p.text(th::kMarginX, 420, "No results for \"" + query_ + "\".", th::kHeading,
                   th::kTextSecondary);
        if (!results_.empty())
        {
            if (const MediaItem *item = results_.focused())
            {
                p.text(1260, kHeroTop + 96, item->title, th::kBodyStrong, th::kText, 560);
                p.text(1260, kHeroTop + 132, item->meta.empty() ? item->subtitle : item->meta,
                       th::kCaption, th::kTextMuted, 560);
            }
            results_.render(p, {th::kMarginX, 400, th::kWidth - 2 * th::kMarginX, 600},
                            app_.images(), &app_.store(), true);
        }
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        return {{Glyph::cross, "Open"},
                {Glyph::triangle, "New search"},
                {Glyph::square, "Favorite"},
                {Glyph::circle, "Back"}};
    }

  private:
    void ask()
    {
        app_.open_keyboard(title_, query_, 80, false,
                           [this, alive = alive_](bool ok, const std::string &text)
                           {
                               if (!*alive)
                                   return;
                               if (ok && !text.empty())
                                   run(text);
                               else if (!searched_)
                                   app_.pop();
                           });
    }

    void run(const std::string &query)
    {
        query_ = query;
        searching_ = true;
        error_.clear();
        if (cancel_)
            cancel_->store(true);
        auto cancel = net::make_cancel_flag();
        cancel_ = cancel;
        auto alive = alive_;
        Provider *provider = &provider_;
        App &app = app_;
        app_.jobs().run(
            [provider, query, cancel, alive, &app, this]
            {
                ItemsResult result = provider->search(query, cancel);
                app.jobs().post(
                    [result = std::move(result), alive, this]() mutable
                    {
                        if (!*alive)
                            return;
                        searching_ = false;
                        searched_ = true;
                        if (!result.ok)
                        {
                            error_ = result.error;
                            app_.report_error("search", result.error);
                            results_.set({});
                            return;
                        }
                        // Split results into rows of five.
                        std::vector<Shelf> rows;
                        const bool portrait =
                            !result.items.empty() && result.items.front().portrait;
                        const std::size_t per_row = portrait ? 6 : 4;
                        for (std::size_t i = 0; i < result.items.size(); i += per_row)
                        {
                            Shelf row;
                            row.title =
                                i == 0 ? std::to_string(result.items.size()) + " results" : "";
                            row.portrait = portrait;
                            for (std::size_t j = i; j < std::min(result.items.size(), i + per_row);
                                 ++j)
                                row.items.push_back(result.items[j]);
                            rows.push_back(std::move(row));
                        }
                        results_.set(std::move(rows));
                        app_.mark_dirty();
                    });
            });
    }

    Provider &provider_;
    Pixel accent_;
    std::string title_;
    std::string query_;
    ShelfView results_;
    bool searching_ = false;
    bool searched_ = false;
    bool moving_ = false;
    std::string error_;
    net::CancelFlag cancel_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

// ---------------------------------------------------------------------------
class DetailsScreen final : public Screen
{
  public:
    DetailsScreen(App &app, MediaItem item, Provider *provider, Pixel accent)
        : Screen{app}, item_{std::move(item)}, provider_{provider}, accent_{accent}
    {
        build_actions();
        if (provider_ && (item_.provider == "anilist" || item_.provider == "youtube"))
            fetch();
    }
    ~DetailsScreen() override
    {
        *alive_ = false;
        if (cancel_)
            cancel_->store(true);
    }

    void handle(input::Button b) override
    {
        if (in_related_)
        {
            if (b == input::Button::up && related_.focused_row() == 0)
            {
                in_related_ = false;
                return;
            }
            if (related_.handle(b))
                return;
            if (b == input::Button::cross)
            {
                if (const MediaItem *m = related_.focused())
                {
                    if (m->provider == "link")
                        app_.show_qr(*m);
                    else
                        app_.open_item(*m);
                }
                return;
            }
        }
        else
        {
            switch (b)
            {
            case input::Button::left:
                action_ = std::max(0, action_ - 1);
                return;
            case input::Button::right:
                action_ = std::min(static_cast<int>(actions_.size()) - 1, action_ + 1);
                return;
            case input::Button::down:
                if (!related_.empty())
                    in_related_ = true;
                return;
            case input::Button::cross:
                if (action_ < static_cast<int>(actions_.size()))
                    run(actions_[static_cast<std::size_t>(action_)].kind);
                return;
            default:
                break;
            }
        }
        if (b == input::Button::square && item_.provider != "link")
        {
            run(ActionKind::favorite);
            return;
        }
        if (b == input::Button::circle)
            app_.pop();
    }

    void update(std::uint64_t) override
    {
        moving_ = related_.animate(app_.store().settings().reduce_motion);
    }
    [[nodiscard]] bool animating() const override
    {
        return loading_ || moving_;
    }

    void resumed() override
    {
        build_actions();
    }

    void render(ui::Painter &p, std::uint64_t now_ms) override
    {
        hero_art(p, app_, item_, 110);
        // Poster or thumbnail.
        const bool portrait = item_.portrait;
        const int pw = portrait ? 300 : 520, ph = portrait ? 450 : 292;
        const Rect poster{th::kMarginX, kHeroTop + 10, pw, ph};
        const gfx::Image *art =
            item_.image_url.empty() ? nullptr : app_.images().get(item_.image_url, pw, ph);
        if (art)
            p.s.draw_image(*art, poster.x, poster.y, 255, th::kCardRadius);
        else
            p.placeholder(poster, item_.title, item_.accent, th::kCardRadius);

        const int tx = poster.right() + 56;
        const int tw = th::kWidth - th::kMarginX - tx;
        int y = kHeroTop + 6;
        if (!item_.subtitle.empty())
        {
            p.text(tx, y, item_.subtitle, th::kCaptionStrong, accent_, tw);
            y += p.line_height(th::kCaptionStrong) + 2;
        }
        p.text(tx, y, item_.title, th::kTitle, th::kText, tw);
        y += p.line_height(th::kTitle) + 4;
        if (!item_.meta.empty())
        {
            p.text(tx, y, item_.meta, th::kBody, th::kTextSecondary, tw);
            y += p.line_height(th::kBody) + 10;
        }
        int cx = tx;
        for (std::size_t i = 0; i < item_.genres.size() && i < 6; ++i)
            cx += p.chip(cx, y, item_.genres[i], gfx::with_alpha(th::kSurfaceHighlight, 220),
                         th::kText) +
                  10;
        if (!item_.genres.empty())
            y += p.line_height(th::kSmall) + 24;
        const int lines = portrait ? 5 : 3;
        if (!item_.description.empty())
            y += p.wrapped(tx, y, item_.description, th::kBody, th::kTextSecondary,
                           std::min(tw, 1100), lines, 2) +
                 16;
        else if (loading_)
            p.spinner(tx + 30, y + 30, 18, now_ms, accent_);

        // Actions.
        int ax = tx;
        const int ay = std::max(y + 6, poster.bottom() - 64);
        for (std::size_t i = 0; i < actions_.size(); ++i)
            ax +=
                p.button(ax, ay, actions_[i].label, !in_related_ && static_cast<int>(i) == action_,
                         accent_, actions_[i].icon) +
                18;
        if (!error_.empty())
            p.text(tx, ay + 80, error_, th::kCaption, th::kWarning, tw);
        if (!item_.attribution.empty())
            p.text(tx, ay + 80 + (error_.empty() ? 0 : 34), item_.attribution, th::kSmall,
                   th::kTextMuted, tw);

        if (!related_.empty())
            related_.render(p, {th::kMarginX, 700, th::kWidth - 2 * th::kMarginX, 300},
                            app_.images(), &app_.store(), in_related_);
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        std::vector<Hint> h{{Glyph::cross, "Select"}};
        if (item_.provider != "link")
            h.push_back(
                {Glyph::square, app_.store().is_favorite(item_.key()) ? "Unfavorite" : "Favorite"});
        h.push_back({Glyph::circle, "Back"});
        return h;
    }

  private:
    enum class ActionKind
    {
        play,
        restart,
        favorite,
        qr,
        status,
    };
    struct Action
    {
        ActionKind kind;
        std::string label;
        Icon icon;
    };

    void build_actions()
    {
        actions_.clear();
        if (item_.playable)
        {
            const double resume = app_.store().resume_position(item_.key());
            if (resume > 0.0)
            {
                actions_.push_back(
                    {ActionKind::play, "Resume " + format_clock(resume), Icon::play});
                actions_.push_back({ActionKind::restart, "Start over", Icon::refresh});
            }
            else
            {
                actions_.push_back({ActionKind::play, "Play", Icon::play});
            }
        }
        if (!item_.external_url.empty())
            actions_.push_back(
                {ActionKind::qr,
                 item_.external_label.empty() ? "Open on phone" : item_.external_label, Icon::qr});
        if (item_.provider != "link" && item_.provider != "crunchyroll")
            actions_.push_back(
                {ActionKind::favorite,
                 app_.store().is_favorite(item_.key()) ? "In Favorites" : "Add to Favorites",
                 app_.store().is_favorite(item_.key()) ? Icon::star : Icon::star_outline});
        if (item_.provider == "anilist")
            actions_.push_back({ActionKind::status, "Why no playback?", Icon::info});
        action_ = std::clamp(action_, 0, std::max(0, static_cast<int>(actions_.size()) - 1));
    }

    void run(ActionKind kind)
    {
        switch (kind)
        {
        case ActionKind::play:
            app_.play(item_);
            break;
        case ActionKind::restart:
            app_.play(item_, 0.0);
            break;
        case ActionKind::favorite:
        {
            const bool fav = app_.store().toggle_favorite(item_);
            app_.toast(fav ? "Added to Favorites" : "Removed from Favorites",
                       fav ? th::kSuccess : th::kInfo);
            build_actions();
            break;
        }
        case ActionKind::qr:
            app_.show_qr(item_);
            break;
        case ActionKind::status:
            if (provider_)
                app_.show_provider_status(*provider_);
            break;
        }
    }

    void fetch()
    {
        loading_ = true;
        auto cancel = net::make_cancel_flag();
        cancel_ = cancel;
        auto alive = alive_;
        Provider *provider = provider_;
        MediaItem item = item_;
        App &app = app_;
        app_.jobs().run(
            [provider, item, cancel, alive, &app, this]
            {
                DetailsResult result = provider->details(item, cancel);
                app.jobs().post(
                    [result = std::move(result), alive, this]() mutable
                    {
                        if (!*alive)
                            return;
                        loading_ = false;
                        if (!result.ok)
                        {
                            error_ = result.error;
                            app_.report_error("details", result.error);
                            return;
                        }
                        // Keep fields the list item had if details lack them.
                        if (result.item.image_url.empty())
                            result.item.image_url = item_.image_url;
                        item_ = std::move(result.item);
                        related_.set(std::move(result.related));
                        build_actions();
                        app_.mark_dirty();
                    });
            });
    }

    MediaItem item_;
    Provider *provider_;
    Pixel accent_;
    std::vector<Action> actions_;
    int action_ = 0;
    ShelfView related_;
    bool in_related_ = false;
    bool loading_ = false;
    bool moving_ = false;
    std::string error_;
    net::CancelFlag cancel_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

// ---------------------------------------------------------------------------
class QrScreen final : public Screen
{
  public:
    QrScreen(App &app, MediaItem item) : Screen{app}, item_{std::move(item)}
    {
        qr_ = gfx::make_qr(item_.external_url, 440);
    }
    [[nodiscard]] bool modal() const override
    {
        return true;
    }
    void handle(input::Button b) override
    {
        if (b == input::Button::circle || b == input::Button::cross)
            app_.pop();
    }
    void render(ui::Painter &p, std::uint64_t) override
    {
        p.s.dim(p.s.bounds(), 175);
        const Rect panel{360, 170, 1200, 720};
        p.panel(panel, 32, th::kSurface);
        if (qr_.valid())
        {
            const int qx = panel.x + 60, qy = panel.y + (panel.h - qr_.height) / 2;
            p.s.fill_rounded({qx - 16, qy - 16, qr_.width + 32, qr_.height + 32}, 20,
                             gfx::rgba(255, 255, 255));
            p.s.draw_image(qr_, qx, qy);
        }
        const int tx = panel.x + 600;
        const int tw = panel.right() - tx - 50;
        int y = panel.y + 80;
        p.text(tx, y, item_.external_label.empty() ? "Open on your phone" : item_.external_label,
               th::kCaptionStrong, th::kAccentHome, tw);
        y += 40;
        y += p.wrapped(tx, y, item_.title, th::kHeading, th::kText, tw, 2) + 16;
        y += p.wrapped(tx, y,
                       item_.description.empty()
                           ? "Scan the code with your phone's camera to open this page."
                           : item_.description,
                       th::kBody, th::kTextSecondary, tw, 6) +
             20;
        p.wrapped(tx, y, item_.external_url, th::kCaption, th::kTextMuted, tw, 3);
    }
    [[nodiscard]] std::vector<Hint> hints() const override
    {
        return {{Glyph::circle, "Close"}};
    }

  private:
    MediaItem item_;
    gfx::Image qr_;
};

// ---------------------------------------------------------------------------
class StatusScreen final : public Screen
{
  public:
    StatusScreen(App &app, ProviderInfo info) : Screen{app}, info_{std::move(info)}
    {
    }
    [[nodiscard]] bool modal() const override
    {
        return true;
    }
    void handle(input::Button b) override
    {
        if (b == input::Button::circle || b == input::Button::cross)
            app_.pop();
    }
    void render(ui::Painter &p, std::uint64_t) override
    {
        p.s.dim(p.s.bounds(), 175);
        const Rect panel{260, 150, 1400, 790};
        p.panel(panel, 32, th::kSurface);
        p.text(panel.x + 60, panel.y + 46, info_.name, th::kTitle, th::kText);
        p.wrapped(panel.x + 60, panel.y + 116, info_.tagline, th::kBody, th::kTextSecondary,
                  panel.w - 120, 2);
        int y = panel.y + 190;
        for (const auto &c : info_.capabilities)
        {
            const Pixel color = c.support == Support::available     ? th::kSuccess
                                : c.support == Support::needs_setup ? th::kWarning
                                                                    : th::kError;
            const Rect row{panel.x + 50, y, panel.w - 100, 124};
            p.s.fill_rounded(row, 18, th::kSurfaceRaised);
            p.s.fill_rounded({row.x, row.y, 8, row.h}, 4, color);
            p.text(row.x + 32, row.y + 16, c.name, th::kBodyStrong, th::kText);
            p.chip(row.right() - 230, row.y + 16, support_label(c.support),
                   gfx::with_alpha(color, 60), color);
            p.wrapped(row.x + 32, row.y + 54, c.detail, th::kCaption, th::kTextSecondary,
                      row.w - 64, 2);
            y += row.h + 14;
        }
        if (!info_.attribution.empty())
            p.text(panel.x + 60, panel.bottom() - 54, info_.attribution, th::kCaption,
                   th::kTextMuted, panel.w - 120);
    }
    [[nodiscard]] std::vector<Hint> hints() const override
    {
        return {{Glyph::circle, "Close"}};
    }

  private:
    ProviderInfo info_;
};
} // namespace

std::unique_ptr<Screen> make_home_screen(App &app)
{
    return std::make_unique<HomeScreen>(app);
}
std::unique_ptr<Screen> make_anime_screen(App &app)
{
    return std::make_unique<AnimeScreen>(app);
}
std::unique_ptr<Screen> make_youtube_screen(App &app)
{
    return std::make_unique<YouTubeScreen>(app);
}
std::unique_ptr<Screen> make_open_streams_screen(App &app)
{
    return std::make_unique<OpenStreamsScreen>(app);
}
std::unique_ptr<Screen> make_search_screen(App &app, Provider &provider, ui::Pixel accent,
                                           std::string title)
{
    return std::make_unique<SearchScreen>(app, provider, accent, std::move(title));
}
std::unique_ptr<Screen> make_details_screen(App &app, MediaItem item, Provider *provider,
                                            ui::Pixel accent)
{
    return std::make_unique<DetailsScreen>(app, std::move(item), provider, accent);
}
std::unique_ptr<Screen> make_qr_screen(App &app, MediaItem item)
{
    return std::make_unique<QrScreen>(app, std::move(item));
}
std::unique_ptr<Screen> make_status_screen(App &app, ProviderInfo info)
{
    return std::make_unique<StatusScreen>(app, std::move(info));
}
} // namespace akeno
