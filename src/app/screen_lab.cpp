// AKENO STREAM PS5 - Websites > Playback Lab.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Finds out why a website's video does or does not play in the console's
// browser. AKENO STREAM's own lab page plays its bundled clip every way web
// players do (MP4 file, MediaSource, HLS, inside another site's frame) and
// asks for DRM; public test pages from the projects behind the common web
// players (HLS.js, dash.js, Shaka Player) and plain public test streams
// show the same with real network video. After each public test the user
// records what they saw (web/playback_check.hpp).
#include "app/screens.hpp"

#include <algorithm>

namespace akeno
{
namespace th = ui::theme;
using ui::Glyph;
using ui::Pixel;
using ui::Rect;

namespace
{
struct PublicTest
{
    const char *title;
    const char *hint;
    const char *url;
    const char *name; // what the record is filed as
};

// Public test pages and streams, chosen because they are published for
// exactly this purpose by the projects themselves.
const PublicTest kPublicTests[] = {
    {"Public test: MP4 file", "Big Buck Bunny (Blender Foundation, CC BY) from blender.org",
     "https://download.blender.org/peach/bigbuckbunny_movies/BigBuckBunny_320x180.mp4",
     "MP4 test file"},
    {"Public test: HLS played natively", "Apple's HLS example stream, opened directly",
     "https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_4x3/"
     "bipbop_4x3_variant.m3u8",
     "Apple HLS example"},
    {"Public test: HLS through HLS.js", "The HLS.js project's demo page (MediaSource)",
     "https://hlsjs.video-dev.org/demo/", "HLS.js demo"},
    {"Public test: DASH through dash.js", "DASH-IF reference player - press Load",
     "https://reference.dashif.org/dash.js/latest/samples/dash-if-reference-player/index.html",
     "dash.js reference player"},
};
constexpr char kSecureDrmCheck[] = "https://shaka-player-demo.appspot.com/support.html";
constexpr int kRows = 3 + static_cast<int>(std::size(kPublicTests));

class LabScreen final : public Screen
{
  public:
    explicit LabScreen(App &app) : Screen{app}
    {
    }

