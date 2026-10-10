// AKENO STREAM PS5 - Settings, diagnostics and licences.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/screens.hpp"
#include "core/fs.hpp"
#include "media/ffmpeg_probe.hpp"
#include "net/http.hpp"
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
const char *kRegions[] = {"US", "GB", "CA", "AU", "DE", "FR", "ES",
                          "IT", "NL", "JP", "KR", "BR", "MX", "IN"};
const char *kSafeSearch[] = {"moderate", "strict", "none"};

template <std::size_t N>
std::string cycle(const char *(&values)[N], const std::string &current, int direction)
{
    std::size_t index = 0;
    for (std::size_t i = 0; i < N; ++i)
        if (current == values[i])
            index = i;
    index = (index + N + (direction > 0 ? 1 : N - 1)) % N;
    return values[index];
}

class SettingsScreen final : public Screen
{
  public:
    explicit SettingsScreen(App &app) : Screen{app}
    {
    }

    enum class Row
    {
        quality,
        volume,
        resume,
        motion,
        youtube_key,
        youtube_region,
        safe_search,
        adult,
        web_search,
        web_check,
        web_pages,
        services,
        diagnostics,
        clear_history,
        clear_recent,
        about,
    };
    static constexpr int kRows = 16;

    void handle(input::Button b) override
    {
        switch (b)
        {
        case input::Button::up:
            row_ = std::max(0, row_ - 1);
            confirm_clear_ = false;
            return;
        case input::Button::down:
            row_ = std::min(kRows - 1, row_ + 1);
            confirm_clear_ = false;
            return;
        case input::Button::left:
            change(-1);
            return;
        case input::Button::right:
            change(1);
            return;
        case input::Button::cross:
            activate();
            return;
        case input::Button::square:
            if (static_cast<Row>(row_) == Row::youtube_key &&
                !app_.store().youtube_api_key().empty())
            {
                app_.store().set_youtube_api_key("");
                app_.toast("YouTube API key removed", th::kInfo);
            }
            return;
        default:
            return;
        }
    }

    void render(ui::Painter &p, std::uint64_t) override
    {
        p.text(th::kMarginX, 160, "Settings", th::kTitle, th::kText);
        const Rect list{th::kMarginX, 240, 1100, 760};
        const int row_h = 62;
        const int visible = list.h / row_h;
        const int first = std::clamp(row_ - visible / 2, 0, std::max(0, kRows - visible));
        int y = list.y;
        for (int i = first; i < kRows && i < first + visible; ++i)
        {
            const Rect r{list.x, y, list.w, row_h - 8};
            const bool focused = i == row_;
            if (focused)
                p.s.fill_rounded(r, 16, th::kText);
            const Pixel fg = focused ? th::kTextOnAccent : th::kText;
            const Pixel dim = focused ? th::kTextOnAccent : th::kTextSecondary;
            p.text(r.x + 28, r.y + (r.h - p.line_height(th::kBodyStrong)) / 2,
                   label(static_cast<Row>(i)), th::kBodyStrong, fg);
            const std::string v = value(static_cast<Row>(i));
            if (!v.empty())
            {
                const bool adjustable = i <= static_cast<int>(Row::web_pages) &&
                                        static_cast<Row>(i) != Row::youtube_key;
                const std::string shown = adjustable && focused ? "<  " + v + "  >" : v;
                p.text_right(r.right() - 28, r.y + (r.h - p.line_height(th::kBody)) / 2, shown,
                             th::kBody, dim);
            }
            y += row_h;
        }
        // Explanation panel for the focused row.
        const Rect help{list.right() + 40, 240, th::kWidth - th::kMarginX - list.right() - 40, 520};
        p.panel(help, th::kPanelRadius, gfx::with_alpha(th::kSurface, 230));
        p.text(help.x + 32, help.y + 28, label(static_cast<Row>(row_)), th::kHeading, th::kText,
               help.w - 64);
        p.wrapped(help.x + 32, help.y + 92, explanation(static_cast<Row>(row_)), th::kBody,
                  th::kTextSecondary, help.w - 64, 10);
        p.text(help.x + 32, help.bottom() + 24,
               "AKENO STREAM " + app_.config().version + "  (" + app_.config().build + ")",
               th::kCaption, th::kTextMuted, help.w - 64);
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        std::vector<Hint> h{{Glyph::cross, "Select"}, {Glyph::dpad, "Change"}};
        if (static_cast<Row>(row_) == Row::youtube_key && !app_.store().youtube_api_key().empty())
            h.push_back({Glyph::square, "Remove key"});
        return h;
    }

