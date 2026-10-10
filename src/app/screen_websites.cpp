// AKENO STREAM PS5 - Websites mode and the Crunchyroll section.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Websites is a TV front page for the embedded browser: an address bar
// (address or search), the user's saved websites with their icons, the
// services AKENO STREAM has sections for, and recently visited sites. The
// list starts empty and has no allow-list: any http(s) site the user adds
// opens as it is, in the console's own browser, with nothing added to it.
#include "app/browse.hpp"
#include "core/fs.hpp"
#include "core/url.hpp"
#include "platform/platform.hpp"
#include "web/site_probe.hpp"

#include <algorithm>

namespace akeno
{
namespace th = ui::theme;
using ui::Glyph;
using ui::Icon;
using ui::Pixel;
using ui::Rect;

namespace
{
constexpr char kCrunchyrollUrl[] = "https://www.crunchyroll.com/";

constexpr char kBrowserNotice[] =
    "Websites open in the PS5's own web browser, shown inside AKENO STREAM. Use the "
    "browser's controls to move the cursor, scroll, type and play; close the browser to come "
    "back.\n\n"
    "Sign-ins happen on the websites' own pages and stay inside the browser: AKENO STREAM "
    "never sees passwords, cookies or what you type there. Whether a video plays depends on "
    "the site and on what the console's browser supports - DRM-protected video needs a DRM "
    "system the browser may not offer.\n\n"
    "You decide which websites to open and are responsible for using them lawfully. Avoid the "
    "PS button while the browser is open; if the browser offers no way out, press L3 and R3 "
    "together.";

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

std::string checks_line(const web::SiteChecks &c)
{
    std::string out;
    const auto part = [&](const char *name, web::Mark m)
    {
        if (m == web::Mark::untested)
            return;
        out += (out.empty() ? "" : ", ") + std::string{name} +
               (m == web::Mark::works ? " works" : " does not work");
    };
    part("page", c.loads);
    part("sign-in", c.signin);
    part("video", c.video);
    part("sound", c.sound);
    return out;
}

} // namespace

MediaItem site_card(const web::Website &w)
{
    MediaItem m;
    m.provider = "website";
    m.id = w.id;
    m.kind = ItemKind::info;
    m.title = w.name;
    m.subtitle = web::display_host(w.url);
    m.image_url = w.icon_url;
    m.icon_art = true;
    m.accent = ui::accent_for(m.subtitle);
    m.badge = w.private_site ? "PRIVATE" : w.mode ? "MODE" : (w.pinned ? "HOME" : "");
    if (const std::uint32_t color = web::site_color(w))
        m.accent = color;
    if (w.letter_icon)
        m.image_url.clear();
    std::string description = w.url;
    if (!w.last_result.empty())
        description += " - last time: " + w.last_result;
    const std::string checks = checks_line(w.checks);
    if (!checks.empty())
        description += ". Your test: " + checks;
    m.description = description + ". Square: options.";
    if (w.visits > 0)
        m.meta = std::to_string(w.visits) + (w.visits == 1 ? " visit" : " visits");
    return m;
}

namespace
{
// ---------------------------------------------------------------------------
class WebsitesScreen final : public BrowseScreen
{
  public:
    explicit WebsitesScreen(App &app) : BrowseScreen{app, th::kAccentWebsites, nullptr, nullptr, ""}
    {
        loaded_ = true;
        rebuild(false);
    }

    void handle(input::Button b) override
    {
        const MediaItem *item = shelves_.focused();
        switch (b)
        {
        case input::Button::triangle:
            with_notice([this] { ask_address(); });
            return;
        case input::Button::square:
            if (item && item->provider == "website")
                site_menu(item->id);
            else if (item && item->provider == "recent")
                recent_menu(item->id, item->title);
            return;
        case input::Button::options:
            app_.push(make_confirm_screen(app_, "How Websites work", kBrowserNotice, "", nullptr));
            return;
        default:
            BrowseScreen::handle(b);
        }
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        std::vector<Hint> h{{Glyph::cross, "Open"}, {Glyph::triangle, "Address or search"}};
        const MediaItem *item = shelves_.focused();
        if (item && (item->provider == "website" || item->provider == "recent"))
            h.push_back({Glyph::square, "Options"});
        h.push_back({Glyph::options, "How it works"});
        return h;
    }

  protected:
    void activate(const MediaItem &item) override
    {
        if (item.provider == "website")
            with_notice([this, id = item.id] { app_.open_item(site_item(id)); });
        else if (item.provider == "recent")
            with_notice([this, url = item.id, title = item.title] { open_url(url, title); });
        else if (item.id == "open-address")
            with_notice([this] { ask_address(); });
        else if (item.id == "add-website")
            add_website();
        else if (item.id == "playback-lab")
            with_notice([this] { app_.push(make_lab_screen(app_)); });
        else if (item.id == "play-link")
            app_.ask_play_link();
        else if (item.id == "find-public-video")
            ask_scan_video();
        else if (item.id == "clear-browser-data")
            confirm_clear_browser_data(app_);
        else if (item.provider == "mode")
            app_.open_item(item);
    }

