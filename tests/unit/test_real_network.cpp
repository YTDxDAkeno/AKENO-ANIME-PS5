// AKENO STREAM PS5 - Real-network acceptance: an authorized provider into the native player.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Skipped unless AKENO_NETWORK_ACCEPTANCE=1 (.github/workflows/network-acceptance.yml
// runs it on a GitHub runner, which can reach the internet). Nothing here is
// recorded or served locally: the real App, with its real HTTP client, talks
// to a real PeerTube instance through its documented REST API, the same way
// it does on the console, and plays a real episode in AKENO's native player.
// FFmpeg's software decoder stands in for the PS5's hardware decoder: this
// proves the software path end to end over the real network, not the console.
//
// The default source is video.blender.org, the Blender Foundation's own
// PeerTube instance: open movies released under Creative Commons licences,
// DRM-free, published there by their rights holder for anyone to watch.
// They are animated films, not anime, and are identified as such in the
// report. AKENO_NETWORK_PROVIDER and AKENO_NETWORK_SERIES choose another
// instance or channel.
//
// Through the controller, from an empty data folder: Anime > Streaming
// Providers > Add Streaming Provider > the instance's address typed on the
// on-screen keyboard > detection > Add Provider > the provider's home (its
// catalogue from the instance) > a series (channel) > its season (playlist)
// > the first episode > AKENO's native player: picture and sound counted,
// the provider verified, subtitles (Triangle) where the episode has any,
// pause and resume (Cross), seek (Right, Left), the next episode (R2), stop
// (Circle) with the position saved > restart the App > the provider, its
// "verified" state and Continue Watching are still there > the series'
// Continue action resumes the episode where it stopped.
//
// AKENO_NETWORK_REPORT names a Markdown file for the step-by-step report and
// AKENO_NETWORK_SHOTS a folder for screenshots of the real screens.
#include "anime/registry.hpp"
#include "anime/session.hpp"
#include "app/app.hpp"
#include "app/screens.hpp"
#include "core/fs.hpp"
#include "core/url.hpp"
#include "host_web_view.hpp"
#include "net/http.hpp"
#include "platform/platform.hpp"
#include "software_sink.hpp"
#include "stb/stb_image_write.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace akeno;

namespace
{
std::string env_or(const char *name, const std::string &fallback)
{
    const char *v = std::getenv(name);
    return v && *v ? std::string{v} : fallback;
}

bool enabled()
{
    return env_or("AKENO_NETWORK_ACCEPTANCE", "") == "1";
}

std::string fresh_dir()
{
    char path[] = "/tmp/akeno-real-network-XXXXXX";
    return mkdtemp(path) ? path : "/tmp";
}

std::string fixed(double v)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f", v);
    return text;
}

// What happened at each step, written as Markdown for the workflow summary.
class Report
{
  public:
    void step(const std::string &name, bool ok, const std::string &detail)
    {
        steps_.push_back({name, ok ? "PASS" : "FAIL", detail});
        std::printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str());
    }
    void info(const std::string &name, const std::string &result, const std::string &detail)
    {
        steps_.push_back({name, result, detail});
        std::printf("[%s] %s: %s\n", result.c_str(), name.c_str(), detail.c_str());
    }
    void fact(const std::string &key, const std::string &value)
    {
        facts_.emplace_back(key, value);
        std::printf("  %s: %s\n", key.c_str(), value.c_str());
    }
    ~Report()
    {
        const std::string path = env_or("AKENO_NETWORK_REPORT", "");
        if (path.empty())
            return;
        std::string out = "# AKENO STREAM real-network acceptance (host, software decoder)\n\n";
        out += "Real network, real provider, real media; FFmpeg's software decoder on a Linux "
               "runner stands in for the PS5 hardware decoder. Not a console result.\n\n";
        out += "| Fact | Value |\n| --- | --- |\n";
        for (const auto &[k, v] : facts_)
            out += "| " + k + " | " + v + " |\n";
        out += "\n| Step | Result | Detail |\n| --- | --- | --- |\n";
        for (const Row &r : steps_)
            out += "| " + r.name + " | " + r.result + " | " + r.detail + " |\n";
        (void)fs::write_atomic(path, out);
    }

  private:
    struct Row
    {
        std::string name, result, detail;
    };
    std::vector<Row> steps_;
    std::vector<std::pair<std::string, std::string>> facts_;
};

