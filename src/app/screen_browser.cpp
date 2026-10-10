// AKENO STREAM PS5 - The embedded browser session, its test results and small dialogs.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BrowserScreen runs one visit to the embedded browser: an optional look at
// the site first (network problems in plain words), then the system browser
// over AKENO STREAM, pumped every frame, and on return a record of what
// happened. While the browser is open the system owns the screen and the
// controller; this screen is what is underneath and what shows before and
// after. For AKENO STREAM's own pages (official YouTube player, capability
// test) it also runs the loopback page server and turns the pages' reports
// into per-capability results.
#include "app/screens.hpp"
#include "core/fs.hpp"
#include "core/json.hpp"
#include "core/url.hpp"
#include "platform/platform.hpp"
#include "web/site_probe.hpp"

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
constexpr std::uint64_t kPageLoadMs = 30000;   // AKENO page that never asked for its script
constexpr std::uint64_t kPageSilentMs = 20000; // full-screen page without heartbeats
constexpr std::uint64_t kChordMs = 700;        // L3 + R3 together

std::uint64_t mono_ms()
{
    return platform::monotonic_us() / 1000;
}

std::string minutes(std::uint64_t ms)
{
    const std::uint64_t s = ms / 1000;
    char text[32];
    if (s < 60)
        std::snprintf(text, sizeof(text), "%llu s", static_cast<unsigned long long>(s));
    else
        std::snprintf(text, sizeof(text), "%llu min", static_cast<unsigned long long>(s / 60));
    return text;
}

// ---------------------------------------------------------------------------
class BrowserScreen final : public Screen
{
  public:
    BrowserScreen(App &app, WebSession session) : Screen{app}, s_{std::move(session)}
    {
        app_.set_browser_active(true);
        host_ = s_.kind == WebSession::Kind::website ? web::display_host(s_.url) : "AKENO STREAM";
        if (s_.check_first && s_.kind == WebSession::Kind::website)
            probe();
        else
            phase_ = Phase::opening;
    }

    ~BrowserScreen() override
    {
        *alive_ = false;
        if (cancel_)
            cancel_->store(true);
        if (app_.web_view().is_open())
            app_.web_view().close();
        if (owns_pages_)
            app_.local_pages().stop();
        app_.set_browser_active(false);
    }

    [[nodiscard]] bool full_screen() const override
    {
        return true;
    }
    // The system browser needs the app to keep presenting under it.
    [[nodiscard]] bool animating() const override
    {
        return true;
    }

    void handle(input::Button b) override
    {
        switch (phase_)
        {
        case Phase::ask:
            if (b == input::Button::cross)
                phase_ = Phase::opening;
            else if (b == input::Button::circle)
                app_.pop();
            return;
        case Phase::failed:
            if (b == input::Button::cross)
            {
                error_.clear();
                phase_ = Phase::opening;
            }
            else if (b == input::Button::circle)
                app_.pop();
            return;
        case Phase::checking:
            if (b == input::Button::circle)
                app_.pop();
            else if (b == input::Button::cross)
            {
                if (cancel_)
                    cancel_->store(true);
                phase_ = Phase::opening; // skip the check
            }
            return;
        case Phase::running:
        {
            // While the browser is up the system normally keeps the buttons.
            // If they do reach AKENO STREAM, L3 + R3 together close it.
            const std::uint64_t now = mono_ms();
            if (b == input::Button::l3)
                l3_ms_ = now;
            else if (b == input::Button::r3)
                r3_ms_ = now;
            if (l3_ms_ && r3_ms_ &&
                (l3_ms_ > r3_ms_ ? l3_ms_ - r3_ms_ : r3_ms_ - l3_ms_) < kChordMs)
            {
                note_ = "closed with L3 + R3";
                app_.web_view().close();
                phase_ = Phase::closing;
            }
            return;
        }
        default:
            return;
        }
    }

    void update(std::uint64_t) override
    {
        switch (phase_)
        {
        case Phase::opening:
            // Draw "Opening..." once before the system browser covers the screen.
            if (drawn_)
                start();
            return;
        case Phase::running:
        case Phase::closing:
            if (!closing())
                pump();
            return;
        default:
            return;
        }
    }

