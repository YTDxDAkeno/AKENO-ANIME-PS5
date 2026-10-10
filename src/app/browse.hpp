// AKENO STREAM PS5 - Shared pieces of the browse screens.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/screens.hpp"
#include "app/shelf_view.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace akeno
{
inline constexpr int kHeroTop = 150;
inline constexpr int kShelvesTop = 530;

// Wide artwork behind the hero text, fading into the background on the left,
// top and bottom edges. Without artwork a soft accent glow keeps the area alive.
void hero_art(ui::Painter &p, App &app, const MediaItem &item, std::uint8_t opacity = 235);
void hero_text(ui::Painter &p, const MediaItem &item, ui::Pixel accent);
void spinner_panel(ui::Painter &p, std::uint64_t now_ms, ui::Pixel accent, std::string_view label);
void error_panel(ui::Painter &p, const std::string &message, ui::Pixel accent);
// A card that switches mode or opens a screen (provider "mode").
MediaItem mode_card(const char *id, const char *title, const char *subtitle,
                    const char *description, std::uint32_t accent);

// ---------------------------------------------------------------------------
// Generic browse screen: hero + shelves, loaded asynchronously.
class BrowseScreen : public Screen
{
  public:
    using Loader = std::function<ShelvesResult(const net::CancelFlag &)>;

    BrowseScreen(App &app, ui::Pixel accent, Loader loader, Provider *provider,
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
                            app_.toast(result.error, ui::theme::kWarning);
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
                           fav ? ui::theme::kSuccess : ui::theme::kInfo);
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
            shelves_.render(p,
                            {ui::theme::kMarginX, kShelvesTop,
                             ui::theme::kWidth - 2 * ui::theme::kMarginX, 470},
                            app_.images(), &app_.store(), true);
        if (loading_ && shelves_.empty())
            spinner_panel(p, now_ms, accent_, "Loading...");
        else if (loading_)
            p.spinner(ui::theme::kWidth - ui::theme::kMarginX - 30, kShelvesTop + 24, 16, now_ms,
                      accent_);
        if (!error_.empty() && shelves_.empty())
            error_panel(p, error_, accent_);
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        std::vector<Hint> h{{ui::Glyph::cross, "Select"}};
        if (provider_ && provider_->can_search())
            h.push_back({ui::Glyph::triangle, "Search"});
        if (shelves_.focused() && shelves_.focused()->provider != "mode")
            h.push_back({ui::Glyph::square, "Favorite"});
        if (provider_)
            h.push_back({ui::Glyph::options, "Service info"});
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

    ui::Pixel accent_;
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

} // namespace akeno