    std::vector<Shelf> local_before() override
    {
        std::vector<Shelf> out;
        Shelf start{"Browse", {}, false};
        start.items.push_back(action_card(
            "open-address", "Search or Enter Address", "Any website",
            std::string{"Type an address (youtube.com) or words to search with "} +
                web::engine_name(web::engine_from_id(app_.store().settings().web_search)) +
                ". The page opens in the PS5 browser inside AKENO STREAM.",
            0xf5b83d));
        start.items.push_back(action_card(
            "add-website", "Add Website", "Save a site with its name",
            "Type the address and a name. The site appears below with its icon; Square edits, "
            "pins it to Home or removes it. From a PC: websites.txt in the install folder.",
            0x35c79a));
        start.items.push_back(action_card(
            "playback-lab", "Playback Lab", "Why a video plays or not",
            "Measures what the console's browser can play - MP4, streaming (MediaSource, HLS), "
            "video in other sites' frames, DRM - and records what you see on each site.",
            0x5aa9ff));
        start.items.push_back(action_card(
            "find-public-video", "Find Public Video on a Page", "Try the native AKENO player",
            "Enter a public page URL. AKENO checks only declared HTML video / source tags "
            "and public video metadata, without browser cookies or hidden extraction. "
            "Protected and JavaScript-only players cannot be imported.",
            0x2cc4c9));
        start.items.push_back(action_card(
            "play-link", "Play a Video Link", "In AKENO's own player",
            "A DRM-free HLS, MP4, MKV or TS address plays in AKENO STREAM's native player "
            "instead of the browser.",
            0xb18cff));
        start.items.push_back(action_card(
            "clear-browser-data", "Clear Browser Data", "Sign out of all websites",
            "Asks the console's browser to forget its cookies - every website's sign-in. "
            "Experimental: not every firmware lets apps do this.",
            0x8a94a8));
        out.push_back(std::move(start));

        Shelf mine{"Your Websites", {}, false};
        for (const web::Website &w : app_.websites().sites())
            mine.items.push_back(site_card(w));
        if (!mine.items.empty())
            out.push_back(std::move(mine));

        Shelf services{"Sections", {}, false};
        services.items.push_back(mode_card("youtube", "YouTube", "Official embedded player",
                                           "YouTube's own embedded player inside AKENO STREAM, "
                                           "with browsing through the Data API.",
                                           0xff3d5a));
        services.items.push_back(mode_card("crunchyroll", "Crunchyroll", "Website test",
                                           "crunchyroll.com in the browser, its own sign-in, and "
                                           "what the console's browser can and cannot play.",
                                           0xf47521));
        out.push_back(std::move(services));

        Shelf recent{"Recently Visited", {}, false};
        for (const web::RecentVisit &r : app_.websites().recent())
        {
            MediaItem m;
            m.provider = "recent";
            m.id = r.url;
            m.kind = ItemKind::info;
            m.title = r.title.empty() ? web::display_host(r.url) : r.title;
            m.subtitle = web::display_host(r.url);
            m.description = r.url + ". Square: options.";
            m.accent = ui::accent_for(m.subtitle);
            if (const web::Website *w = app_.websites().find_by_url(r.url))
            {
                m.image_url = w->icon_url;
                m.icon_art = true;
            }
            recent.items.push_back(std::move(m));
        }
        if (!recent.items.empty())
            out.push_back(std::move(recent));
        return out;
    }

    void render_empty_hero(ui::Painter &p) override
    {
        p.text(th::kMarginX, kHeroTop + 20, "WEBSITES", th::kCaptionStrong, accent_);
        p.text(th::kMarginX, kHeroTop + 56, "Your web, on the TV", th::kDisplay, th::kText);
    }

  private:
    MediaItem site_item(const std::string &id) const
    {
        MediaItem m;
        m.provider = "website";
        m.id = id;
        return m;
    }

    void open_url(const std::string &url, const std::string &title)
    {
        WebSession session;
        session.url = url;
        session.title = title;
        if (const web::Website *w = app_.websites().find_by_url(url))
        {
            session.site_id = w->id;
            session.title = w->name;
        }
        app_.open_web(std::move(session));
    }

    // The explanation of the browser is confirmed once before its first use.
    void with_notice(std::function<void()> next)
    {
        if (app_.store().settings().websites_notice_accepted)
        {
            next();
            return;
        }
        auto alive = alive_;
        app_.push(make_confirm_screen(app_, "Before the first website", kBrowserNotice, "Continue",
                                      [this, alive, next = std::move(next)]
                                      {
                                          if (!*alive)
                                              return;
                                          Settings s = app_.store().settings();
                                          s.websites_notice_accepted = true;
                                          app_.store().update_settings(s);
                                          next();
                                      }));
    }

    void ask_address()
    {
        auto alive = alive_;
        app_.open_keyboard("Address or search", "", 2048, false,
                           [this, alive](bool ok, const std::string &text)
                           {
                               if (ok && *alive && !text.empty())
                                   app_.open_address(text);
                           });
    }