    void render(ui::Painter &p, std::uint64_t now_ms) override
    {
        drawn_ = true;
        const Pixel accent =
            s_.kind == WebSession::Kind::youtube ? th::kAccentYouTube : th::kAccentWebsites;
        p.background(accent);
        p.text(th::kMarginX, 80, s_.kind == WebSession::Kind::website ? "WEBSITE" : "AKENO STREAM",
               th::kCaptionStrong, accent);
        p.text(th::kMarginX, 116, s_.title.empty() ? host_ : s_.title, th::kTitle, th::kText,
               th::kWidth - 2 * th::kMarginX);
        const char *subtitle = s_.kind == WebSession::Kind::youtube
                                   ? "YouTube's official embedded player"
                                   : "Browser capability test page";
        p.text(th::kMarginX, 186, s_.kind == WebSession::Kind::website ? host_ : subtitle,
               th::kBody, th::kTextSecondary, 1400);

        const Rect panel{th::kMarginX, 300, th::kWidth - 2 * th::kMarginX, 560};
        p.panel(panel, th::kPanelRadius, gfx::with_alpha(th::kSurface, 235));
        const int tx = panel.x + 60, tw = panel.w - 120;
        int y = panel.y + 54;
        switch (phase_)
        {
        case Phase::checking:
            p.spinner(tx + 30, y + 26, 22, now_ms, accent);
            p.text(tx + 80, y, "Checking " + host_ + "...", th::kHeading, th::kText, tw - 80);
            p.wrapped(tx, y + 90,
                      "AKENO STREAM checks that the site answers before the browser opens. "
                      "Press X to skip the check.",
                      th::kBody, th::kTextSecondary, tw, 3);
            break;
        case Phase::ask:
            p.icon(Icon::alert, tx + 26, y + 24, 52, th::kWarning);
            p.text(tx + 80, y, "The site did not answer", th::kHeading, th::kText, tw - 80);
            y += 80;
            y += p.wrapped(tx, y, probe_.problem, th::kBody, th::kTextSecondary, tw, 4) + 20;
            p.text(tx, y, probe_.detail, th::kCaption, th::kTextMuted, tw);
            p.wrapped(tx, panel.bottom() - 110,
                      "The system browser checks the site itself and may still reach it. Open "
                      "it anyway, or go back.",
                      th::kBody, th::kTextSecondary, tw, 2);
            break;
        case Phase::opening:
        case Phase::running:
            p.spinner(tx + 30, y + 26, 22, now_ms, accent);
            p.text(tx + 80, y,
                   phase_ == Phase::opening ? "Opening the browser..." : "The browser is open",
                   th::kHeading, th::kText, tw - 80);
            y += 90;
            y += p.wrapped(tx, y,
                           "The PS5's own browser shows the page over AKENO STREAM. Use the "
                           "browser's controls to move, scroll, type and play. Close the "
                           "browser to come back here.",
                           th::kBody, th::kTextSecondary, tw, 4) +
                 24;
            p.wrapped(tx, y,
                      "Your sign-ins stay inside the browser: AKENO STREAM never sees passwords, "
                      "cookies or what you type on websites.",
                      th::kCaption, th::kTextMuted, tw, 2);
            p.wrapped(tx, panel.bottom() - 70,
                      "If the browser offers no way out: press L3 and R3 together. Avoid the "
                      "PS button while the browser is open.",
                      th::kCaption, th::kWarning, tw, 2);
            break;
        case Phase::closing:
            p.spinner(tx + 30, y + 26, 22, now_ms, accent);
            p.text(tx + 80, y, "Returning to AKENO STREAM...", th::kHeading, th::kText, tw - 80);
            break;
        case Phase::failed:
            p.icon(Icon::alert, tx + 26, y + 24, 52, th::kError);
            p.text(tx + 80, y, "The browser could not open", th::kHeading, th::kText, tw - 80);
            p.wrapped(tx, y + 90, error_, th::kBody, th::kTextSecondary, tw, 6);
            p.wrapped(tx, panel.bottom() - 110,
                      "Settings > Diagnostics > Browser shows the details for a bug report.",
                      th::kCaption, th::kTextMuted, tw, 2);
            break;
        }
        int hx = th::kMarginX;
        for (const auto &h : hints())
            hx += p.hint(hx, th::kHintBarY, h.glyph, h.label) + 36;
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        switch (phase_)
        {
        case Phase::checking:
            return {{Glyph::cross, "Skip the check"}, {Glyph::circle, "Back"}};
        case Phase::ask:
            return {{Glyph::cross, "Open anyway"}, {Glyph::circle, "Back"}};
        case Phase::failed:
            return {{Glyph::cross, "Try again"}, {Glyph::circle, "Back"}};
        default:
            return {};
        }
    }

  private:
    enum class Phase : std::uint8_t
    {
        checking,
        ask,
        opening,
        running,
        closing,
        failed,
    };

    void record(const char *id, const char *group, const char *name, web::Outcome outcome,
                std::string detail, const char *source)
    {
        web::TestRecord r;
        r.id = id;
        r.group = group;
        r.name = name;
        r.outcome = outcome;
        r.detail = std::move(detail);
        r.at = platform::wall_clock_seconds();
        r.source = source;
        app_.web_tests().set(std::move(r));
    }

    void youtube(const char *id, const char *name, web::Outcome outcome, std::string detail)
    {
        record(id, "YouTube embedded player", name, outcome, std::move(detail), "YouTube player");
    }

    void probe()
    {
        phase_ = Phase::checking;
        auto cancel = net::make_cancel_flag();
        cancel_ = cancel;
        auto alive = alive_;
        App &app = app_;
        const std::string url = s_.url;
        app_.jobs().run(
            [url, cancel, alive, &app, this]
            {
                web::SiteProbe result = web::probe_site(url, cancel);
                app.jobs().post(
                    [result = std::move(result), alive, this]() mutable
                    {
                        if (!*alive || phase_ != Phase::checking)
                            return;
                        probe_ = std::move(result);
                        // A saved site without an icon gets the one found now.
                        if (!s_.site_id.empty() && !probe_.icon_url.empty())
                            if (const web::Website *site = app_.websites().find(s_.site_id))
                                if (site->icon_url.empty())
                                    app_.websites().set_icon(s_.site_id, probe_.icon_url);
                        if (probe_.blocking)
                        {
                            app_.report_error("website check", host_ + ": " + probe_.problem);
                            phase_ = Phase::ask;
                        }
                        else
                        {
                            if (!probe_.problem.empty())
                                app_.toast(probe_.problem, th::kWarning);
                            phase_ = Phase::opening;
                        }
                        app_.mark_dirty();
                    });
            });
    }