  private:
    static const char *label(Row r)
    {
        switch (r)
        {
        case Row::quality:
            return "Maximum video quality";
        case Row::volume:
            return "Volume";
        case Row::resume:
            return "Resume where I left off";
        case Row::motion:
            return "Reduce motion";
        case Row::youtube_key:
            return "YouTube API key";
        case Row::youtube_region:
            return "YouTube region";
        case Row::safe_search:
            return "YouTube SafeSearch";
        case Row::adult:
            return "Show adult anime";
        case Row::web_search:
            return "Web search engine";
        case Row::web_check:
            return "Check websites before opening";
        case Row::web_pages:
            return "Player pages without browser controls";
        case Row::services:
            return "Service status";
        case Row::diagnostics:
            return "Diagnostics";
        case Row::clear_history:
            return "Clear watch history";
        case Row::clear_recent:
            return "Clear recently visited websites";
        case Row::about:
            return "About & licences";
        }
        return "";
    }

    std::string value(Row r) const
    {
        const Settings &s = app_.store().settings();
        switch (r)
        {
        case Row::quality:
            return std::to_string(s.max_height) + "p";
        case Row::volume:
            return std::to_string(s.volume) + "%";
        case Row::resume:
            return s.resume_playback ? "On" : "Off";
        case Row::motion:
            return s.reduce_motion ? "On" : "Off";
        case Row::youtube_key:
            return app_.store().youtube_api_key().empty() ? "Not set" : "Configured";
        case Row::youtube_region:
            return s.youtube_region;
        case Row::safe_search:
            return s.youtube_safe_search;
        case Row::adult:
            return s.show_adult_anime ? "On" : "Off";
        case Row::web_search:
            return web::engine_name(web::engine_from_id(s.web_search));
        case Row::web_check:
            return s.web_check_first ? "On" : "Off";
        case Row::web_pages:
            return s.web_full_screen_pages ? "On (experimental)" : "Off";
        case Row::clear_recent:
            return std::to_string(app_.websites().recent().size()) + " entries";
        case Row::clear_history:
            return confirm_clear_ ? "Press X again to confirm"
                                  : std::to_string(app_.store().history().size()) + " entries";
        default:
            return ">";
        }
    }

    static std::string explanation(Row r)
    {
        switch (r)
        {
        case Row::quality:
            return "The highest resolution chosen from adaptive HLS streams. Lower it on slow "
                   "connections. The PS5 "
                   "hardware decoder handles up to 4K; the interface presents video at 1080p.";
        case Row::volume:
            return "Playback volume for all streams. Up and Down on the D-pad also change it "
                   "during playback.";
        case Row::resume:
            return "Continue videos from the last position (saved every 15 seconds and when you "
                   "stop).";
        case Row::motion:
            return "Turns off scrolling animations.";
        case Row::youtube_key:
            return "Your own YouTube Data API v3 key from Google Cloud Console. It is stored only "
                   "on this console "
                   "(/download0/akeno/secrets.json), never shown in diagnostics and never sent "
                   "anywhere except "
                   "Google's API. Press X to enter or replace it, Square to remove it.";
        case Row::youtube_region:
            return "Region used for YouTube trending charts and search ranking.";
        case Row::safe_search:
            return "YouTube's SafeSearch filter for search results.";
        case Row::adult:
            return "Include titles AniList marks as adult (18+). Off by default.";
        case Row::web_search:
            return "Used when the text typed in Websites' address bar is not an address.";
        case Row::web_check:
            return "Before the browser opens a site, AKENO STREAM checks that it answers and "
                   "explains network problems (name not found, no connection, certificate) in "
                   "plain words. It also finds the site's icon. Nothing is sent but a normal "
                   "request for the start page.";
        case Row::web_pages:
            return "Experimental: AKENO STREAM's own pages (the YouTube player, the browser "
                   "test) fill the screen without the browser's own bar. They carry a Back to "
                   "AKENO button, and AKENO closes the browser if such a page stops answering. "
                   "Off: the browser's normal presentation with its own controls.";
        case Row::clear_recent:
            return "Removes the Recently Visited row in Websites. Your saved websites and "
                   "anything you are signed in to inside the browser stay.";
        case Row::services:
            return "What each streaming service can and cannot do in AKENO, and why.";
        case Row::diagnostics:
            return "Network and media self-tests, decoder statistics and an exportable report for "
                   "troubleshooting.";
        case Row::clear_history:
            return "Removes all resume points and the Continue Watching row. Favorites are kept.";
        case Row::about:
            return "Version information and the open-source components AKENO is built from.";
        }
        return "";
    }