    // Looks at a site in the background for its icon (and reports problems).
    void fetch_icon(const std::string &id, const std::string &url, bool announce)
    {
        auto alive = alive_;
        App &app = app_;
        app_.jobs().run(
            [id, url, alive, announce, &app, this]
            {
                web::SiteProbe probe = web::probe_site(url, net::make_cancel_flag());
                app.jobs().post(
                    [probe = std::move(probe), id, alive, announce, this]
                    {
                        if (!*alive)
                            return;
                        if (!probe.icon_url.empty())
                            app_.websites().set_icon(id, probe.icon_url);
                        app_.websites().set_metadata(id, probe.theme_color);
                        // A name the user left at the host name becomes the
                        // site's own name (og:site_name / application-name).
                        if (const web::Website *w = app_.websites().find(id);
                            w && !probe.site_name.empty() && w->name == web::display_host(w->url))
                        {
                            web::Website named = *w;
                            named.name = probe.site_name;
                            app_.websites().update(named);
                        }
                        app_.site_modes_changed();
                        if (announce && !probe.problem.empty())
                            app_.toast("Saved. Note: " + probe.problem, th::kWarning);
                        rebuild(true);
                    });
            });
    }

    void add_website()
    {
        auto alive = alive_;
        app_.open_keyboard(
            "Website address", "https://", 2048, false,
            [this, alive](bool ok, const std::string &text)
            {
                if (!ok || !*alive)
                    return;
                const web::Destination d = web::check_address(text);
                if (!d.ok)
                {
                    app_.toast(d.error, th::kWarning);
                    return;
                }
                if (app_.websites().find_by_url(d.url))
                {
                    app_.toast("This address is already in your websites", th::kInfo);
                    return;
                }
                const std::string host = web::display_host(d.url);
                app_.open_keyboard(
                    "Name for this website", host, web::WebsiteStore::kMaxName, false,
                    [this, alive, url = d.url, host, insecure = d.insecure](bool named,
                                                                            const std::string &name)
                    {
                        if (!*alive)
                            return;
                        std::string why, id;
                        if (!app_.websites().add(named ? name : host, url, &why, &id))
                        {
                            app_.toast(why, th::kWarning);
                            return;
                        }
                        app_.toast(insecure ? "Website added (not encrypted: http)"
                                            : "Website added",
                                   insecure ? th::kWarning : th::kSuccess);
                        rebuild(true);
                        fetch_icon(id, url, true);
                    });
            });
    }

    void scan_public_video(const std::string &address)
    {
        auto alive = alive_;
        App &app = app_;
        app_.toast("Checking publicly declared video links...", th::kInfo);
        app_.jobs().run(
            [address, alive, &app]
            {
                web::PublicVideos result =
                    web::probe_public_videos(address, net::make_cancel_flag());
                app.jobs().post(
                    [result = std::move(result), alive, &app]
                    {
                        if (!*alive)
                            return;
                        if (result.videos.empty())
                        {
                            app.push(make_confirm_screen(
                                app, "Native video not found",
                                result.message +
                                    "\n\nA website is not itself a direct video address. "
                                    "Use Sources for a DRM-free MP4/HLS link you are authorized to "
                                    "play.",
                                "", nullptr));
                            return;
                        }
                        std::vector<MenuOption> entries;
                        for (const auto &video : result.videos)
                        {
                            entries.push_back({"Play in AKENO - " + video.source,
                                               url::redact(video.url),
                                               [&app, url = video.url] { app.play_link(url); }});
                        }
                        app.push(make_menu_screen(
                            app, "Native playback candidates",
                            "Only explicitly published media. Not an authentication or DRM bypass.",
                            std::move(entries)));
                    });
            });
    }

    void ask_scan_video()
    {
        auto alive = alive_;
        app_.open_keyboard("Web page containing public video", "https://", 2048, false,
                           [this, alive](bool ok, const std::string &address)
                           {
                               if (ok && *alive && !address.empty())
                                   scan_public_video(address);
                           });
    }

    void site_menu(const std::string &id)
    {
        const web::Website *found = app_.websites().find(id);
        if (!found)
            return;
        const web::Website site = *found;
        auto alive = alive_;
        std::vector<MenuOption> options;
        options.push_back({"Open", site.url, [this, alive, id]
                           {
                               if (*alive)
                                   with_notice([this, id] { app_.open_item(site_item(id)); });
                           }});
        options.push_back({"Find public videos (native)",
                           "Detect declared MP4/HLS links without cookies, DRM or page scripts",
                           [this, alive, url = site.url]
                           {
                               if (*alive)
                                   scan_public_video(url);
                           }});
        options.push_back({"Rename", "Now: " + site.name, [this, alive, id, name = site.name]
                           {
                               app_.open_keyboard(
                                   "Name for this website", name, web::WebsiteStore::kMaxName,
                                   false, [this, alive, id](bool ok, const std::string &text)
                                   { edit(alive, id, ok, text, false); });
                           }});
        options.push_back({"Change address", site.url, [this, alive, id, url = site.url]
                           {
                               app_.open_keyboard(
                                   "Website address", url, 2048, false,
                                   [this, alive, id](bool ok, const std::string &text)
                                   { edit(alive, id, ok, text, true); });
                           }});
        options.push_back({site.mode ? "Mode: on (its own tab)" : "Pin as Mode",
                           site.mode ? "Tab name, icon, colour and start page"
                                     : "Gives the site its own tab after Websites",
                           [this, alive, id, on = site.mode]
                           {
                               if (!*alive)
                                   return;
                               if (!on)
                               {
                                   std::string why;
                                   if (!app_.websites().set_mode(id, true, &why))
                                   {
                                       app_.toast(why, th::kWarning);
                                       return;
                                   }
                                   app_.site_modes_changed();
                                   app_.toast("Added as a mode - L1/R1 reach its tab",
                                              th::kSuccess);
                                   rebuild(true);
                               }
                               open_site_mode_menu(app_, id);
                           }});
        options.push_back(
            {site.pinned ? "Remove from Home" : "Save to Home",
             site.pinned ? "It stays in Websites" : "Adds it to Home's My Websites row",
             [this, alive, id]
             { toggle(alive, id, [](web::Website &w) { w.pinned = !w.pinned; }); }});
        options.push_back(
            {site.private_site ? "Private: on" : "Private: off",
             "Private sites stay out of Recently Visited and Home", [this, alive, id]
             { toggle(alive, id, [](web::Website &w) { w.private_site = !w.private_site; }); }});
        options.push_back({"Record video playback",
                           "What happened when a video played, and its error code",
                           [this, alive, url = site.url, name = site.name]
                           {
                               if (!*alive)
                                   return;
                               const std::string key = web::check_key(url);
                               open_playback_record(app_, key, name, key == "crunchyroll.com");
                           }});
        options.push_back({"Record what works", "Page, sign-in, video and sound, for test reports",
                           [this, alive, id] { record(alive, id); }});
        options.push_back({"Refresh icon", "Looks at the site again for its icon",
                           [this, alive, id, url = site.url]
                           {
                               if (*alive)
                                   fetch_icon(id, url, true);
                           }});
        options.push_back({"Remove", "Removes it from Websites", [this, alive, id, name = site.name]
                           {
                               if (!*alive)
                                   return;
                               app_.push(make_confirm_screen(
                                   app_, "Remove \"" + name + "\"?",
                                   "The website is removed from your list. Sign-ins inside the "
                                   "browser are not affected.",
                                   "Remove",
                                   [this, alive, id]
                                   {
                                       if (!*alive)
                                           return;
                                       app_.websites().remove(id);
                                       app_.site_modes_changed();
                                       app_.toast("Website removed", th::kInfo);
                                       rebuild(true);
                                   }));
                           }});
        app_.push(
            make_menu_screen(app_, site.name, web::display_host(site.url), std::move(options)));
    }