    void fail(std::string why)
    {
        error_ = std::move(why);
        phase_ = Phase::failed;
        app_.report_error("browser", error_);
        record("browser.open", "Browser", "The browser opens inside AKENO STREAM", web::Outcome::no,
               error_, "AKENO STREAM");
        if (owns_pages_)
        {
            app_.local_pages().stop();
            owns_pages_ = false;
        }
    }

    void start()
    {
        platform::WebOpenRequest request;
        request.url = s_.url;
        std::string error;
        if (s_.kind != WebSession::Kind::website)
        {
            // The lab plays AKENO STREAM's own clips; the YouTube page needs none.
            const std::string media = s_.kind == WebSession::Kind::capability_test
                                          ? fs::join(platform::app_dir(), "assets/selftest")
                                          : std::string{};
            if (!app_.local_pages().start(media, &error))
            {
                fail("AKENO STREAM's page could not be served: " + error);
                return;
            }
            owns_pages_ = true;
            if (s_.kind == WebSession::Kind::youtube)
            {
                std::vector<std::pair<std::string, std::string>> query;
                if (!s_.youtube.video_id.empty())
                    query.emplace_back("v", s_.youtube.video_id);
                if (!s_.youtube.list_id.empty())
                    query.emplace_back("list", s_.youtube.list_id);
                if (s_.youtube.start_seconds > 0)
                    query.emplace_back("t", std::to_string(s_.youtube.start_seconds));
                request.url = app_.local_pages().page_url("youtube", url::build_query(query));
            }
            else
            {
                request.url = app_.local_pages().page_url("captest");
            }
            if (app_.store().settings().web_full_screen_pages)
            {
                request.layout = platform::WebLayout::custom;
                request.rect = {0, 0, th::kWidth, th::kHeight};
                custom_ = true;
            }
        }
        if (!app_.web_view().open(request, &error))
        {
            fail(error.empty() ? std::string{"the system browser is not available"} : error);
            return;
        }
        phase_ = Phase::running;
        opened_ms_ = mono_ms();
        record("browser.open", "Browser", "The browser opens inside AKENO STREAM",
               web::Outcome::yes, app_.web_view().info().engine, "AKENO STREAM");
        if (s_.kind == WebSession::Kind::website)
        {
            app_.websites().record_visit(s_.url, s_.title, platform::wall_clock_seconds());
            if (!s_.site_id.empty())
                app_.websites().set_result(s_.site_id, "opened");
        }
    }

    void pump()
    {
        platform::WebView &web = app_.web_view();
        const platform::WebStatus status = web.update();
        if (owns_pages_)
        {
            web::LocalPages &pages = app_.local_pages();
            for (const web::PageEvent &e : pages.take_events())
                on_event(e);
            take_lab_reports();
            const std::uint64_t now = mono_ms();
            if (phase_ == Phase::running)
            {
                if (pages.take_close_request())
                {
                    web.close();
                    phase_ = Phase::closing;
                }
                else if (!pages.page_loaded() && now - opened_ms_ > kPageLoadMs)
                {
                    note_ = "AKENO STREAM's page did not load within 30 seconds";
                    web.close();
                    phase_ = Phase::closing;
                }
                else if (custom_ && pages.page_loaded() && pages.last_contact_ms() &&
                         now - pages.last_contact_ms() > kPageSilentMs)
                {
                    // Without browser controls, a page that went away (or a
                    // site the user navigated to) would leave no way back.
                    note_ = "the page stopped answering";
                    web.close();
                    phase_ = Phase::closing;
                }
            }
        }
        if (status == platform::WebStatus::running)
            return;
        finish();
    }

