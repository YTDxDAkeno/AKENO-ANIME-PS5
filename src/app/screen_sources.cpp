// AKENO STREAM PS5 - Sources mode: the user's own lists, feeds and addresses.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app provides the means to add sources, not the sources: the list starts
// empty and every entry comes from the user, typed on the console or placed
// in sources.json / sources.txt in the install folder from a PC.
#include "app/browse.hpp"
#include "core/fs.hpp"
#include "core/url.hpp"
#include "platform/platform.hpp"

#include <algorithm>

namespace akeno
{
namespace th = ui::theme;
using ui::Glyph;
using ui::Pixel;
using ui::Rect;

namespace
{
std::string trimmed(const std::string &text)
{
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

MediaItem action_card(const char *id, const char *title, const char *subtitle,
                      std::string description, std::uint32_t accent)
{
    MediaItem m;
    m.provider = "action";
    m.id = id;
    m.kind = ItemKind::info;
    m.title = title;
    m.subtitle = subtitle;
    m.description = std::move(description);
    m.accent = accent;
    return m;
}

constexpr char kFormats[] =
    "Supported: M3U/M3U8 lists, AKENO JSON feeds, HLS playlists (MPEG-TS or fragmented MP4, "
    "AES-128), MPEG-TS streams and MP4/MKV files with H.264 or HEVC video.";

// ---------------------------------------------------------------------------
class ConfirmScreen final : public Screen
{
  public:
    ConfirmScreen(App &app, std::string title, std::string message, std::string confirm,
                  std::function<void()> on_confirm)
        : Screen{app}, title_{std::move(title)}, message_{std::move(message)},
          confirm_{std::move(confirm)}, on_confirm_{std::move(on_confirm)}
    {
    }
    [[nodiscard]] bool modal() const override
    {
        return true;
    }
    void handle(input::Button b) override
    {
        if (b == input::Button::cross)
        {
            auto run = std::move(on_confirm_); // pop() destroys this screen
            app_.pop();
            if (run)
                run();
        }
        else if (b == input::Button::circle)
        {
            app_.pop();
        }
    }
    void render(ui::Painter &p, std::uint64_t) override
    {
        p.s.dim(p.s.bounds(), 175);
        const Rect panel{360, 220, 1200, 620};
        p.panel(panel, 32, th::kSurface);
        p.text(panel.x + 60, panel.y + 50, title_, th::kTitle, th::kText, panel.w - 120);
        p.wrapped(panel.x + 60, panel.y + 130, message_, th::kBody, th::kTextSecondary,
                  panel.w - 120, 11);
    }
    [[nodiscard]] std::vector<Hint> hints() const override
    {
        if (!on_confirm_)
            return {{Glyph::circle, "Close"}};
        return {{Glyph::cross, confirm_}, {Glyph::circle, "Cancel"}};
    }

  private:
    std::string title_, message_, confirm_;
    std::function<void()> on_confirm_;
};

// ---------------------------------------------------------------------------
// One source, browsed like a catalogue: groups become shelves.
class SourceScreen final : public BrowseScreen
{
  public:
    SourceScreen(App &app, std::shared_ptr<SourceProvider> provider)
        : BrowseScreen{app, th::kAccentSources,
                       [provider](const net::CancelFlag &cancel) { return provider->home(cancel); },
                       provider.get(), "Search " + provider->entry().name},
          provider_{std::move(provider)}
    {
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        std::vector<Hint> h;
        const MediaItem *item = shelves_.focused();
        if (item)
            h.push_back({Glyph::cross, item->kind == ItemKind::folder ? "Open" : "Play"});
        h.push_back({Glyph::triangle, "Search"});
        if (item)
            h.push_back({Glyph::square, "Favorite"});
        h.push_back({Glyph::options, "Source info"});
        h.push_back({Glyph::circle, "Back"});
        return h;
    }

  protected:
    void render_empty_hero(ui::Painter &p) override
    {
        p.text(th::kMarginX, kHeroTop + 20, "SOURCE", th::kCaptionStrong, accent_);
        p.text(th::kMarginX, kHeroTop + 56, provider_->entry().name, th::kDisplay, th::kText, 1200);
        p.text(th::kMarginX, kHeroTop + 150, url::redact(provider_->entry().url), th::kBody,
               th::kTextSecondary, 1300);
    }

  private:
    std::shared_ptr<SourceProvider> provider_;
};

// ---------------------------------------------------------------------------
// The mode: the list of sources and the ways to add them.
class SourcesScreen final : public BrowseScreen
{
  public:
    explicit SourcesScreen(App &app) : BrowseScreen{app, th::kAccentSources, nullptr, nullptr, ""}
    {
        loaded_ = true;
        rebuild(false);
    }

    void handle(input::Button b) override
    {
        const MediaItem *item = shelves_.focused();
        if (b == input::Button::square)
        {
            if (item && item->provider == "source-entry")
                ask_remove(*item);
            return;
        }
        if (b == input::Button::options)
        {
            show_help();
            return;
        }
        BrowseScreen::handle(b);
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        std::vector<Hint> h{{Glyph::cross, "Select"}};
        const MediaItem *item = shelves_.focused();
        if (item && item->provider == "source-entry" && item->badge != "PC")
            h.push_back({Glyph::square, "Remove"});
        h.push_back({Glyph::options, "How sources work"});
        return h;
    }

  protected:
    void activate(const MediaItem &item) override
    {
        if (item.provider == "source-entry")
            app_.push(make_source_screen(app_, SourceEntry{item.title, item.id, false}));
        else if (item.id == "add-source")
            add_source();
        else if (item.id == "play-address")
            play_address();
        else if (item.id == "help")
            show_help();
    }

    std::vector<Shelf> local_before() override
    {
        std::vector<Shelf> out;
        Shelf mine{"Your Sources", {}, false};
        std::string problem;
        for (const SourceEntry &e : install_folder_sources(&problem))
            mine.items.push_back(entry_card(e));
        for (const SourceEntry &e : app_.store().sources())
            mine.items.push_back(entry_card(e));
        if (!problem.empty() && problem != last_problem_)
            app_.toast(problem, th::kWarning);
        last_problem_ = problem;

        Shelf actions{mine.items.empty() ? "Get Started" : "Add More", {}, false};
        actions.items.push_back(action_card(
            "add-source", "Add a Source", "Playlist, feed or stream address",
            std::string{"Type the address of an M3U list, an AKENO JSON feed or a stream. "} +
                "Easier from a PC: put sources.txt in the install folder. " + kSourcesNotice,
            0x2ec4d6));
        actions.items.push_back(
            action_card("play-address", "Play an Address", "Without saving it",
                        "Plays an HLS, MPEG-TS, MP4 or MKV address once. The type is detected "
                        "automatically.",
                        0x3a7bd5));
        actions.items.push_back(action_card("help", "How Sources Work", "Formats and files",
                                            std::string{kFormats} + " " + kSourcesNotice,
                                            0x9b7bff));
        if (!mine.items.empty())
            out.push_back(std::move(mine));
        out.push_back(std::move(actions));
        return out;
    }

    void render_empty_hero(ui::Painter &p) override
    {
        p.text(th::kMarginX, kHeroTop + 20, "SOURCES", th::kCaptionStrong, accent_);
        p.text(th::kMarginX, kHeroTop + 56, "Your own streams", th::kDisplay, th::kText);
        p.wrapped(th::kMarginX, kHeroTop + 150, kSourcesNotice, th::kBody, th::kTextSecondary, 900,
                  3);
    }

  private:
    MediaItem entry_card(const SourceEntry &e) const
    {
        MediaItem m;
        m.provider = "source-entry";
        m.id = e.url;
        m.kind = ItemKind::folder;
        m.title = e.name;
        const auto parsed = url::parse(e.url);
        m.subtitle = e.from_install_folder ? "From the install folder"
                                           : (parsed ? parsed->host : std::string{});
        m.description = url::redact(e.url) +
                        (e.from_install_folder
                             ? " - listed in the install folder; edit the file there to change it."
                             : " - press Square to remove it.");
        m.badge = e.from_install_folder ? "PC" : "";
        m.accent = ui::accent_for(e.url);
        return m;
    }

    // sources.json and sources.txt next to the app (written from a PC).
    static std::vector<SourceEntry> install_folder_sources(std::string *problem)
    {
        std::vector<SourceEntry> out;
        const std::string dir = platform::app_dir();
        if (auto text = fs::read_text(fs::join(dir, "sources.json"), 1024 * 1024))
            out = parse_source_list(*text, problem);
        if (auto text = fs::read_text(fs::join(dir, "sources.txt"), 1024 * 1024))
        {
            std::string error;
            for (auto &e : parse_source_text(*text, &error))
                out.push_back(std::move(e));
            if (!error.empty() && problem && problem->empty())
                *problem = error;
        }
        for (auto &e : out)
            e.from_install_folder = true;
        return out;
    }

    void add_source()
    {
        if (app_.store().settings().sources_notice_accepted)
        {
            ask_address();
            return;
        }
        auto alive = alive_;
        app_.push(make_confirm_screen(
            app_, "Before you add a source",
            std::string{kSourcesNotice} + "\n\n" + kFormats +
                " Nothing is read out of web pages and no protection is bypassed.",
            "I understand",
            [this, alive]
            {
                if (!*alive)
                    return;
                Settings s = app_.store().settings();
                s.sources_notice_accepted = true;
                app_.store().update_settings(s);
                ask_address();
            }));
    }

    void ask_address()
    {
        auto alive = alive_;
        app_.open_keyboard(
            "Source address", "https://", 2048, false,
            [this, alive](bool ok, const std::string &text)
            {
                if (!ok || !*alive)
                    return;
                const std::string address = trimmed(text);
                const auto parsed = url::parse(address);
                if (!parsed || !parsed->is_http() || parsed->host.empty())
                {
                    app_.toast("Enter an address that starts with http:// or https://",
                               th::kWarning);
                    return;
                }
                app_.open_keyboard(
                    "Name for this source", parsed->host, 60, false,
                    [this, alive, address, host = parsed->host](bool named, const std::string &name)
                    {
                        if (!*alive)
                            return;
                        const std::string chosen = named ? trimmed(name) : std::string{};
                        SourceEntry entry{chosen.empty() ? host : chosen, address, false};
                        if (app_.store().add_source(entry))
                            app_.toast("Source added", th::kSuccess);
                        else
                            app_.toast("This address is already in your sources (or the list is "
                                       "full)",
                                       th::kInfo);
                        if (!app_.store().last_error().empty())
                            app_.report_error("sources", app_.store().last_error());
                        rebuild(true);
                    });
            });
    }

    void play_address()
    {
        auto alive = alive_;
        app_.open_keyboard("Address to play", "https://", 2048, false,
                           [this, alive](bool ok, const std::string &text)
                           {
                               if (!ok || !*alive)
                                   return;
                               const std::string address = trimmed(text);
                               const auto parsed = url::parse(address);
                               if (!parsed || !parsed->is_http() || parsed->host.empty())
                               {
                                   app_.toast("Enter an address that starts with http:// or "
                                              "https://",
                                              th::kWarning);
                                   return;
                               }
                               MediaItem m;
                               m.provider = "source";
                               m.id = address;
                               m.kind = ItemKind::video;
                               std::string title = parsed->path;
                               title = title.substr(title.find_last_of('/') + 1);
                               m.title =
                                   title.empty() ? parsed->host : url::decode_component(title);
                               m.subtitle = parsed->host;
                               m.playable = Playable{guess_source_kind(address), address};
                               app_.play(m);
                           });
    }

    void ask_remove(const MediaItem &item)
    {
        if (item.badge == "PC")
        {
            app_.toast("This source is listed in the install folder - remove it there", th::kInfo);
            return;
        }
        auto alive = alive_;
        app_.push(make_confirm_screen(app_, "Remove \"" + item.title + "\"?",
                                      "The source is removed from this list. Favorites and "
                                      "history entries you played from it stay.",
                                      "Remove",
                                      [this, alive, url = item.id]
                                      {
                                          if (!*alive)
                                              return;
                                          app_.store().remove_source(url);
                                          app_.toast("Source removed", th::kInfo);
                                          rebuild(true);
                                      }));
    }

    void show_help()
    {
        const std::string dir = "/data/homebrew/PPSA99276/";
        app_.push(make_confirm_screen(
            app_, "How sources work",
            std::string{kFormats} +
                "\n\nAdd a source here with the keyboard, or from a PC: put sources.txt "
                "(one \"Name = address\" per line) or sources.json in " +
                dir + " - they appear here the next time you open Sources.\n\n" + kSourcesNotice,
            "", nullptr));
    }

    std::string last_problem_;
};
} // namespace

std::unique_ptr<Screen> make_sources_screen(App &app)
{
    return std::make_unique<SourcesScreen>(app);
}

std::unique_ptr<Screen> make_source_screen(App &app, SourceEntry entry)
{
    return std::make_unique<SourceScreen>(app, std::make_shared<SourceProvider>(std::move(entry)));
}

std::unique_ptr<Screen> make_confirm_screen(App &app, std::string title, std::string message,
                                            std::string confirm_label,
                                            std::function<void()> on_confirm)
{
    return std::make_unique<ConfirmScreen>(app, std::move(title), std::move(message),
                                           std::move(confirm_label), std::move(on_confirm));
}
} // namespace akeno