    void recent_menu(const std::string &url, const std::string &title)
    {
        auto alive = alive_;
        std::vector<MenuOption> options;
        options.push_back({"Open", url, [this, alive, url, title]
                           {
                               if (*alive)
                                   with_notice([this, url, title] { open_url(url, title); });
                           }});
        options.push_back({"Find public videos (native)",
                           "Only media publicly declared in the HTML page", [this, alive, url]
                           {
                               if (*alive)
                                   scan_public_video(url);
                           }});
        if (!app_.websites().find_by_url(url))
            options.push_back({"Save to Websites", "Keeps it in Your Websites",
                               [this, alive, url, title]
                               {
                                   if (!*alive)
                                       return;
                                   std::string why, id;
                                   if (app_.websites().add(title, url, &why, &id))
                                   {
                                       app_.toast("Website added", th::kSuccess);
                                       fetch_icon(id, url, false);
                                   }
                                   else
                                       app_.toast(why, th::kWarning);
                                   rebuild(true);
                               }});
        options.push_back({"Remove from Recently Visited", "", [this, alive, url]
                           {
                               if (!*alive)
                                   return;
                               app_.websites().remove_recent(url);
                               rebuild(true);
                           }});
        options.push_back({"Clear Recently Visited", "Removes every entry of this row",
                           [this, alive]
                           {
                               if (!*alive)
                                   return;
                               app_.websites().clear_recent();
                               app_.toast("Recently Visited cleared", th::kInfo);
                               rebuild(true);
                           }});
        app_.push(make_menu_screen(app_, title, web::display_host(url), std::move(options)));
    }

    void edit(const std::shared_ptr<bool> &alive, const std::string &id, bool ok,
              const std::string &text, bool address)
    {
        if (!ok || !*alive)
            return;
        const web::Website *found = app_.websites().find(id);
        if (!found)
            return;
        web::Website site = *found;
        if (address)
        {
            const web::Destination d = web::check_address(text);
            if (!d.ok)
            {
                app_.toast(d.error, th::kWarning);
                return;
            }
            site.url = d.url;
        }
        else
        {
            site.name = text;
        }
        std::string why;
        if (!app_.websites().update(site, &why))
        {
            app_.toast(why, th::kWarning);
            return;
        }
        app_.toast(address ? "Address changed" : "Name changed", th::kSuccess);
        if (address)
            fetch_icon(id, site.url, true);
        rebuild(true);
    }

    void toggle(const std::shared_ptr<bool> &alive, const std::string &id,
                const std::function<void(web::Website &)> &change)
    {
        if (!*alive)
            return;
        const web::Website *found = app_.websites().find(id);
        if (!found)
            return;
        web::Website site = *found;
        change(site);
        app_.websites().update(site);
        rebuild(true);
    }