    void handle(input::Button b) override
    {
        switch (b)
        {
        case input::Button::up:
            focus_ = std::max(0, focus_ - 1);
            return;
        case input::Button::down:
            focus_ = std::min(kRows - 1, focus_ + 1);
            return;
        case input::Button::cross:
            run(focus_);
            return;
        case input::Button::square:
            record(focus_);
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
        const Pixel accent = th::kAccentWebsites;
        p.text(th::kMarginX, 150, "WEBSITES", th::kCaptionStrong, accent);
        p.text(th::kMarginX, 186, "Playback Lab", th::kTitle, th::kText);
        p.wrapped(th::kMarginX, 250,
                  "Why a website's video plays or not. The lab measures this console's browser; "
                  "the public tests try real streams. After a public test, record what you saw.",
                  th::kBody, th::kTextSecondary, 960, 2);

        const int lw = 980;
        int y = 350;
        for (int i = 0; i < kRows; ++i)
        {
            const Rect r{th::kMarginX, y, lw, 78};
            const bool focused = i == focus_;
            p.s.fill_rounded(r, 18, focused ? th::kSurfaceHighlight : th::kSurfaceRaised);
            if (focused)
                p.focus_ring(r, 18);
            const Row row = row_at(i);
            p.text(r.x + 26, r.y + 8, row.title, th::kBodyStrong, th::kText, r.w - 330);
            p.text(r.x + 26, r.y + 44, row.hint, th::kSmall, th::kTextMuted, r.w - 330);
            if (!row.status.empty())
                p.chip(r.right() - 26 - p.measure(row.status, th::kSmall) - 24, r.y + 24,
                       row.status, gfx::with_alpha(row.color, 60), row.color);
            y += r.h + 10;
        }

        const Rect panel{th::kMarginX + lw + 40, 150, th::kWidth - 2 * th::kMarginX - lw - 40, 800};
        p.panel(panel, th::kPanelRadius, gfx::with_alpha(th::kSurface, 235));
        p.text(panel.x + 30, panel.y + 24, "This browser", th::kHeading, th::kText);
        int ry = panel.y + 80;
        for (const Fact &f : facts())
        {
            const web::TestRecord *rec = app_.web_tests().get(f.id);
            const web::Outcome o = rec ? rec->outcome : web::Outcome::unknown;
            const Pixel color = outcome_color(o);
            p.s.fill_circle(panel.x + 40, ry + 14, 7, color);
            p.text(panel.x + 60, ry, f.label, th::kCaption, th::kText, panel.w - 230);
            p.text_right(panel.right() - 30, ry, rec ? web::outcome_label(o) : "Not tested",
                         th::kCaption, color);
            ry += 38;
        }
        p.wrapped(panel.x + 30, ry + 16, web::lab_meaning(web::lab_facts(app_.web_tests())),
                  th::kCaption, th::kTextSecondary, panel.w - 60, 7);
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        std::vector<Hint> out = {{Glyph::cross, "Open"}};
        if (kind(focus_) == Kind::drm || kind(focus_) == Kind::test)
            out.push_back({Glyph::square, "Record what you saw"});
        out.push_back({Glyph::circle, "Back"});
        return out;
    }

  private:
    enum class Kind : std::uint8_t
    {
        lab,
        drm,
        test,
        link,
    };
    static Kind kind(int i)
    {
        if (i == 0)
            return Kind::lab;
        if (i == 1)
            return Kind::drm;
        return i - 2 < static_cast<int>(std::size(kPublicTests)) ? Kind::test : Kind::link;
    }
    static const PublicTest &test_at(int i)
    {
        return kPublicTests[static_cast<std::size_t>(i - 2)];
    }

    struct Row
    {
        std::string title, hint, status;
        Pixel color = th::kTextMuted;
    };
    struct Fact
    {
        const char *id;
        const char *label;
    };

    static Pixel outcome_color(web::Outcome o)
    {
        return o == web::Outcome::yes       ? th::kSuccess
               : o == web::Outcome::no      ? th::kError
               : o == web::Outcome::partial ? th::kWarning
               : o == web::Outcome::info    ? th::kInfo
                                            : th::kTextMuted;
    }

    static const std::vector<Fact> &facts()
    {
        static const std::vector<Fact> list = {
            {"playback.video", "MP4 file (HTML5 video)"},
            {"mse.available", "MediaSource present"},
            {"playback.mse", "MediaSource plays (HLS.js, DASH)"},
            {"playback.hls_native", "HLS played natively"},
            {"playback.iframe", "Video in another site's frame"},
            {"browser.third_party_cookies", "Cookies in other sites' frames"},
            {"browser.secure", "Lab page is a secure context"},
            {"drm.eme", "DRM interface (EME)"},
            {"drm.widevine", "Widevine"},
            {"drm.playready", "PlayReady"},
            {"drm.fairplay", "FairPlay"},
            {"drm.secure_check", "Secure DRM check (HTTPS page)"},
        };
        return list;
    }

    Row row_at(int i) const
    {
        Row row;
        switch (kind(i))
        {
        case Kind::lab:
        {
            row.title = "Run the AKENO playback lab";
            row.hint = "MP4, MediaSource, HLS and frame playback of AKENO's clip; DRM; storage";
            const web::TestRecord *r = app_.web_tests().get("captest.finished");
            row.status = !r                                ? "Not run"
                         : r->outcome == web::Outcome::yes ? "Complete"
                                                           : "Incomplete";
            row.color = !r ? th::kTextMuted : outcome_color(r->outcome);
            break;
        }
        case Kind::drm:
        {
            row.title = "Secure DRM check";
            row.hint = "Shaka Player's support page (HTTPS) lists the DRM systems on offer";
            const web::TestRecord *r = app_.web_tests().get("drm.secure_check");
            row.status = r ? r->detail : "Not checked";
            row.color = r ? outcome_color(r->outcome) : th::kTextMuted;
            break;
        }
        case Kind::test:
        {
            const PublicTest &t = test_at(i);
            row.title = t.title;
            row.hint = t.hint;
            const web::PlaybackCheck *c = app_.playback_checks().get(web::check_key(t.url));
            if (c && c->seen != web::Seen::not_tested)
            {
                const web::Classification cls =
                    web::classify(*c, false, web::lab_facts(app_.web_tests()));
                row.status = web::state_label(cls.state);
                row.color = cls.state == web::PlaybackState::works          ? th::kSuccess
                            : cls.state == web::PlaybackState::undetermined ? th::kWarning
                                                                            : th::kError;
            }
            else
                row.status = "Not tested";
            break;
        }
        case Kind::link:
            row.title = "Play a video link in AKENO's player";
            row.hint =
                "A DRM-free HLS, MP4, MKV or TS address - plays natively, not in the browser";
            break;
        }
        return row;
    }

    void run(int i)
    {
        App &app = app_;
        switch (kind(i))
        {
        case Kind::lab:
            app_.run_browser_test();
            return;
        case Kind::drm:
        {
            WebSession session;
            session.url = kSecureDrmCheck;
            session.title = "Secure DRM check";
            session.after = [&app] { open_secure_drm_record(app); };
            app_.open_web(std::move(session));
            return;
        }
        case Kind::test:
        {
            const PublicTest &t = test_at(i);
            WebSession session;
            session.url = t.url;
            session.title = t.name;
            const std::string key = web::check_key(t.url), name = t.name;
            session.after = [&app, key, name] { open_playback_record(app, key, name, false); };
            app_.open_web(std::move(session));
            return;
        }
        case Kind::link:
            app_.ask_play_link();
            return;
        }
    }

    void record(int i)
    {
        if (kind(i) == Kind::test)
            open_playback_record(app_, web::check_key(test_at(i).url), test_at(i).name, false);
        else if (kind(i) == Kind::drm)
            open_secure_drm_record(app_);
    }

    int focus_ = 0;
};
} // namespace

std::unique_ptr<Screen> make_lab_screen(App &app)
{
    return std::make_unique<LabScreen>(app);
}
} // namespace akeno
