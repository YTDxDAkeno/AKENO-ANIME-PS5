// AKENO STREAM PS5 - Anime end to end: search, series, season, episode, native player.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drives the real App with controller input, offline, on the sample series
// that ships in assets/anime/sample: Anime -> search "signal" (typed on the
// on-screen keyboard) -> the series -> season 2 and back -> S1 E3 -> Play
// in AKENO's native player (software decoder: picture and sound counted) ->
// the episode ends -> Up next -> S2 E1 (the next season) -> seek, stop ->
// Continue resumes where it stopped. The console's hardware decoder is the
// one thing this cannot show; the hardware checklist does.
#include "anime/registry.hpp"
#include "anime/session.hpp"
#include "app/app.hpp"
#include "app/screens.hpp"
#include "core/fs.hpp"
#include "host_web_view.hpp"
#include "net/http.hpp"
#include "platform/platform.hpp"
#include "software_sink.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>

using namespace akeno;

namespace
{
std::string fresh_dir()
{
    char path[] = "/tmp/akeno-anime-e2e-XXXXXX";
    return mkdtemp(path) ? path : "/tmp";
}

class Harness
{
  public:
    Harness() : dir_{fresh_dir()}
    {
        setenv("AKENO_DATA_DIR", dir_.c_str(), 1);
        test::reset_web_view_script();
        // Offline: AniList and every other service fail; the sample series is local.
        net::set_test_transport(
            [](const net::Request &)
            {
                net::Response r;
                r.outcome = net::Outcome::network_error;
                r.error = "offline";
                return r;
            });
        AppConfig config;
        config.make_sink = [this] { return test::make_software_sink(app_->frames(), record_); };
        app_ = std::make_unique<App>(config, fonts_);
        app_->start(now_);
    }
    ~Harness()
    {
        app_->player().stop();
        app_.reset();
        net::set_test_transport({});
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
    // Steps until done() holds or about `seconds` of real time pass.
    bool until(const std::function<bool()> &done, int seconds = 10)
    {
        for (int i = 0; i < seconds * 100; ++i)
        {
            if (done())
                return true;
            step();
            platform::sleep_us(10000);
        }
        return done();
    }
    // Types lower-case letters on the on-screen keyboard (it opens on the
    // "qwertyuiop" row, first key) like a person with the D-pad would.
    void type(const std::string &text)
    {
        static const char *rows[] = {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm_.@"};
        int row = 1, col = 0;
        for (const char c : text)
        {
            int r = 0, k = 0;
            for (r = 0; r < 4; ++r)
            {
                const char *at = std::strchr(rows[r], c);
                if (at)
                {
                    k = static_cast<int>(at - rows[r]);
                    break;
                }
            }
            ASSERT_LT(r, 4) << c;
            while (row < r)
                press(input::Button::down), ++row;
            while (row > r)
                press(input::Button::up), --row;
            while (col < k)
                press(input::Button::right), ++col;
            while (col > k)
                press(input::Button::left), --col;
            press(input::Button::cross);
        }
        press(input::Button::options); // done
    }
    [[nodiscard]] media::PlayerStatus status() const
    {
        return app_->player().status();
    }
    // The newest anime episode in the watch history.
    [[nodiscard]] const HistoryEntry *last_episode() const
    {
        for (const HistoryEntry &h : app_->store().history())
            if (h.item.provider == anime::kEpisodeItemProvider)
                return &h;
        return nullptr;
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

constexpr char kSeries[] = "signal-lights";
} // namespace

TEST(AnimeEndToEnd, SearchSeasonEpisodePlayNextAndResume)
{
    Harness h;
    App &app = h.app();
    app.switch_mode(Mode::anime);
    // The sample series loads from the install folder and its episodes are
    // checked (local MP4 files): it becomes Available to Watch.
    ASSERT_TRUE(h.until([&] { return app.anime_registry().available().size() == 1; }));
    const anime::Series *series =
        app.anime_registry().find_series(anime::Registry::kSampleId, kSeries);
    ASSERT_NE(series, nullptr);
    ASSERT_EQ(series->seasons.size(), 2u);
    for (const anime::Season &season : series->seasons)
        for (const anime::Episode &e : season.episodes)
            EXPECT_EQ(e.availability, anime::Availability::playable)
                << e.id << ": " << e.availability_detail;

    // Search: Triangle in Anime, Triangle to type, "signal", Options to finish.
    h.press(input::Button::triangle);
    h.press(input::Button::triangle);
    ASSERT_TRUE(app.keyboard_active());
    h.type("signal");
    ASSERT_FALSE(app.keyboard_active());
    ASSERT_TRUE(h.until([&] { return app.jobs().pending() == 0; }));
    h.step(5);
    EXPECT_EQ(app.anime_library().searches().front(), "signal");
    // Down to the results; the first is the series ("Available to Watch").
    h.press(input::Button::down, 2);
    h.press(input::Button::cross);
    h.step(5);

    // The series: seasons (down), season 2 (right) and back to 1 (left),
    // the episodes (down), the third (down twice), Play.
    h.press(input::Button::down);
    h.press(input::Button::right);
    h.press(input::Button::left);
    h.press(input::Button::down);
    h.press(input::Button::down, 2);
    h.press(input::Button::cross);
    ASSERT_TRUE(h.until([&] { return h.status().state == media::PlayerState::playing; }))
        << media::state_name(h.status().state) << " " << h.status().error;
    // Picture and sound from the episode, through the native player.
    ASSERT_TRUE(h.until([&] { return h.record().frames > 10 && h.record().audio_units > 10; }));
    EXPECT_EQ(h.record().width, 640u);
    // ... which verifies the provider's native playback on this device.
    ASSERT_TRUE(h.until(
        [&]
        { return app.anime_registry().native_playback(anime::Registry::kSampleId) != nullptr; }));
    EXPECT_EQ(app.anime_registry().playback_of(anime::Registry::kSampleId),
              anime::PlaybackClass::verified);
    EXPECT_GE(app.anime_registry().native_playback(anime::Registry::kSampleId)->frames, 30u);

    // Near the end: the episode finishes, "Up next" offers S2 E1, Cross plays it.
    h.press(input::Button::right); // +10 s
    ASSERT_TRUE(h.until([&] { return h.status().state == media::PlayerState::ended; }, 15));
    h.step(3); // the app records the finished episode on its next update
    const HistoryEntry *finished = h.last_episode();
    ASSERT_NE(finished, nullptr);
    EXPECT_EQ(finished->item.id, "akeno-sample/signal-lights/s1/e3");
    EXPECT_TRUE(finished->finished());
    h.press(input::Button::cross);
    ASSERT_TRUE(h.until(
        [&]
        {
            const media::PlayerStatus s = h.status();
            return s.state == media::PlayerState::playing && s.position > 0.5;
        },
        15));

    // Seek into S2 E1 and stop: the position is kept for this episode.
    h.press(input::Button::right); // +10 s
    ASSERT_TRUE(h.until([&] { return h.status().position >= 10.5; }, 15));
    h.press(input::Button::circle);
    h.step(10);
    const HistoryEntry *stopped = h.last_episode();
    ASSERT_NE(stopped, nullptr);
    EXPECT_EQ(stopped->item.id, "akeno-sample/signal-lights/s2/e1");
    EXPECT_GE(stopped->position, 10.0);
    EXPECT_FALSE(stopped->finished());
    // Continue Watching knows it (also on Home).
    bool listed = false;
    for (const HistoryEntry &e : app.store().continue_watching())
        listed = listed || e.item.id == "akeno-sample/signal-lights/s2/e1";
    EXPECT_TRUE(listed);

    // Back on the series: up to the actions, "Continue S2 E1" resumes there.
    h.press(input::Button::up, 6);
    h.press(input::Button::cross);
    ASSERT_TRUE(h.until([&] { return h.status().state == media::PlayerState::playing; }, 15));
    ASSERT_TRUE(h.until([&] { return h.status().position > 0.0; }, 10));
    EXPECT_GE(h.status().position, 9.0) << "resumed at " << h.status().position;
    h.press(input::Button::circle);
    h.step(5);
}

TEST(AnimeEndToEnd, CatalogueOnlyTitlesHaveNoPlayButton)
{
    Harness h;
    App &app = h.app();
    // An AniList title no provider maps: the series screen offers no Play
    // and says why (no network here, so only the card is known).
    MediaItem title;
    title.provider = "anilist";
    title.id = "99999";
    title.title = "Some Licensed Show";
    title.kind = ItemKind::series;
    app.open_item(title);
    h.step(20);
    h.press(input::Button::cross); // the first action: Add to Watchlist (no Play)
    EXPECT_EQ(app.anime_library().watchlist().size(), 1u);
    EXPECT_EQ(app.anime_library().watchlist()[0].anilist_id, 99999);
    // The provider's episodes would be needed to play: none is mapped.
    EXPECT_TRUE(app.anime_registry().series_for_anilist(99999).empty());
}