class Harness
{
  public:
    Harness() : dir_{fresh_dir()}
    {
        setenv("AKENO_DATA_DIR", dir_.c_str(), 1);
        test::reset_web_view_script();
        net::set_test_transport({}); // the real HTTP client
        start();
    }
    ~Harness()
    {
        stop();
    }
    void start()
    {
        AppConfig config;
        config.make_sink = [this] { return test::make_software_sink(app_->frames(), record_); };
        app_ = std::make_unique<App>(config, fonts_);
        app_->start(now_);
        app_->switch_mode(Mode::anime);
        step(5);
    }
    void stop()
    {
        if (!app_)
            return;
        app_->player().stop();
        app_.reset();
    }
    void restart()
    {
        stop();
        start();
    }
    App &app()
    {
        return *app_;
    }
    test::SinkRecord &record()
    {
        return *record_;
    }
    void step(int frames = 1)
    {
        for (int i = 0; i < frames; ++i)
        {
            now_ += 16;
            app_->update(now_);
            if (app_->needs_redraw())
                app_->render(surface_, now_);
        }
    }
    void press(input::Button b, int times = 1)
    {
        for (int i = 0; i < times; ++i)
        {
            app_->handle({b, false});
            step(2);
        }
    }
    // Wall-clock bound: rendering a 1080p frame with the sanitizers takes far
    // longer than the app's 16 ms frame on a runner.
    bool until(const std::function<bool()> &done, int seconds)
    {
        const std::uint64_t end =
            platform::monotonic_us() + static_cast<std::uint64_t>(seconds) * 1000000u;
        while (platform::monotonic_us() < end)
        {
            if (done())
                return true;
            step();
            platform::sleep_us(10000);
        }
        return done();
    }
    bool settled(int seconds)
    {
        return until([&] { return app_->jobs().pending() == 0; }, seconds) && (step(4), true);
    }
    void type(const std::string &text)
    {
        static const char *rows[] = {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm_.@"};
        int row = 1, col = 0;
        for (const char c : text)
        {
            int r = 0, k = 0;
            for (r = 0; r < 4; ++r)
                if (const char *at = std::strchr(rows[r], c))
                {
                    k = static_cast<int>(at - rows[r]);
                    break;
                }
            ASSERT_LT(r, 4) << c;
            for (; row < r; ++row)
                press(input::Button::down);
            for (; row > r; --row)
                press(input::Button::up);
            for (; col < k; ++col)
                press(input::Button::right);
            for (; col > k; --col)
                press(input::Button::left);
            press(input::Button::cross);
        }
        press(input::Button::options);
    }
    [[nodiscard]] std::string top() const
    {
        return app_->top_screen_id();
    }
    media::PlayerStatus status()
    {
        return app_->player().status();
    }
    // Moves the provider home's focus onto a card with the D-pad: row by row,
    // left to right. False when no row holds it.
    bool focus_on(const std::string &card)
    {
        press(input::Button::down); // into the shelves
        for (int row = 0; row < 40; ++row)
        {
            press(input::Button::left, 14);
            for (int col = 0; col < 14; ++col)
            {
                if (app_->top_focus_id() == card)
                    return true;
                press(input::Button::right);
            }
            if (app_->top_focus_id() == card)
                return true;
            const std::string before = app_->top_focus_id();
            press(input::Button::down);
            if (app_->top_focus_id() == before)
                return false; // the last row
        }
        return false;
    }
    [[nodiscard]] const HistoryEntry *history(const std::string &key) const
    {
        for (const HistoryEntry &h : app_->store().history())
            if (h.item.id == key)
                return &h;
        return nullptr;
    }
    void shot(const std::string &name)
    {
        const std::string folder = env_or("AKENO_NETWORK_SHOTS", "");
        if (folder.empty())
            return;
        fs::make_directory(folder);
        app_->render(surface_, now_);
        std::FILE *file = std::fopen(fs::join(folder, name + ".png").c_str(), "wb");
        if (!file)
            return;
        stbi_write_png_to_func(
            [](void *context, void *data, int size) {
                std::fwrite(data, 1, static_cast<std::size_t>(size),
                            static_cast<std::FILE *>(context));
            },
            file, 1920, 1080, 4, pixels_.data(), 1920 * 4);
        std::fclose(file);
    }

  private:
    std::string dir_;
    gfx::FontEngine fonts_;
    std::vector<gfx::Pixel> pixels_ = std::vector<gfx::Pixel>(1920 * 1080);
    gfx::Surface surface_{pixels_.data(), 1920, 1080, 1920};
    std::uint64_t now_ = 1000;
    std::shared_ptr<test::SinkRecord> record_ = std::make_shared<test::SinkRecord>();
    std::unique_ptr<App> app_;
};

// A season with at least two episodes whose first episode was checked and
// plays natively: the index, or -1.
int playable_season(const anime::Series &s)
{
    for (std::size_t i = 0; i < s.seasons.size(); ++i)
    {
        const auto &episodes = s.seasons[i].episodes;
        if (episodes.size() >= 2 && episodes[0].availability == anime::Availability::playable)
            return static_cast<int>(i);
    }
    return -1;
}

// When detection did not offer Add Provider: what the instance answers and
// what the same check found, in the job log (the artifact is not always at hand).
void explain_detection(const std::string &host, Report &report)
{
    net::Client client;
    net::Request request;
    request.url = "https://" + host + "/api/v1/config";
    const net::Response r = client.perform(request);
    report.fact("GET /api/v1/config", "outcome " + std::to_string(static_cast<int>(r.outcome)) +
                                          ", HTTP " + std::to_string(r.status) + ", " +
                                          std::to_string(r.body.size()) + " bytes" +
                                          (r.error.empty() ? "" : ", " + r.error));
    anime::ProviderConfig config;
    const anime::ProviderReport check =
        anime::check_source("https://" + host, "", {}, net::make_cancel_flag(), &config);
    report.fact("Check: verdict", check.verdict);
    report.fact("Check: source", anime::source_class_label(check.source));
    report.fact("Check: format / adapter", check.format + " / " + check.adapter);
    report.fact("Check: importable", check.importable ? "yes" : "no");
    report.fact("Check: playback", anime::playback_class_label(check.playback));
    report.fact("Check: counts",
                std::to_string(check.series) + " series, " + std::to_string(check.episodes) +
                    " episodes, " + std::to_string(check.with_media) + " name a video, " +
                    std::to_string(check.probed) + " checked, " + std::to_string(check.playable) +
                    " playable, " + std::to_string(check.network) + " unreachable, " +
                    std::to_string(check.unsupported) + " unsupported, " +
                    std::to_string(check.missing) + " missing");
    for (const std::string &e : check.evidence)
        report.fact("Check: evidence", e);
    for (const anime::CatalogIssue &e : check.errors)
        report.fact("Check: error", e.path + " " + e.message);
    for (const anime::CatalogIssue &w : check.warnings)
        report.fact("Check: warning", w.path + " " + w.message);
}

std::string stream_line(const media::PlayerStatus &s)
{
    return s.container + ", " + s.video_codec + " " + std::to_string(s.width) + "x" +
           std::to_string(s.height) + ", " + s.audio_codec + " " + std::to_string(s.audio_rate) +
           " Hz " + std::to_string(s.audio_channels) + " ch" +
           (s.variant.empty() ? "" : ", variant " + s.variant) +
           (s.variant_count ? " of " + std::to_string(s.variant_count) : "");
}
} // namespace