    void on_event(const web::PageEvent &e)
    {
        using web::Outcome;
        if (e.type == "input")
        {
            const std::string key = e.value.empty() ? e.detail : e.value + " (" + e.detail + ")";
            if (std::find(keys_.begin(), keys_.end(), key) == keys_.end() && keys_.size() < 12)
                keys_.push_back(key);
            return;
        }
        if (s_.kind != WebSession::Kind::youtube)
            return;
        if (e.type == "loaded")
        {
            youtube("youtube.page", "AKENO's player page loads", Outcome::yes, "");
            record("browser.user_agent", "Browser", "User agent", Outcome::info, e.detail,
                   "YouTube player");
        }
        else if (e.type == "api")
            youtube("youtube.api", "YouTube IFrame API loads",
                    e.value == "ok" ? Outcome::yes : Outcome::no, e.detail);
        else if (e.type == "ready")
            youtube("youtube.ready", "Player initializes", Outcome::yes,
                    "ready " + e.value + " s after the page opened");
        else if (e.type == "playing")
        {
            played_ = true;
            youtube("youtube.playing", "Video plays", Outcome::yes,
                    "started " + e.value + " s after the page opened");
            if (!s_.youtube.list_id.empty())
                youtube("youtube.playlist_start", "Playlist starts", Outcome::yes, "");
        }
        else if (e.type == "state")
        {
            if (e.value == "2")
            {
                paused_ = true;
                youtube("youtube.pause", "Pause", Outcome::yes, "");
            }
            else if (e.value == "1" && paused_)
                youtube("youtube.resume", "Play after pause", Outcome::yes, "");
            else if (e.value == "1" && playlist_pending_)
            {
                playlist_pending_ = false;
                youtube("youtube.playlist", "Playlist next / previous", Outcome::yes,
                        "the next video started");
            }
        }
        else if (e.type == "playlist")
            playlist_pending_ = true;
        else if (e.type == "error")
        {
            error_ = "YouTube player error " + e.value + ": " + e.detail;
            youtube("youtube.error", "Player error", Outcome::no,
                    "error " + e.value + ": " + e.detail);
            app_.report_error("YouTube player", error_);
        }
        else if (e.type == "autoplay")
            youtube("youtube.autoplay", "Starts by itself", Outcome::partial,
                    "autoplay was blocked: the Play button starts it (" + e.detail + ")");
        else if (e.type == "fullscreen")
            youtube("youtube.fullscreen", "Full screen",
                    e.value == "ok" || e.value == "requested" ? Outcome::yes : Outcome::no,
                    e.value + (e.detail.empty() ? "" : ": " + e.detail));
        else if (e.type == "volume")
            youtube("youtube.mute", "Mute and sound on", Outcome::yes, e.value);
        else if (e.type == "time")
            youtube("youtube.continuous", "Keeps playing", Outcome::yes, e.value + " s reached");
        else if (e.type == "ended")
            youtube("youtube.ended", "Plays to the end", Outcome::yes, "");
        else if (e.type == "log")
            app_.report_error("YouTube page", e.value + ": " + e.detail);
    }

    // The lab sends its results after every step: each report is saved at
    // once, so leaving early (or a crash) keeps what was measured.
    void take_lab_reports()
    {
        if (s_.kind != WebSession::Kind::capability_test || !owns_pages_)
            return;
        for (const std::string &report : app_.local_pages().take_reports())
        {
            std::string error;
            const int taken =
                app_.web_tests().accept_report(report, platform::wall_clock_seconds(), &error);
            if (taken > 0)
                lab_taken_ = std::max(lab_taken_, taken);
            else
                lab_error_ = error;
            json::ParseLimits limits;
            limits.max_bytes = 64 * 1024;
            limits.max_depth = 8;
            const auto parsed = json::parse(report, limits);
            if (parsed.ok && parsed.value["done"].boolean() && taken > 0)
                lab_done_ = true;
        }
    }

    void finish()
    {
        using web::Outcome;
        const std::uint64_t spent = opened_ms_ ? mono_ms() - opened_ms_ : 0;
        std::string summary;
        Pixel color = th::kInfo;
        if (owns_pages_)
        {
            web::LocalPages &pages = app_.local_pages();
            for (const web::PageEvent &e : pages.take_events())
                on_event(e);
            take_lab_reports();
            if (s_.kind == WebSession::Kind::capability_test)
            {
                if (lab_done_)
                {
                    summary = "Playback lab: " + std::to_string(lab_taken_) +
                              " results saved (Websites > Playback Lab)";
                    color = th::kSuccess;
                    record("captest.finished", "Browser", "Playback lab completes", Outcome::yes,
                           std::to_string(lab_taken_) + " results", "capability test");
                }
                else
                {
                    summary = lab_taken_ > 0
                                  ? "Playback lab: closed early - " + std::to_string(lab_taken_) +
                                        " results kept; run it again and wait for \"Done\""
                                  : "The playback lab did not finish - run it again and wait "
                                    "for \"Done\"";
                    color = th::kWarning;
                    record("captest.finished", "Browser", "Playback lab completes", Outcome::no,
                           !lab_error_.empty()   ? lab_error_
                           : lab_taken_ > 0      ? "closed before the last test finished"
                           : pages.page_loaded() ? "closed before the results were sent"
                                                 : "the test page did not load",
                           "capability test");
                }
            }
            pages.stop();
            owns_pages_ = false;
        }
        if (!keys_.empty())
        {
            std::string list;
            for (const std::string &k : keys_)
                list += (list.empty() ? "" : ", ") + k;
            record("browser.keys", "Browser", "Controller keys pages receive", Outcome::info, list,
                   "AKENO STREAM");
        }
        if (s_.kind == WebSession::Kind::youtube)
        {
            if (!played_)
                youtube("youtube.playing", "Video plays", Outcome::no,
                        !error_.empty()  ? error_
                        : !note_.empty() ? note_
                                         : "no playing state reported before the browser closed");
            if (!error_.empty())
            {
                summary = error_;
                color = th::kError;
            }
            else if (played_)
            {
                summary = "YouTube: played for " + minutes(spent) + ", no player errors";
                color = th::kSuccess;
            }
            else
            {
                summary = "YouTube: the video did not start" +
                          (note_.empty() ? std::string{} : " (" + note_ + ")");
                color = th::kWarning;
            }
            youtube("youtube.return", "Back to AKENO STREAM afterwards", Outcome::yes,
                    "after " + minutes(spent));
        }
        else if (s_.kind == WebSession::Kind::website)
        {
            summary = "Back from " + host_ + " (" + minutes(spent) + ")";
            if (!s_.site_id.empty())
                app_.websites().set_result(s_.site_id,
                                           "opened, " + minutes(spent) + " in the browser");
        }
        record("browser.return", "Browser", "Back to AKENO STREAM after closing the browser",
               Outcome::yes,
               "result code " + std::to_string(app_.web_view().result()) +
                   (note_.empty() ? std::string{} : ", " + note_),
               "AKENO STREAM");
        if (!note_.empty())
            app_.report_error("browser", note_);
        if (!summary.empty())
            app_.toast(summary, color);
        phase_ = Phase::closing;
        close_later(); // the app removes this screen after the update
        // Screens cannot be pushed while the app updates its overlays.
        if (s_.after)
            app_.jobs().post(std::move(s_.after));
    }