    void record(const std::shared_ptr<bool> &alive, const std::string &id)
    {
        if (!*alive)
            return;
        const web::Website *found = app_.websites().find(id);
        if (!found)
            return;
        std::vector<RecordRow> rows = {
            {"The page loads", "Text and pictures appear", found->checks.loads},
            {"Sign-in works", "Only if the site has accounts", found->checks.signin},
            {"A video plays", "Picture moves", found->checks.video},
            {"With sound", "You hear the video", found->checks.sound},
        };
        app_.push(make_record_screen(
            app_, "What works on " + web::display_host(found->url),
            "Record what you saw on this console. It goes into the diagnostics report (host name "
            "only) and helps others know what to expect.",
            std::move(rows),
            [this, alive, id](const std::vector<RecordRow> &r)
            {
                if (!*alive || r.size() != 4)
                    return;
                const web::Website *w = app_.websites().find(id);
                if (!w)
                    return;
                web::Website site = *w;
                site.checks = {r[0].mark, r[1].mark, r[2].mark, r[3].mark};
                app_.websites().update(site);
                rebuild(true);
            }));
    }
};

// ---------------------------------------------------------------------------
// Crunchyroll: the real website in the browser, its own sign-in, and the
// evidence of whether its DRM-protected episodes can play on this console.
class CrunchyrollScreen final : public Screen
{
  public:
    explicit CrunchyrollScreen(App &app) : Screen{app}
    {
    }