    void change(int direction)
    {
        Settings s = app_.store().settings();
        switch (static_cast<Row>(row_))
        {
        case Row::quality:
        {
            static const int heights[] = {480, 720, 1080};
            int index = s.max_height >= 1080 ? 2 : s.max_height >= 720 ? 1 : 0;
            index = std::clamp(index + direction, 0, 2);
            s.max_height = heights[index];
            break;
        }
        case Row::volume:
            s.volume = std::clamp(s.volume + direction * 10, 0, 100);
            break;
        case Row::resume:
            s.resume_playback = !s.resume_playback;
            break;
        case Row::motion:
            s.reduce_motion = !s.reduce_motion;
            break;
        case Row::youtube_region:
            s.youtube_region = cycle(kRegions, s.youtube_region, direction);
            break;
        case Row::safe_search:
            s.youtube_safe_search = cycle(kSafeSearch, s.youtube_safe_search, direction);
            break;
        case Row::adult:
            s.show_adult_anime = !s.show_adult_anime;
            break;
        case Row::web_search:
        {
            const int count = web::kSearchEngineCount;
            const int index = static_cast<int>(web::engine_from_id(s.web_search));
            s.web_search = web::engine_id(
                static_cast<web::SearchEngine>((index + count + (direction > 0 ? 1 : -1)) % count));
            break;
        }
        case Row::web_check:
            s.web_check_first = !s.web_check_first;
            break;
        case Row::web_pages:
            s.web_full_screen_pages = !s.web_full_screen_pages;
            break;
        default:
            return;
        }
        app_.store().update_settings(s);
        app_.apply_settings();
    }

    void activate()
    {
        switch (static_cast<Row>(row_))
        {
        case Row::resume:
        case Row::motion:
        case Row::adult:
        case Row::web_check:
        case Row::web_pages:
            change(1);
            return;
        case Row::clear_recent:
            app_.websites().clear_recent();
            app_.toast("Recently visited websites cleared", th::kInfo);
            return;
        case Row::youtube_key:
            app_.open_keyboard(
                "YouTube Data API key", "", 39, true,
                [this](bool ok, const std::string &text)
                {
                    if (!ok)
                        return;
                    if (!plausible_youtube_key(text))
                    {
                        app_.toast("Not a valid key: 39 characters starting with AIza", th::kError);
                        return;
                    }
                    app_.store().set_youtube_api_key(text);
                    app_.toast("YouTube API key saved", th::kSuccess);
                });
            return;
        case Row::services:
            services_ = (services_ + 1) % 4;
            switch (services_)
            {
            case 0:
                app_.show_provider_status(app_.open_catalog());
                break;
            case 1:
                app_.show_provider_status(app_.anilist());
                break;
            case 2:
                app_.show_provider_status(app_.youtube());
                break;
            default:
                app_.push(make_crunchyroll_screen(app_));
                break;
            }
            return;
        case Row::diagnostics:
            app_.push(make_diagnostics_screen(app_));
            return;
        case Row::clear_history:
            if (!confirm_clear_)
            {
                confirm_clear_ = true;
                return;
            }
            app_.store().clear_history();
            confirm_clear_ = false;
            app_.toast("Watch history cleared", th::kInfo);
            return;
        case Row::about:
            app_.push(make_licenses_screen(app_));
            return;
        default:
            change(1);
            return;
        }
    }

    int row_ = 0;
    int services_ = -1;
    bool confirm_clear_ = false;
};