    WebSession s_;
    std::string host_;
    Phase phase_ = Phase::opening;
    bool drawn_ = false;
    bool owns_pages_ = false;
    bool custom_ = false;
    bool played_ = false;
    bool paused_ = false;
    bool playlist_pending_ = false;
    bool lab_done_ = false;
    int lab_taken_ = 0;
    std::string lab_error_;
    std::uint64_t opened_ms_ = 0;
    std::uint64_t l3_ms_ = 0, r3_ms_ = 0;
    std::string error_;
    std::string note_;
    std::vector<std::string> keys_;
    web::SiteProbe probe_;
    net::CancelFlag cancel_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

// ---------------------------------------------------------------------------
class MenuScreen final : public Screen
{
  public:
    MenuScreen(App &app, std::string title, std::string subtitle, std::vector<MenuOption> options)
        : Screen{app}, title_{std::move(title)}, subtitle_{std::move(subtitle)},
          options_{std::move(options)}
    {
    }
    [[nodiscard]] bool modal() const override
    {
        return true;
    }
    void handle(input::Button b) override
    {
        const int count = static_cast<int>(options_.size());
        if (b == input::Button::up)
            focus_ = std::max(0, focus_ - 1);
        else if (b == input::Button::down)
            focus_ = std::min(count - 1, focus_ + 1);
        else if (b == input::Button::cross && focus_ < count)
        {
            auto run = options_[static_cast<std::size_t>(focus_)].run; // pop() destroys this
            app_.pop();
            if (run)
                run();
        }
        else if (b == input::Button::circle)
            app_.pop();
    }
    void render(ui::Painter &p, std::uint64_t) override
    {
        p.s.dim(p.s.bounds(), 175);
        const int row_h = 76;
        const int h = std::min(860, 190 + static_cast<int>(options_.size()) * row_h);
        const Rect panel{460, (th::kHeight - h) / 2, 1000, h};
        p.panel(panel, 32, th::kSurface);
        p.text(panel.x + 50, panel.y + 40, title_, th::kTitle, th::kText, panel.w - 100);
        p.text(panel.x + 50, panel.y + 100, subtitle_, th::kCaption, th::kTextMuted, panel.w - 100);
        const int visible = (h - 170) / row_h;
        const int first = std::clamp(focus_ - visible / 2, 0,
                                     std::max(0, static_cast<int>(options_.size()) - visible));
        int y = panel.y + 150;
        for (int i = first; i < static_cast<int>(options_.size()) && i < first + visible; ++i)
        {
            const Rect r{panel.x + 36, y, panel.w - 72, row_h - 10};
            const bool focused = i == focus_;
            if (focused)
                p.s.fill_rounded(r, 16, th::kText);
            const auto &o = options_[static_cast<std::size_t>(i)];
            p.text(r.x + 26, r.y + 8, o.label, th::kBodyStrong,
                   focused ? th::kTextOnAccent : th::kText, r.w - 52);
            p.text(r.x + 26, r.y + 38, o.detail, th::kSmall,
                   focused ? th::kTextOnAccent : th::kTextMuted, r.w - 52);
            y += row_h;
        }
    }
    [[nodiscard]] std::vector<Hint> hints() const override
    {
        return {{Glyph::cross, "Select"}, {Glyph::circle, "Close"}};
    }