    void handle(input::Button b) override
    {
        switch (b)
        {
        case input::Button::left:
            action_ = std::max(0, action_ - 1);
            return;
        case input::Button::right:
            action_ = std::min(kActions - 1, action_ + 1);
            return;
        case input::Button::cross:
            run(action_);
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
        const Pixel accent = gfx::hex(0xf47521);
        p.text(th::kMarginX, 150, "CRUNCHYROLL", th::kCaptionStrong, accent);
        p.text(th::kMarginX, 186, "crunchyroll.com in AKENO STREAM", th::kTitle, th::kText);
        const int lw = 820;
        int y = 262;
        y += p.wrapped(th::kMarginX, y,
                       "Opens Crunchyroll's own website in the PS5 browser inside AKENO STREAM. "
                       "Sign in on Crunchyroll's own page there - AKENO STREAM never asks for "
                       "and never sees your Crunchyroll password.",
                       th::kBody, th::kTextSecondary, lw, 4) +
             20;
        y += p.wrapped(th::kMarginX, y,
                       "Crunchyroll's episodes are DRM-protected: they play only if the browser "
                       "offers a licensed DRM system (Widevine, PlayReady or FairPlay) to the "
                       "page. AKENO STREAM does not and will not work around DRM. An error code "
                       "such as KAT-6005 alone does not prove the cause - record it, and the "
                       "Playback Lab's measurements show what is missing.",
                       th::kBody, th::kTextSecondary, lw, 7) +
             20;
        const web::Classification cls = classification();
        const Pixel color = cls.state == web::PlaybackState::works          ? th::kSuccess
                            : cls.state == web::PlaybackState::not_tested   ? th::kTextSecondary
                            : cls.state == web::PlaybackState::undetermined ? th::kWarning
                                                                            : th::kError;
        y += p.wrapped(th::kMarginX, y,
                       std::string{"Episodes: "} + web::state_label(cls.state) +
                           (cls.measured ? " (measured)" : ""),
                       th::kBodyStrong, color, lw, 1) +
             8;
        p.wrapped(th::kMarginX, y,
                  cls.state == web::PlaybackState::not_tested ? verdict().text : cls.reason,
                  th::kCaption, th::kTextSecondary, lw, 5);

        static const char *labels[] = {"Open crunchyroll.com", "Record what happened",
                                       "Playback Lab", "Back"};
        static const Icon icons[] = {Icon::spark, Icon::check, Icon::refresh, Icon::home};
        int x = th::kMarginX;
        for (int i = 0; i < kActions; ++i)
            x += p.button(x, 920, labels[i], i == action_, accent, icons[i]) + 18;

        const Rect panel{th::kMarginX + lw + 60, 150, th::kWidth - 2 * th::kMarginX - lw - 60, 740};
        p.panel(panel, th::kPanelRadius, gfx::with_alpha(th::kSurface, 235));
        p.text(panel.x + 30, panel.y + 24, "On this console", th::kHeading, th::kText);
        int ry = panel.y + 80;
        const auto line = [&](const std::string &label, const std::string &value, Pixel c,
                              const std::string &detail)
        {
            p.s.fill_circle(panel.x + 40, ry + 15, 8, c);
            p.text(panel.x + 62, ry, label, th::kCaptionStrong, th::kText, panel.w - 300);
            p.text_right(panel.right() - 30, ry, value, th::kCaption, c);
            if (!detail.empty())
            {
                p.text(panel.x + 62, ry + 27, detail, th::kSmall, th::kTextMuted, panel.w - 100);
                ry += 56;
            }
            else
                ry += 44;
        };
        for (const Row &r : rows())
        {
            const web::TestRecord *rec = app_.web_tests().get(r.id);
            const web::Outcome o = rec ? rec->outcome : web::Outcome::unknown;
            line(r.label, rec ? web::outcome_label(o) : "Not tested", outcome_color(o),
                 rec && r.id[0] != 'c' ? rec->detail : std::string{});
        }
        const web::PlaybackCheck *check = app_.playback_checks().get(kKey);
        line("Episode playback (your test)",
             check && check->seen != web::Seen::not_tested ? web::state_label(cls.state)
                                                           : "Not tested",
             color, check ? web::seen_label(check->seen) : std::string{});
        line("Last error", check && !check->error_code.empty() ? check->error_code : "None",
             check && !check->error_code.empty() ? th::kError : th::kTextMuted, {});
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        return {{Glyph::cross, "Select"}, {Glyph::dpad, "Choose"}, {Glyph::circle, "Back"}};
    }

  private:
    static constexpr int kActions = 4;
    static constexpr char kKey[] = "crunchyroll.com";

    struct Row
    {
        const char *id;
        const char *label;
    };
    static const std::vector<Row> &rows()
    {
        static const std::vector<Row> list = {
            {"crunchyroll.render", "Website renders (your test)"},
            {"crunchyroll.login", "Sign-in on Crunchyroll's page (your test)"},
            {"crunchyroll.session", "Still signed in next time (your test)"},
            {"mse.available", "Streaming (MediaSource)"},
            {"playback.mse", "MediaSource plays (lab)"},
            {"drm.eme", "DRM interface (EME)"},
            {"drm.widevine", "Widevine"},
            {"drm.playready", "PlayReady"},
            {"drm.fairplay", "FairPlay"},
            {"drm.secure_check", "Secure DRM check"},
        };
        return list;
    }

    static Pixel outcome_color(web::Outcome o)
    {
        return o == web::Outcome::yes       ? th::kSuccess
               : o == web::Outcome::no      ? th::kError
               : o == web::Outcome::partial ? th::kWarning
                                            : th::kTextMuted;
    }

    web::Outcome outcome(const char *id) const
    {
        const web::TestRecord *r = app_.web_tests().get(id);
        return r ? r->outcome : web::Outcome::unknown;
    }

    web::DrmVerdict verdict() const
    {
        return web::drm_verdict(app_.web_tests(), "");
    }

    web::Classification classification() const
    {
        web::PlaybackCheck check;
        if (const web::PlaybackCheck *c = app_.playback_checks().get(kKey))
            check = *c;
        return web::classify(check, true, web::lab_facts(app_.web_tests()));
    }

    void run(int action)
    {
        switch (action)
        {
        case 0:
        {
            WebSession session;
            session.url = kCrunchyrollUrl;
            session.title = "Crunchyroll";
            if (const web::Website *w = app_.websites().find_by_url(kCrunchyrollUrl))
                session.site_id = w->id;
            app_.open_web(std::move(session));
            return;
        }
        case 1:
            record();
            return;
        case 2:
            app_.push(make_lab_screen(app_));
            return;
        default:
            app_.pop();
            return;
        }
    }

    void record()
    {
        App &app = app_;
        std::vector<MenuOption> options;
        options.push_back({"Episode playback", "What the player did, and the error code it showed",
                           [&app] { open_playback_record(app, kKey, "Crunchyroll", true); }});
        options.push_back(
            {"Website, sign-in and session", "Each item separately", [this] { record_site(); }});
        app_.push(make_menu_screen(app_, "Record what happened", "Crunchyroll on this console",
                                   std::move(options)));
    }

    void record_site()
    {
        std::vector<RecordRow> marks;
        for (std::size_t i = 0; i < 3; ++i)
        {
            const web::Outcome o = outcome(rows()[i].id);
            marks.push_back({rows()[i].label, "",
                             o == web::Outcome::yes  ? web::Mark::works
                             : o == web::Outcome::no ? web::Mark::fails
                                                     : web::Mark::untested});
        }
        marks[0].hint = "Crunchyroll's pages appear and can be navigated";
        marks[1].hint = "With Crunchyroll's own sign-in page";
        marks[2].hint = "Close the browser, open Crunchyroll again";
        App &app = app_;
        app_.push(make_record_screen(
            app_, "Crunchyroll on this console",
            "Record what you saw. Website rendering or a successful sign-in alone does not mean "
            "episodes play - record the episode separately.",
            std::move(marks),
            [&app](const std::vector<RecordRow> &r)
            {
                for (std::size_t i = 0; i < r.size() && i < 3; ++i)
                {
                    web::TestRecord rec;
                    rec.id = rows()[i].id;
                    rec.group = "Crunchyroll (your test)";
                    rec.name = rows()[i].label;
                    rec.outcome = r[i].mark == web::Mark::works   ? web::Outcome::yes
                                  : r[i].mark == web::Mark::fails ? web::Outcome::no
                                                                  : web::Outcome::unknown;
                    rec.at = platform::wall_clock_seconds();
                    rec.source = "your test";
                    app.web_tests().set(std::move(rec));
                }
            }));
    }

    int action_ = 0;
};
// ---------------------------------------------------------------------------
// A website as an app mode: its own tab, name, icon, colour and start page.
// It opens in the system browser like any website; the browser does not tell
// apps which page was open, so the mode always starts at the chosen page.
struct TileColor
{
    const char *name;
    std::uint32_t rgb;
};
constexpr TileColor kTileColors[] = {
    {"Automatic", 0},     {"Red", 0xff4d5e},    {"Orange", 0xf47521},
    {"Yellow", 0xf5b83d}, {"Green", 0x35c79a},  {"Teal", 0x2cc4c9},
    {"Blue", 0x5aa9ff},   {"Purple", 0xb18cff}, {"Pink", 0xff6fb5},
};

const char *tile_color_name(std::uint32_t rgb)
{
    for (const TileColor &c : kTileColors)
        if (c.rgb == rgb)
            return c.name;
    return "Custom";
}

class SiteModeScreen final : public Screen
{
  public:
    SiteModeScreen(App &app, std::string id) : Screen{app}, id_{std::move(id)}
    {
    }