TEST(RealNetwork, AuthorizedProviderToNativePlayerEndToEnd)
{
    if (!enabled())
        GTEST_SKIP() << "set AKENO_NETWORK_ACCEPTANCE=1 to run against the real network";
    const std::string host = env_or("AKENO_NETWORK_PROVIDER", "video.blender.org");
    const std::string wanted = env_or("AKENO_NETWORK_SERIES", "");
    Report report;
    report.fact("Source", "https://" + host + " (PeerTube REST API, documented)");
    report.fact("Decoder", "FFmpeg software decoder (host); the PS5 uses its hardware decoder");
    Harness h;
    App &app = h.app();
    // Settings > Maximum video quality 480p: the software decoder (with the
    // sanitizers) keeps up; the console decodes in hardware at any of them.
    Settings settings = app.store().settings();
    settings.max_height = 480;
    app.store().update_settings(settings);
    report.fact("Maximum video quality", "480p (Settings; for the software decoder)");

    // 1. Anime > Streaming Providers > Add Streaming Provider > the address.
    app.push(make_anime_providers_screen(app));
    h.step(3);
    ASSERT_EQ(h.top(), "providers");
    h.press(input::Button::cross);
    ASSERT_EQ(h.top(), "add-provider");
    h.press(input::Button::cross);
    ASSERT_TRUE(app.keyboard_active());
    h.type(host);
    const bool detected = h.until([&] { return app.top_focus_id() != "detecting"; }, 300);
    h.settled(60);
    h.shot("01-detection");
    const std::string button = app.top_focus_id();
    report.fact("Detection", std::string{detected ? "finished" : "still running after 300 s"} +
                                 ", focus on \"" + button + "\"");
    if (button != "add-provider")
        explain_detection(host, report);
    h.press(input::Button::cross); // Add Provider
    const std::string home = h.top();
    const bool added = home.rfind("provider-home:", 0) == 0;
    report.step("Provider import", added,
                added ? "Recognised and added; its home opened (" + home + ")"
                      : "Not added; top screen " + home + ", " + app.latest_toast());
    ASSERT_TRUE(added);
    const std::string pid = home.substr(std::strlen("provider-home:"));
    const anime::ProviderConfig *config = app.anime_registry().config(pid);
    ASSERT_NE(config, nullptr);
    report.fact("Provider", config->name + " (" + config->adapter + " adapter)");
    report.fact("State after the check",
                anime::playback_class_label(app.anime_registry().playback_of(pid)));

    // 2. Its catalogue, from the instance.
    const bool loaded = h.until(
        [&]
        {
            const anime::ProviderState *s = app.anime_registry().state(pid);
            return s && (s->loaded || !s->error.empty());
        },
        180);
    h.settled(180);
    const anime::ProviderState *state = app.anime_registry().state(pid);
    ASSERT_TRUE(loaded && state && state->loaded)
        << (state ? state->error : std::string{"no state"});
    std::string titles;
    for (std::size_t i = 0; i < state->series.size() && i < 12; ++i)
        titles += (i ? "; " : "") + state->series[i].title;
    report.step("Anime catalogue", !state->series.empty(),
                std::to_string(state->series.size()) + " series: " + titles);
    h.shot("02-provider-home");

    // 3. A series (channel) with a season (playlist) whose episodes play.
    std::string sid;
    int season = -1;
    std::vector<std::string> candidates;
    for (const anime::Series &s : state->series)
        if (wanted.empty() || s.title == wanted)
            candidates.push_back(s.id);
    for (std::size_t i = 0; i < candidates.size() && i < 8 && season < 0; ++i)
    {
        const std::string card = pid + "/" + candidates[i];
        if (!h.focus_on(card))
        {
            report.info("Series " + candidates[i], "SKIP", "not reachable with the D-pad");
            continue;
        }
        h.press(input::Button::cross);
        if (h.top() != "series:" + card)
            continue;
        h.until(
            [&]
            {
                const anime::Series *s = app.anime_registry().find_series(pid, candidates[i]);
                return s && !s->details_pending;
            },
            180);
        h.settled(180);
        const anime::Series *s = app.anime_registry().find_series(pid, candidates[i]);
        if (s && !s->details_pending && (season = playable_season(*s)) >= 0)
        {
            sid = candidates[i];
            break;
        }
        report.info("Series " + (s ? s->title : candidates[i]), "SKIP",
                    s ? std::to_string(s->seasons.size()) +
                            " seasons, none with two episodes whose first plays"
                      : "not found");
        h.press(input::Button::circle); // back to the provider's home
        h.step(5);
        h.press(input::Button::up, 8); // the actions, so the next search starts at the top
    }
    ASSERT_GE(season, 0) << "no series with a playable season among the first candidates";
    const anime::Series *series = app.anime_registry().find_series(pid, sid);
    ASSERT_NE(series, nullptr);
    const anime::Season &chosen = series->seasons[static_cast<std::size_t>(season)];
    const anime::Episode &first = chosen.episodes[0];
    const anime::Episode &second = chosen.episodes[1];
    const std::string key1 = anime::episode_key(pid, sid, chosen.id, first.id);
    const std::string key2 = anime::episode_key(pid, sid, chosen.id, second.id);
    report.fact("Series",
                series->title + " (" + std::to_string(series->seasons.size()) + " seasons)");
    report.fact("Season",
                chosen.title + " (" + std::to_string(chosen.episodes.size()) + " episodes)");
    report.fact("Episode 1", anime::episode_heading(chosen, first));
    report.fact("Episode 2", anime::episode_heading(chosen, second));
    report.step("Season and episode list", true,
                std::to_string(chosen.episodes.size()) + " episodes listed; episode 1 checked: " +
                    anime::availability_label(first.availability));
    h.shot("03-series");

    // 4. Seasons row, this season, the episodes, the first: AKENO's player.
    h.press(input::Button::down);
    h.press(input::Button::right, season);
    h.press(input::Button::down);
    h.press(input::Button::cross);
    const bool playing =
        h.until([&] { return h.status().state == media::PlayerState::playing; }, 90);
    report.step("Native playback starts", playing,
                playing
                    ? stream_line(h.status())
                    : std::string{media::state_name(h.status().state)} + " " + h.status().error);
    ASSERT_TRUE(playing);
    const bool av =
        h.until([&] { return h.record().frames > 30 && h.record().audio_units > 24; }, 90);
    report.step("Picture and audio", av,
                std::to_string(h.record().frames.load()) + " pictures decoded and presented, " +
                    std::to_string(h.record().audio_units.load()) + " audio frames, decoder " +
                    h.status().decoder + ", decoder errors " +
                    std::to_string(h.status().decoder_errors));
    ASSERT_TRUE(av);
    const bool verified =
        h.until([&] { return app.anime_registry().native_playback(pid) != nullptr; }, 30);
    report.step("Provider state: Native playback verified", verified,
                anime::playback_class_label(app.anime_registry().playback_of(pid)));
    EXPECT_TRUE(verified);
    h.step(60);
    h.shot("04-player");

    // 5. Subtitles: Triangle cycles the episode's tracks (when it has any).
    const std::string at_start = app.latest_toast();
    h.press(input::Button::triangle);
    h.step(30);
    const std::string toast = app.latest_toast();
    if (toast == "This video has no subtitles")
        report.info("Subtitles", "N/A", "the episode publishes no captions on " + host);
    else
        report.step("Subtitles",
                    toast.rfind("Subtitles", 0) == 0 &&
                        toast.find("could not be read") == std::string::npos,
                    "Triangle: \"" + toast + "\"" +
                        (at_start.empty() ? "" : " (at start: \"" + at_start + "\")"));
    h.press(input::Button::triangle); // and once more (next track or off)
    h.step(10);

    // 6. Pause and resume.
    h.press(input::Button::cross);
    const bool paused = h.until([&] { return h.status().state == media::PlayerState::paused; }, 15);
    const double paused_at = h.status().position;
    h.until([] { return false; }, 2);
    const bool held = h.status().position < paused_at + 0.5;
    h.press(input::Button::cross);
    const bool resumed =
        h.until([&] { return h.status().state == media::PlayerState::playing; }, 15);
    report.step("Pause and resume", paused && held && resumed,
                "paused at " + fixed(paused_at) + " s (held: " + (held ? "yes" : "no") +
                    "), resumed: " + (resumed ? "yes" : "no"));
    EXPECT_TRUE(paused && held && resumed);

    // 7. Seek: +10 s, then -10 s.
    h.until([&] { return h.status().position > 3.0; }, 30);
    const double before = h.status().position;
    h.press(input::Button::right);
    const bool forward = h.until([&] { return h.status().position >= before + 8.0; }, 60);
    const double after = h.status().position;
    report.step("Seek forward (+10 s)", forward,
                "from " + fixed(before) + " s to " + fixed(after) + " s");
    EXPECT_TRUE(forward);
    h.until([&] { return h.status().position >= after + 2.0; }, 30);
    const double again = h.status().position;
    h.press(input::Button::left);
    const bool back = h.until(
        [&]
        {
            const media::PlayerStatus s = h.status();
            return s.state == media::PlayerState::playing && s.position <= again - 5.0 &&
                   s.position > 0.0;
        },
        60);
    report.step("Seek back (-10 s)", back,
                "from " + fixed(again) + " s to " + fixed(h.status().position) + " s");
    EXPECT_TRUE(back);
    // Playback after the seeks: pictures keep coming.
    const auto frames_before = h.record().frames.load();
    const bool continues = h.until([&] { return h.record().frames > frames_before + 30; }, 60);
    report.step("Playback recovers after seeking", continues,
                std::to_string(h.record().frames.load() - frames_before) + " more pictures");
    EXPECT_TRUE(continues);
    h.until([&] { return h.status().position >= 12.0; }, 60);
    const double first_position = h.status().position;

    // 8. The next episode: R2 in the player. The sink's counts run across
    // streams (a seek may open the stream again), so they are compared with
    // what they were before.
    const int opens_before = h.record().opens.load();
    const auto frames_at_next = h.record().frames.load();
    const auto audio_at_next = h.record().audio_units.load();
    h.press(input::Button::r2);
    const bool next = h.until(
        [&] {
            return h.record().opens > opens_before &&
                   h.status().state == media::PlayerState::playing;
        },
        90);
    h.step(10);
    const HistoryEntry *one = h.history(key1);
    report.step("Next episode (R2)", next && one,
                std::string{"episode 2 playing: "} + (next ? "yes" : "no") +
                    "; episode 1 saved at " + (one ? fixed(one->position) : std::string{"-"}) +
                    " s (left at " + fixed(first_position) + " s)");
    ASSERT_TRUE(next);
    EXPECT_NE(one, nullptr);
    if (one)
        EXPECT_GE(one->position, 5.0);
    const bool av2 = h.until(
        [&] {
            return h.record().frames > frames_at_next + 30 &&
                   h.record().audio_units > audio_at_next + 24;
        },
        90);
    report.step("Episode 2 picture and audio", av2,
                stream_line(h.status()) + "; " +
                    std::to_string(h.record().frames.load() - frames_at_next) + " pictures, " +
                    std::to_string(h.record().audio_units.load() - audio_at_next) +
                    " audio frames");
    EXPECT_TRUE(av2);

    // 9. Progress: seek in, stop; the position is kept for episode 2.
    h.press(input::Button::right);
    h.until([&] { return h.status().position >= 12.0; }, 60);
    const double stopped_at = h.status().position;
    h.press(input::Button::circle);
    h.step(20);
    const HistoryEntry *two = h.history(key2);
    const bool saved = two && two->position >= 10.0 && !two->finished();
    // A copy: the history entry belongs to the App that the restart replaces.
    const double saved_position = two ? two->position : -1.0;
    report.step("Playback progress saved", saved,
                "episode 2 stopped at " + fixed(stopped_at) + " s; saved " +
                    (two ? fixed(saved_position) + " s" : std::string{"nothing"}));
    EXPECT_TRUE(saved);
    h.shot("05-series-after-stop");

    // 10. Restart: the provider, its state and the progress are still there.
    h.restart();
    App &after_restart = h.app();
    const anime::ProviderConfig *kept = after_restart.anime_registry().config(pid);
    const bool still =
        kept && after_restart.anime_registry().native_playback(pid) &&
        after_restart.anime_registry().playback_of(pid) == anime::PlaybackClass::verified;
    bool listed = false;
    for (const HistoryEntry &e : after_restart.store().continue_watching())
        listed = listed || e.item.id == key2;
    report.step("App restart", still && listed,
                std::string{"provider kept: "} + (kept ? "yes" : "no") + "; state: " +
                    anime::playback_class_label(after_restart.anime_registry().playback_of(pid)) +
                    "; episode 2 in Continue Watching: " + (listed ? "yes" : "no"));
    EXPECT_TRUE(still && listed);

    // 11. Resume: the provider's home > the series > Continue.
    after_restart.push(make_provider_home_screen(after_restart, pid));
    h.until(
        [&]
        {
            const anime::ProviderState *s = after_restart.anime_registry().state(pid);
            return s && (s->loaded || !s->error.empty());
        },
        180);
    h.settled(180);
    ASSERT_TRUE(h.focus_on(pid + "/" + sid));
    h.press(input::Button::cross);
    h.until(
        [&]
        {
            const anime::Series *s = after_restart.anime_registry().find_series(pid, sid);
            return s && !s->details_pending;
        },
        180);
    h.settled(180);
    h.shot("06-series-continue");
    h.press(input::Button::up, 6);
    h.press(input::Button::cross);
    const bool resumed_play =
        h.until([&] { return h.status().state == media::PlayerState::playing; }, 90) &&
        h.until([&] { return h.status().position > 0.0; }, 60);
    const double resumed_at = h.status().position;
    const bool resume_ok = resumed_play && saved && resumed_at >= saved_position - 3.0;
    report.step("Resume after restart", resume_ok,
                "Continue started at " + fixed(resumed_at) + " s (saved " + fixed(saved_position) +
                    " s)");
    EXPECT_TRUE(resume_ok);
    h.shot("07-resumed");
    h.press(input::Button::circle);
    h.step(10);
}