  private:
    std::string title_, subtitle_;
    std::vector<MenuOption> options_;
    int focus_ = 0;
};

// ---------------------------------------------------------------------------
class RecordScreen final : public Screen
{
  public:
    RecordScreen(App &app, std::string title, std::string note, std::vector<RecordRow> rows,
                 std::function<void(const std::vector<RecordRow> &)> save)
        : Screen{app}, title_{std::move(title)}, note_{std::move(note)}, rows_{std::move(rows)},
          save_{std::move(save)}
    {
    }
    [[nodiscard]] bool modal() const override
    {
        return true;
    }
    void handle(input::Button b) override
    {
        const int count = static_cast<int>(rows_.size());
        if (b == input::Button::up)
            focus_ = std::max(0, focus_ - 1);
        else if (b == input::Button::down)
            focus_ = std::min(count - 1, focus_ + 1);
        else if ((b == input::Button::cross || b == input::Button::right ||
                  b == input::Button::left) &&
                 focus_ < count)
        {
            web::Mark &m = rows_[static_cast<std::size_t>(focus_)].mark;
            const int step = b == input::Button::left ? 2 : 1;
            m = static_cast<web::Mark>((static_cast<int>(m) + step) % 3);
            if (save_)
                save_(rows_); // saved at once: nothing is lost by leaving
        }
        else if (b == input::Button::circle)
            app_.pop();
    }
    void render(ui::Painter &p, std::uint64_t) override
    {
        p.s.dim(p.s.bounds(), 175);
        const Rect panel{300, 140, 1320, 800};
        p.panel(panel, 32, th::kSurface);
        p.text(panel.x + 56, panel.y + 40, title_, th::kTitle, th::kText, panel.w - 112);
        const int note_h = p.wrapped(panel.x + 56, panel.y + 104, note_, th::kCaption,
                                     th::kTextSecondary, panel.w - 112, 3);
        int y = panel.y + 124 + note_h;
        for (std::size_t i = 0; i < rows_.size(); ++i)
        {
            const Rect r{panel.x + 40, y, panel.w - 80, 96};
            const bool focused = static_cast<int>(i) == focus_;
            p.s.fill_rounded(r, 18, focused ? th::kSurfaceHighlight : th::kSurfaceRaised);
            if (focused)
                p.focus_ring(r, 18);
            p.text(r.x + 28, r.y + 14, rows_[i].label, th::kBodyStrong, th::kText, r.w - 380);
            p.text(r.x + 28, r.y + 54, rows_[i].hint, th::kSmall, th::kTextMuted, r.w - 380);
            const web::Mark m = rows_[i].mark;
            const Pixel color = m == web::Mark::works   ? th::kSuccess
                                : m == web::Mark::fails ? th::kError
                                                        : th::kTextMuted;
            const std::string label = web::mark_name(m);
            p.chip(r.right() - 40 - p.measure(label, th::kSmall) - 24, r.y + 32, label,
                   gfx::with_alpha(color, 60), color);
            y += r.h + 12;
        }
    }
    [[nodiscard]] std::vector<Hint> hints() const override
    {
        return {{Glyph::cross, "Change"}, {Glyph::dpad, "Choose"}, {Glyph::circle, "Done"}};
    }

  private:
    std::string title_, note_;
    std::vector<RecordRow> rows_;
    std::function<void(const std::vector<RecordRow> &)> save_;
    int focus_ = 0;
};

// ---------------------------------------------------------------------------
class ChoiceScreen final : public Screen
{
  public:
    ChoiceScreen(App &app, std::string title, std::string note, ChoiceModel model)
        : Screen{app}, title_{std::move(title)}, note_{std::move(note)}, m_{std::move(model)}
    {
        focus_ = std::max(0, m_.selected ? m_.selected() : 0);
    }
    [[nodiscard]] bool modal() const override
    {
        return true;
    }
    void handle(input::Button b) override
    {
        const int count = static_cast<int>(m_.options().size());
        if (b == input::Button::up)
            focus_ = std::max(0, focus_ - 1);
        else if (b == input::Button::down)
            focus_ = std::min(count - 1, focus_ + 1);
        else if (b == input::Button::cross && focus_ < count && m_.choose)
            m_.choose(focus_);
        else if (b == input::Button::circle)
            app_.pop();
    }
    void render(ui::Painter &p, std::uint64_t) override
    {
        p.s.dim(p.s.bounds(), 175);
        const Rect panel{240, 70, 1440, 940};
        p.panel(panel, 32, th::kSurface);
        p.text(panel.x + 56, panel.y + 36, title_, th::kTitle, th::kText, panel.w - 112);
        const int note_h = p.wrapped(panel.x + 56, panel.y + 96, note_, th::kCaption,
                                     th::kTextSecondary, panel.w - 112, 2);
        const std::vector<ChoiceOption> options = m_.options();
        const int selected = m_.selected ? m_.selected() : -1;
        const int row_h = 74;
        const int top = panel.y + 116 + note_h;
        const int footer_h = 150;
        const int visible = std::max(1, (panel.bottom() - footer_h - top) / row_h);
        const int first = std::clamp(focus_ - visible / 2, 0,
                                     std::max(0, static_cast<int>(options.size()) - visible));
        int y = top;
        for (int i = first; i < static_cast<int>(options.size()) && i < first + visible; ++i)
        {
            const Rect r{panel.x + 40, y, panel.w - 80, row_h - 8};
            const bool focused = i == focus_;
            p.s.fill_rounded(r, 16, focused ? th::kSurfaceHighlight : th::kSurfaceRaised);
            if (focused)
                p.focus_ring(r, 16);
            const auto &o = options[static_cast<std::size_t>(i)];
            if (i == selected)
                p.icon(Icon::check, r.x + 30, r.y + r.h / 2, 30, th::kSuccess);
            p.text(r.x + 64, r.y + 6, o.label, th::kBodyStrong, th::kText, r.w - 100);
            p.text(r.x + 64, r.y + 38, o.hint, th::kSmall, th::kTextMuted, r.w - 100);
            y += row_h;
        }
        if (m_.footer)
            p.wrapped(panel.x + 56, panel.bottom() - footer_h + 16, m_.footer(), th::kCaption,
                      th::kTextSecondary, panel.w - 112, 4);
    }
    [[nodiscard]] std::vector<Hint> hints() const override
    {
        return {{Glyph::cross, "Choose"}, {Glyph::dpad, "Move"}, {Glyph::circle, "Done"}};
    }