    void handle(input::Button b) override
    {
        const web::Website *site = app_.websites().find(id_);
        if (!site)
            return;
        switch (b)
        {
        case input::Button::left:
            action_ = std::max(0, action_ - 1);
            return;
        case input::Button::right:
            action_ = std::min(kActions - 1, action_ + 1);
            return;
        case input::Button::cross:
            run(*site, action_);
            return;
        case input::Button::square:
            open_site_mode_menu(app_, id_);
            return;
        default:
            return;
        }
    }

    void render(ui::Painter &p, std::uint64_t) override
    {
        const web::Website *found = app_.websites().find(id_);
        if (!found)
            return;
        const web::Website &site = *found;
        const Pixel accent = app_.accent();
        const std::string host = web::display_host(site.url);

        // The tile: the site's icon, or a letter on its colour.
        const Rect tile{th::kMarginX, 170, 260, 260};
        p.s.fill_rounded(tile, 48, gfx::with_alpha(accent, 235));
        const gfx::Image *icon = nullptr;
        if (!site.letter_icon && !site.icon_url.empty())
            icon = app_.images().get(site.icon_url, 160, 160);
        if (icon)
        {
            p.s.fill_rounded({tile.x + 30, tile.y + 30, 200, 200}, 36, th::kText);
            p.s.draw_image(*icon, tile.x + (tile.w - icon->width) / 2,
                           tile.y + (tile.h - icon->height) / 2);
        }
        else
        {
            std::string letter = site.name.substr(0, 1);
            for (char &c : letter)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            const th::Type big{150, gfx::Weight::bold};
            p.text_center(tile.x + tile.w / 2, tile.y + (tile.h - p.line_height(big)) / 2, letter,
                          big, th::kTextOnAccent);
        }

        const int tx = tile.right() + 56, tw = th::kWidth - th::kMarginX - tx;
        p.text(tx, 182, "WEBSITE MODE", th::kCaptionStrong, accent);
        p.text(tx, 216, site.name, th::kDisplay, th::kText, tw);
        p.text(tx, 306, host, th::kBody, th::kTextSecondary, tw);
        std::string visits = site.visits > 0 ? std::to_string(site.visits) +
                                                   (site.visits == 1 ? " visit" : " visits")
                                             : std::string{"Not opened yet"};
        if (!site.last_result.empty())
            visits += " - last time: " + site.last_result;
        p.text(tx, 352, visits, th::kCaption, th::kTextMuted, tw);

        int y = 470;
        p.text(th::kMarginX, y, "Starts at", th::kCaptionStrong, th::kTextMuted);
        p.text(th::kMarginX + 220, y, web::start_address(site), th::kBody, th::kText, 1300);
        y += 50;
        const web::PlaybackCheck *check = app_.playback_checks().get(web::check_key(site.url));
        const web::Classification cls = web::classify(check ? *check : web::PlaybackCheck{},
                                                      web::check_key(site.url) == "crunchyroll.com",
                                                      web::lab_facts(app_.web_tests()));
        p.text(th::kMarginX, y, "Video", th::kCaptionStrong, th::kTextMuted);
        p.text(th::kMarginX + 220, y,
               std::string{web::state_label(cls.state)} + (cls.measured ? " (measured)" : ""),
               th::kBodyStrong,
               cls.state == web::PlaybackState::works        ? th::kSuccess
               : cls.state == web::PlaybackState::not_tested ? th::kTextSecondary
                                                             : th::kWarning,
               1300);
        y += 44;
        if (check && !check->error_code.empty())
        {
            p.text(th::kMarginX + 220, y, "Last error: " + check->error_code, th::kCaption,
                   th::kError, 1300);
            y += 40;
        }
        p.wrapped(th::kMarginX, y + 20,
                  "Opens in the PS5 browser inside AKENO STREAM. The browser does not tell apps "
                  "which page you were on, so this mode always starts at the page above. "
                  "Sign-ins stay inside the browser.",
                  th::kCaption, th::kTextMuted, 1400, 3);

        static const char *labels[] = {"Open", "Other start", "Record video", "Mode settings"};
        static const Icon icons[] = {Icon::play, Icon::home, Icon::check, Icon::gear};
        int x = th::kMarginX;
        for (int i = 0; i < kActions; ++i)
        {
            std::string label = labels[i];
            if (i == 1)
                label = site.open_home ? "Open saved page" : "Open homepage";
            x += p.button(x, 860, label, i == action_, accent, icons[i]) + 18;
        }
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        return {{Glyph::cross, "Select"}, {Glyph::dpad, "Choose"}, {Glyph::square, "Settings"}};
    }

  private:
    static constexpr int kActions = 4;

    void open(const web::Website &site, const std::string &url)
    {
        WebSession session;
        session.url = url;
        session.title = site.name;
        session.site_id = site.id;
        app_.open_web(std::move(session));
    }

    void run(const web::Website &site, int action)
    {
        switch (action)
        {
        case 0:
            open(site, web::start_address(site));
            return;
        case 1:
            open(site, site.open_home ? site.url : web::homepage_of(site));
            return;
        case 2:
        {
            const std::string key = web::check_key(site.url);
            open_playback_record(app_, key, site.name, key == "crunchyroll.com");
            return;
        }
        default:
            open_site_mode_menu(app_, id_);
            return;
        }
    }