// ---------------------------------------------------------------------------
class DiagnosticsScreen final : public Screen
{
  public:
    explicit DiagnosticsScreen(App &app) : Screen{app}
    {
    }
    ~DiagnosticsScreen() override
    {
        *alive_ = false;
    }

    void handle(input::Button b) override
    {
        switch (b)
        {
        case input::Button::left:
            action_ = std::max(0, action_ - 1);
            return;
        case input::Button::right:
            action_ = std::min(4, action_ + 1);
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

    [[nodiscard]] bool animating() const override
    {
        const Diagnostics &d = app_.diagnostics();
        return d.network.state == TestResult::State::running ||
               d.software_decode.state == TestResult::State::running;
    }

    void render(ui::Painter &p, std::uint64_t now_ms) override
    {
        const DiagnosticSnapshot s = app_.snapshot();
        const Diagnostics &d = app_.diagnostics();
        p.text(th::kMarginX, 150, "Diagnostics", th::kTitle, th::kText);

        const int col_w = (th::kWidth - 2 * th::kMarginX - 2 * 28) / 3;
        const Rect system{th::kMarginX, 222, col_w, 600};
        const Rect network{system.right() + 28, 222, col_w, 290};
        const Rect media_box{system.right() + 28, 532, col_w, 290};
        const Rect playback{network.right() + 28, 222, col_w, 600};
        const auto heading = [&](const Rect &r, const char *title)
        {
            p.panel(r, 20, gfx::with_alpha(th::kSurface, 235));
            p.text(r.x + 26, r.y + 18, title, th::kSubheading, th::kText);
        };
        const auto kv = [&](const Rect &r, int &y, const std::string &k, const std::string &v)
        {
            p.text(r.x + 26, y, k, th::kSmall, th::kTextMuted);
            p.text(r.x + 26, y + 24, v, th::kCaption, th::kText, r.w - 52);
            y += 60;
        };

        heading(system, "System");
        int y = system.y + 70;
        kv(system, y, "Application", "AKENO STREAM " + s.app_version + " (" + s.build + ")");
        kv(system, y, "Firmware", s.firmware);
        kv(system, y, "Platform", s.platform);
        kv(system, y, "Data folder", s.data_dir);
        kv(system, y, "Text rendering", s.fonts);
        char line[160];
        std::snprintf(line, sizeof(line), "%llu frames, render %u us, copy+flip %u us",
                      static_cast<unsigned long long>(s.frames_presented), s.last_frame_us,
                      s.last_present_us);
        kv(system, y, "Display", line);
        std::snprintf(line, sizeof(line), "%zu images, %zu KiB", s.images_cached,
                      s.image_bytes / 1024);
        kv(system, y, "Artwork cache", line);
        kv(system, y, "Controller", s.controller_connected ? "Connected" : "Not connected");

        heading(network, "Network");
        y = network.y + 70;
        test_line(p, network, y, d.network, now_ms);
        p.text(network.x + 26, network.bottom() - 46,
               "libcurl " + s.curl_version.substr(0, s.curl_version.find(' ', 13)), th::kSmall,
               th::kTextMuted, network.w - 52);

        heading(media_box, "Media");
        y = media_box.y + 70;
        p.text(media_box.x + 26, y, s.ffmpeg, th::kSmall, th::kTextMuted, media_box.w - 52);
        y += 34;
        test_line(p, media_box, y, d.software_decode, now_ms);

        heading(playback, "Last playback");
        y = playback.y + 70;
        const media::PlayerStatus &ps = s.player;
        kv(playback, y, "State",
           std::string{media::state_name(ps.state)} + (ps.title.empty() ? "" : " - " + ps.title));
        kv(playback, y, "Decoder", ps.decoder.empty() ? "-" : ps.decoder);
        std::snprintf(line, sizeof(line), "%s %dx%d, audio %s", ps.video_codec.c_str(), ps.width,
                      ps.height, ps.audio_codec.empty() ? "none" : ps.audio_codec.c_str());
        kv(playback, y, "Stream", line);
        std::snprintf(line, sizeof(line), "%llu decoded, %llu presented, %llu dropped",
                      static_cast<unsigned long long>(ps.frames_decoded),
                      static_cast<unsigned long long>(ps.frames_presented),
                      static_cast<unsigned long long>(ps.frames_dropped));
        kv(playback, y, "Frames", line);
        std::snprintf(line, sizeof(line), "%llu audio frames, %llu underruns, %llu errors",
                      static_cast<unsigned long long>(ps.audio_frames),
                      static_cast<unsigned long long>(ps.audio_underruns),
                      static_cast<unsigned long long>(ps.audio_errors));
        kv(playback, y, "Audio", line);
        kv(playback, y, "Last error",
           ps.error.empty() ? (d.errors().empty() ? "none" : d.errors().front().message)
                            : ps.error);

        // Actions.
        static const char *labels[] = {"Run network test", "Run media self-test",
                                       "Play hardware test clip", "Browser", "Export report"};
        static const Icon icons[] = {Icon::refresh, Icon::film, Icon::play, Icon::spark,
                                     Icon::check};
        int x = th::kMarginX;
        for (int i = 0; i < 5; ++i)
            x += p.button(x, 860, labels[i], i == action_, th::kAccentSettings, icons[i]) + 18;
        if (!exported_.empty())
            p.text(th::kMarginX, 950, exported_, th::kCaption, th::kSuccess,
                   th::kWidth - 2 * th::kMarginX);
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        return {{Glyph::cross, "Run"}, {Glyph::dpad, "Choose"}, {Glyph::circle, "Back"}};
    }

  private:
    void test_line(ui::Painter &p, const Rect &box, int y, const TestResult &t,
                   std::uint64_t now_ms)
    {
        Pixel color = th::kTextMuted;
        const char *label = "Not run";
        switch (t.state)
        {
        case TestResult::State::running:
            p.spinner(box.x + 40, y + 16, 12, now_ms, th::kInfo);
            label = "Running...";
            color = th::kInfo;
            break;
        case TestResult::State::passed:
            label = "Passed";
            color = th::kSuccess;
            break;
        case TestResult::State::failed:
            label = "Failed";
            color = th::kError;
            break;
        default:
            break;
        }
        if (t.state != TestResult::State::running)
            p.s.fill_circle(box.x + 40, y + 16, 9, color);
        p.text(box.x + 62, y, label, th::kBodyStrong, color);
        p.wrapped(box.x + 26, y + 44, t.summary, th::kCaption, th::kTextSecondary, box.w - 52, 4);
    }

    void run(int action)
    {
        Diagnostics &d = app_.diagnostics();
        auto alive = alive_;
        App &app = app_;
        switch (action)
        {
        case 0:
            if (d.network.state == TestResult::State::running)
                return;
            d.network.state = TestResult::State::running;
            d.network.summary.clear();
            app_.jobs().run(
                [&app, alive]
                {
                    TestResult result = Diagnostics::run_network_test();
                    app.jobs().post([&app, result = std::move(result)]() mutable
                                    { app.diagnostics().network = std::move(result); });
                });
            return;
        case 1:
        {
            if (d.software_decode.state == TestResult::State::running)
                return;
            d.software_decode.state = TestResult::State::running;
            d.software_decode.summary.clear();
            const std::string clip =
                fs::join(platform::app_dir(), "assets/selftest/h264-aac-360p.ts");
            app_.jobs().run(
                [&app, clip]
                {
                    TestResult result = Diagnostics::run_media_self_test(clip);
                    app.jobs().post([&app, result = std::move(result)]() mutable
                                    { app.diagnostics().software_decode = std::move(result); });
                });
            return;
        }
        case 2:
        {
            const auto clips = app_.open_catalog().bundled();
            if (!clips.empty())
                app_.play(clips.front(), 0.0);
            return;
        }
        case 3:
            app_.push(make_browser_tests_screen(app_));
            return;
        default:
        {
            std::string path, error;
            const std::string report = d.build_report(app_.snapshot());
            if (Diagnostics::export_report(platform::data_dir(), report,
                                           platform::wall_clock_seconds(), &path, &error))
            {
                exported_ = "Report saved: " + path + "  -  on a PC (FTP, while the app runs): " +
                            platform::data_dir_from_pc();
                app_.toast("Diagnostics report saved", th::kSuccess);
            }
            else
            {
                exported_.clear();
                app_.toast("Could not save the report: " + error, th::kError);
            }
            return;
        }
        }
    }

    int action_ = 0;
    std::string exported_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

// ---------------------------------------------------------------------------
class LicensesScreen final : public Screen
{
  public:
    explicit LicensesScreen(App &app) : Screen{app}
    {
    }
    void handle(input::Button b) override
    {
        if (b == input::Button::down)
            scroll_ = std::clamp(scroll_ + 1, 0, std::max(0, static_cast<int>(lines().size()) - 1));
        else if (b == input::Button::up)
            scroll_ = std::max(0, scroll_ - 1);
        else if (b == input::Button::circle)
            app_.pop();
    }
    void render(ui::Painter &p, std::uint64_t) override
    {
        p.text(th::kMarginX, 150, "About & licences", th::kTitle, th::kText);
        const Rect box{th::kMarginX, 230, th::kWidth - 2 * th::kMarginX, 760};
        p.panel(box, th::kPanelRadius, gfx::with_alpha(th::kSurface, 230));
        int y = box.y + 30;
        const auto &all = lines();
        for (std::size_t i = static_cast<std::size_t>(scroll_);
             i < all.size() && y < box.bottom() - 40; ++i)
        {
            const bool heading = !all[i].empty() && all[i][0] == '#';
            const std::string text = heading ? all[i].substr(1) : all[i];
            y += p.wrapped(box.x + 40, y, text, heading ? th::kBodyStrong : th::kCaption,
                           heading ? th::kText : th::kTextSecondary, box.w - 80, 3) +
                 (heading ? 6 : 10);
        }
    }
    [[nodiscard]] std::vector<Hint> hints() const override
    {
        return {{Glyph::dpad, "Scroll"}, {Glyph::circle, "Back"}};
    }

  private:
    static const std::vector<std::string> &lines()
    {
        static const std::vector<std::string> text = {
            "#AKENO STREAM PS5",
            "An independent homebrew media application for jailbroken PlayStation 5 consoles. Not "
            "affiliated with "
            "or endorsed by Sony, Crunchyroll, Google/YouTube or AniList. Licensed under the GNU "
            "GPL v3 or later.",
            "#Built with",
            "ps5-native-app-boilerplate (BlackBearReloaded, GPL-3.0-or-later): native title "
            "tooling, runtime shim, "
            "VideoOut setup.",
            "ProsperoTV (BlackBearReloaded, GPL-3.0-or-later): MPEG-TS demuxer and the hardware "
            "Videodec2/Audiodec "
            "playback backend.",
            "PS5 Payload SDK (John Tornblom, GPL-3.0-or-later) and PacBrew ports: libcurl (curl "
            "licence), "
            "OpenSSL (Apache-2.0), zlib, zstd, libpsl, FreeType (FTL).",
            "FFmpeg 8.0.1 (LGPL-2.1-or-later as configured): MP4/Matroska demuxing, MPEG-TS "
            "remuxing, software "
            "audio decoding and the media self-test.",
            "minimp3 (CC0-1.0), stb_image (public domain / MIT), QR Code generator by Project "
            "Nayuki (MIT).",
            "Inter typeface by Rasmus Andersson (SIL Open Font License 1.1).",
            "#Data sources",
            "Anime data and artwork: AniList (anilist.co) public API. YouTube data: YouTube Data "
            "API v3 with your "
            "own key; YouTube is a trademark of Google LLC.",
            "Big Buck Bunny, Sintel and Tears of Steel: (c) Blender Foundation, CC BY 3.0. Public "
            "test streams are "
            "operated by Mux, Bitmovin, Apple and Akamai.",
            "#Source code",
            "https://github.com/YTDxDAkeno/AKENO-ANIME-PS5 - full source, build instructions and "
            "the complete "
            "THIRD_PARTY_NOTICES.md.",
        };
        return text;
    }
    int scroll_ = 0;
};
} // namespace

std::unique_ptr<Screen> make_settings_screen(App &app)
{
    return std::make_unique<SettingsScreen>(app);
}
std::unique_ptr<Screen> make_diagnostics_screen(App &app)
{
    return std::make_unique<DiagnosticsScreen>(app);
}
std::unique_ptr<Screen> make_licenses_screen(App &app)
{
    return std::make_unique<LicensesScreen>(app);
}
} // namespace akeno