  private:
    std::string title_, note_;
    ChoiceModel m_;
    int focus_ = 0;
};

// ---------------------------------------------------------------------------
// Settings > Diagnostics > Browser: the engine's start-up and every result.
class BrowserTestsScreen final : public Screen
{
  public:
    explicit BrowserTestsScreen(App &app) : Screen{app}
    {
    }
    void handle(input::Button b) override
    {
        const int count = static_cast<int>(lines().size());
        if (b == input::Button::down)
            scroll_ = std::clamp(scroll_ + 1, 0, std::max(0, count - 1));
        else if (b == input::Button::up)
            scroll_ = std::max(0, scroll_ - 1);
        else if (b == input::Button::right)
            scroll_ = std::clamp(scroll_ + 10, 0, std::max(0, count - 1));
        else if (b == input::Button::left)
            scroll_ = std::max(0, scroll_ - 10);
        else if (b == input::Button::cross)
            app_.run_browser_test();
        else if (b == input::Button::square && !app_.web_tests().records().empty())
            app_.push(make_confirm_screen(app_, "Clear the browser results?",
                                          "All saved capability, YouTube and Crunchyroll test "
                                          "results are removed. Your websites stay.",
                                          "Clear", [this] { app_.web_tests().clear(); }));
        else if (b == input::Button::circle)
            app_.pop();
    }
    void render(ui::Painter &p, std::uint64_t) override
    {
        p.text(th::kMarginX, 150, "Browser", th::kTitle, th::kText);
        p.text(th::kMarginX, 210,
               "What the console's browser can do inside AKENO STREAM, item by item.", th::kBody,
               th::kTextSecondary, 1500);
        const Rect box{th::kMarginX, 270, th::kWidth - 2 * th::kMarginX, 720};
        p.panel(box, th::kPanelRadius, gfx::with_alpha(th::kSurface, 235));
        const auto all = lines();
        int y = box.y + 26;
        for (std::size_t i = static_cast<std::size_t>(scroll_);
             i < all.size() && y < box.bottom() - 40; ++i)
        {
            const Line &l = all[i];
            if (l.heading)
            {
                y += 8;
                p.text(box.x + 36, y, l.text, th::kBodyStrong, th::kText, box.w - 72);
                y += 42;
                continue;
            }
            if (l.has_status)
            {
                const Pixel color = l.outcome == web::Outcome::yes       ? th::kSuccess
                                    : l.outcome == web::Outcome::no      ? th::kError
                                    : l.outcome == web::Outcome::partial ? th::kWarning
                                    : l.outcome == web::Outcome::info    ? th::kInfo
                                                                         : th::kTextMuted;
                p.text(box.x + 36, y, web::outcome_label(l.outcome), th::kCaptionStrong, color,
                       130);
            }
            p.text(box.x + 180, y, l.text, th::kCaption, th::kText, 560);
            p.text(box.x + 760, y, l.detail, th::kCaption, th::kTextSecondary, box.w - 800);
            y += 36;
        }
    }
    [[nodiscard]] std::vector<Hint> hints() const override
    {
        return {{Glyph::cross, "Run the browser test"},
                {Glyph::square, "Clear results"},
                {Glyph::dpad, "Scroll"},
                {Glyph::circle, "Back"}};
    }

  private:
    struct Line
    {
        bool heading = false;
        bool has_status = false;
        web::Outcome outcome = web::Outcome::unknown;
        std::string text, detail;
    };
    std::vector<Line> lines() const
    {
        std::vector<Line> out;
        const platform::WebEngineInfo engine = app_.web_view().info();
        out.push_back({true, false, {}, "Engine", {}});
        out.push_back(
            {false, true,
             !engine.attempted  ? web::Outcome::unknown
             : engine.available ? web::Outcome::yes
                                : web::Outcome::no,
             engine.engine.empty() ? "Not started yet - open a website first" : engine.engine,
             engine.error});
        for (const auto &step : engine.steps)
            out.push_back({false, false, {}, "", step});
        std::vector<web::TestRecord> records = app_.web_tests().records();
        std::stable_sort(records.begin(), records.end(),
                         [](const web::TestRecord &a, const web::TestRecord &b)
                         { return a.group < b.group; });
        std::string group;
        for (const auto &r : records)
        {
            if (r.group != group)
            {
                group = r.group;
                out.push_back({true, false, {}, group, {}});
            }
            out.push_back({false, true, r.outcome, r.name, r.detail});
        }
        if (records.empty())
            out.push_back({false,
                           false,
                           {},
                           "No results yet",
                           "Press X to run the browser capability test."});
        return out;
    }
    int scroll_ = 0;
};
} // namespace

std::unique_ptr<Screen> make_browser_screen(App &app, WebSession session)
{
    return std::make_unique<BrowserScreen>(app, std::move(session));
}

std::unique_ptr<Screen> make_browser_tests_screen(App &app)
{
    return std::make_unique<BrowserTestsScreen>(app);
}

std::unique_ptr<Screen> make_menu_screen(App &app, std::string title, std::string subtitle,
                                         std::vector<MenuOption> options)
{
    return std::make_unique<MenuScreen>(app, std::move(title), std::move(subtitle),
                                        std::move(options));
}

std::unique_ptr<Screen> make_choice_screen(App &app, std::string title, std::string note,
                                           ChoiceModel model)
{
    return std::make_unique<ChoiceScreen>(app, std::move(title), std::move(note), std::move(model));
}