    std::string id_;
    int action_ = 0;
};
} // namespace

std::unique_ptr<Screen> make_websites_screen(App &app)
{
    return std::make_unique<WebsitesScreen>(app);
}

std::unique_ptr<Screen> make_crunchyroll_screen(App &app)
{
    return std::make_unique<CrunchyrollScreen>(app);
}

std::unique_ptr<Screen> make_site_mode_screen(App &app, std::string site_id)
{
    return std::make_unique<SiteModeScreen>(app, std::move(site_id));
}

void confirm_clear_browser_data(App &app)
{
    app.push(make_confirm_screen(
        app, "Clear the browser's data?",
        "The console's browser forgets its cookies: you are signed out of every website "
        "(YouTube, Crunchyroll and all others) and sites forget their settings. Your saved "
        "websites, modes and AKENO STREAM's own data stay.",
        "Clear",
        [&app]
        {
            std::string error;
            if (app.web_view().clear_cookies(&error))
                app.toast("The browser's cookies were cleared", th::kSuccess);
            else
            {
                app.toast("Not cleared: " + error, th::kWarning);
                app.report_error("browser data", error);
            }
        }));
}

void open_site_mode_menu(App &app, const std::string &id)
{
    const auto site = [&app, id]() -> const web::Website * { return app.websites().find(id); };
    const auto change = [&app, id](const std::function<void(web::Website &)> &edit)
    {
        const web::Website *w = app.websites().find(id);
        if (!w)
            return;
        web::Website copy = *w;
        edit(copy);
        std::string why;
        if (!app.websites().update(copy, &why))
            app.toast(why, th::kWarning);
        app.site_modes_changed();
    };
    ChoiceModel model;
    model.options = [site]
    {
        std::vector<ChoiceOption> out;
        const web::Website *w = site();
        if (!w)
            return out;
        out.push_back({w->mode ? "Shown as a mode: yes" : "Shown as a mode: no",
                       w->mode ? "Choose to remove its tab (the website stays saved)"
                               : "Choose to give it its own tab after Websites"});
        out.push_back({"Tab name: " + web::mode_title(*w),
                       "Short names fit the mode bar best (up to 16 characters)"});
        out.push_back({w->letter_icon ? "Icon: letter tile" : "Icon: the site's own icon",
                       "Choose to switch between the two"});
        out.push_back({w->custom_icon ? "Icon image: your address" : "Icon image: from the site",
                       "Choose to enter the address of a PNG, JPEG or ICO image"});
        out.push_back({std::string{"Colour: "} + tile_color_name(w->tile_color) +
                           (w->tile_color == 0 && !w->theme_color.empty()
                                ? " (the site's " + w->theme_color + ")"
                                : ""),
                       "Choose to try the next colour"});
        out.push_back({w->open_home ? "Starts at: the homepage" : "Starts at: the saved address",
                       w->open_home ? web::homepage_of(*w) : w->url});
        out.push_back(
            {w->pinned ? "On Home: yes" : "On Home: no", "Shows it in Home's \"My Websites\" row"});
        return out;
    };
    model.selected = [] { return -1; };
    model.choose = [&app, id, site, change](int i)
    {
        const web::Website *w = site();
        if (!w)
            return;
        switch (i)
        {
        case 0:
        {
            std::string why;
            if (!app.websites().set_mode(id, !w->mode, &why))
                app.toast(why, th::kWarning);
            app.site_modes_changed();
            return;
        }
        case 1:
            app.open_keyboard("Tab name", web::mode_title(*w), web::kMaxModeLabel, false,
                              [change](bool ok, const std::string &text)
                              {
                                  if (ok)
                                      change([&](web::Website &x) { x.mode_label = text; });
                              });
            return;
        case 2:
            change([](web::Website &x) { x.letter_icon = !x.letter_icon; });
            return;
        case 3:
            app.open_keyboard(
                "Icon image address (empty: the site's own)",
                w->custom_icon ? w->icon_url : std::string{"https://"}, 2048, false,
                [&app, id](bool ok, const std::string &text)
                {
                    if (!ok)
                        return;
                    const std::string address =
                        text == "https://" || text == "http://" ? std::string{} : text;
                    if (!address.empty() && !web::check_address(address).ok)
                    {
                        app.toast("Enter an image address that starts with https://", th::kWarning);
                        return;
                    }
                    app.websites().set_icon(id, address, true);
                    if (address.empty())
                        app.toast("The site's own icon returns the next time it is looked up "
                                  "(Refresh icon)",
                                  th::kInfo);
                    app.site_modes_changed();
                });
            return;
        case 4:
            change(
                [](web::Website &x)
                {
                    std::size_t next = 0;
                    for (std::size_t k = 0; k < std::size(kTileColors); ++k)
                        if (kTileColors[k].rgb == x.tile_color)
                            next = (k + 1) % std::size(kTileColors);
                    x.tile_color = kTileColors[next].rgb;
                });
            return;
        case 5:
            change([](web::Website &x) { x.open_home = !x.open_home; });
            return;
        case 6:
            change([](web::Website &x) { x.pinned = !x.pinned; });
            return;
        default:
            return;
        }
    };
    model.footer = []
    {
        return std::string{
            "A mode opens the website in the PS5 browser inside AKENO STREAM, like any saved "
            "site - it is a shortcut with its own tab, not a native app for that service."};
    };
    const web::Website *w = site();
    app.push(make_choice_screen(app, w ? w->name : std::string{"Website"},
                                "Its tab, name, icon and where it starts", std::move(model)));
}
} // namespace akeno