void open_playback_record(App &app, const std::string &key, const std::string &name,
                          bool drm_expected)
{
    if (key.empty())
        return;
    const auto current = [&app, key]
    {
        const web::PlaybackCheck *c = app.playback_checks().get(key);
        web::PlaybackCheck out;
        if (c)
            out = *c;
        out.key = key;
        return out;
    };
    const auto save = [&app, name](web::PlaybackCheck c)
    {
        c.name = name;
        c.at = platform::wall_clock_seconds();
        app.playback_checks().set(std::move(c));
    };
    const auto ask_code = [&app, current, save]
    {
        app.open_keyboard("Error code or message the player showed", current().error_code, 60,
                          false,
                          [current, save](bool ok, const std::string &text)
                          {
                              if (!ok)
                                  return;
                              web::PlaybackCheck c = current();
                              c.error_code = web::clean_error_code(text);
                              save(std::move(c));
                          });
    };
    ChoiceModel model;
    model.options = [current]
    {
        std::vector<ChoiceOption> out;
        for (int i = 1; i < web::kSeenCount; ++i)
        {
            const auto seen = static_cast<web::Seen>(i);
            out.push_back({web::seen_label(seen), web::seen_hint(seen)});
        }
        const std::string code = current().error_code;
        out.push_back({code.empty() ? "Error code: none entered" : "Error code: " + code,
                       "Enter exactly what the player shows (for example KAT-6005)"});
        out.push_back({"Clear this record", "Removes what you recorded for this site"});
        return out;
    };
    model.selected = [current] { return static_cast<int>(current().seen) - 1; };
    model.choose = [&app, key, current, save, ask_code](int i)
    {
        if (i < web::kSeenCount - 1)
        {
            web::PlaybackCheck c = current();
            c.seen = static_cast<web::Seen>(i + 1);
            if (c.seen == web::Seen::plays)
                c.error_code.clear();
            save(c);
            if (c.seen == web::Seen::error_message && c.error_code.empty())
                ask_code();
        }
        else if (i == web::kSeenCount - 1)
            ask_code();
        else
            app.playback_checks().remove(key);
    };
    model.footer = [&app, current, drm_expected]
    {
        const web::PlaybackCheck c = current();
        const web::Classification cls =
            web::classify(c, drm_expected, web::lab_facts(app.web_tests()));
        return std::string{"Result: "} + web::state_label(cls.state) +
               (cls.measured ? " (measured)" : "") + ". " + cls.reason;
    };
    app.push(make_choice_screen(
        app, "Video on " + name,
        "What happened when you played a video there? AKENO STREAM cannot see inside websites, "
        "so your answer and the playback lab's measurements decide the result together.",
        std::move(model)));
}

void open_secure_drm_record(App &app)
{
    struct Choice
    {
        const char *label;
        const char *hint;
        web::Outcome outcome;
        const char *detail;
    };
    static const Choice kChoices[] = {
        {"Widevine is listed as supported", "com.widevine.alpha shows a value, not null or false",
         web::Outcome::yes, "Widevine listed"},
        {"PlayReady is listed as supported", "com.microsoft.playready... shows a value",
         web::Outcome::yes, "PlayReady listed"},
        {"FairPlay is listed as supported", "com.apple.fps... shows a value", web::Outcome::yes,
         "FairPlay listed"},
        {"No DRM system is listed", "Every DRM entry shows null or false", web::Outcome::no,
         "no DRM system listed"},
        {"The page did not load or shows no list", "Blank page, script error, endless loading",
         web::Outcome::unknown, "the support page did not show a result"},
    };
    ChoiceModel model;
    model.options = []
    {
        std::vector<ChoiceOption> out;
        for (const Choice &c : kChoices)
            out.push_back({c.label, c.hint});
        return out;
    };
    model.selected = [&app]
    {
        const web::TestRecord *r = app.web_tests().get("drm.secure_check");
        if (!r)
            return -1;
        for (int i = 0; i < static_cast<int>(std::size(kChoices)); ++i)
            if (r->detail == kChoices[i].detail)
                return i;
        return -1;
    };
    model.choose = [&app](int i)
    {
        const Choice &c = kChoices[static_cast<std::size_t>(i)];
        web::TestRecord r;
        r.id = "drm.secure_check";
        r.group = "DRM (Encrypted Media Extensions)";
        r.name = "Secure DRM check (public HTTPS support page)";
        r.outcome = c.outcome;
        r.detail = c.detail;
        r.at = platform::wall_clock_seconds();
        r.source = "your reading";
        app.web_tests().set(std::move(r));
    };
    model.footer = [&app] { return web::drm_verdict(app.web_tests(), "").text; };
    app.push(make_choice_screen(
        app, "Secure DRM check",
        "What did the support page list under DRM? It runs over HTTPS, so the browser cannot "
        "hide DRM from it the way it may from AKENO STREAM's local page.",
        std::move(model)));
}

std::unique_ptr<Screen> make_record_screen(App &app, std::string title, std::string note,
                                           std::vector<RecordRow> rows,
                                           std::function<void(const std::vector<RecordRow> &)> save)
{
    return std::make_unique<RecordScreen>(app, std::move(title), std::move(note), std::move(rows),
                                          std::move(save));
}
} // namespace akeno
