// AKENO STREAM PS5 - Website providers end to end, through the controller.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drives the real App offline, every test from its own empty data folder:
// Anime > Streaming Providers (only the built-in series) > Add Streaming
// Provider > the address typed on the on-screen keyboard > detection >
// Add Provider > the provider's home > a series (its seasons and episodes
// load from the site) > an episode in AKENO's native player (software
// decoder: picture and sound counted) > search on the site > the next
// catalogue page > the same address again opens the existing provider >
// restart > remove. The site is tests/fixtures/website-schema answered at
// https://anime.fixture.test; every other address is offline.
#include "anime/provider_store.hpp"
#include "anime/registry.hpp"
#include "app/app.hpp"
#include "app/screens.hpp"
#include "core/fs.hpp"
#include "core/json.hpp"
#include "fixture_site.hpp"
#include "host_web_view.hpp"
#include "net/http.hpp"
#include "platform/platform.hpp"
#include "software_sink.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>

using namespace akeno;

namespace
{
std::string source_root()
{
    const char *root = std::getenv("AKENO_SOURCE_ROOT");
    return root ? root : ".";
}

std::string fresh_dir()
{
    char path[] = "/tmp/akeno-provider-e2e-XXXXXX";
    return mkdtemp(path) ? path : "/tmp";
}

class Harness
{
  public:
    Harness() : dir_{fresh_dir()}, site_{test::make_fixture_site(source_root())}
    {
        setenv("AKENO_DATA_DIR", dir_.c_str(), 1);
        test::reset_web_view_script();
        test::clear_fixture_site_requests();
        net::set_test_transport(
            [site = site_](const net::Request &request)
            {
                if (auto r = test::serve_fixture_site(site, request))
                    return *r;
                net::Response offline;
                offline.outcome = net::Outcome::network_error;
                offline.error = "offline";
                return offline;
            });
        start();
    }
    ~Harness()
    {
        stop();
        net::set_test_transport({});
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
    const std::string &dir() const
    {
        return dir_;
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
    bool settled()
    {
        return until([&] { return app_->jobs().pending() == 0; }) && (step(4), true);
    }
    // Types on the on-screen keyboard like a person with the D-pad (it opens
    // on the "qwertyuiop" row, first key), then Options to finish.
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
    [[nodiscard]] bool requested(const std::string &path) const
    {
        const auto list = test::fixture_site_requests();
        return std::find(list.begin(), list.end(), path) != list.end();
    }

  private:
    std::string dir_;
    std::string site_;
    gfx::FontEngine fonts_;
    std::vector<gfx::Pixel> pixels_ = std::vector<gfx::Pixel>(1920 * 1080);
    gfx::Surface surface_{pixels_.data(), 1920, 1080, 1920};
    std::uint64_t now_ = 1000;
    std::shared_ptr<test::SinkRecord> record_ = std::make_shared<test::SinkRecord>();
    std::unique_ptr<App> app_;
};

constexpr char kProvider[] = "web_open-anime-fixture";
constexpr char kSignal[] = "anime-the-signal-html";

// Streaming Providers > Add Streaming Provider > the address on the keyboard.
void add_by_typing(Harness &h, const std::string &host)
{
    h.app().push(make_anime_providers_screen(h.app()));
    h.step(3);
    ASSERT_EQ(h.top(), "providers");
    h.press(input::Button::cross); // the first row: Add Streaming Provider
    ASSERT_EQ(h.top(), "add-provider");
    h.press(input::Button::cross); // the address field
    ASSERT_TRUE(h.app().keyboard_active());
    h.type(host); // after the "https://" the field starts with
    ASSERT_FALSE(h.app().keyboard_active());
}
} // namespace

TEST(ProviderEndToEnd, AddBrowsePlaySearchMoreDuplicateRestartRemove)
{
    Harness h;
    App &app = h.app();
    // An empty provider list: only the bundled series, no provider files.
    ASSERT_EQ(app.anime_registry().configs().size(), 1u);
    EXPECT_TRUE(app.anime_registry().configs()[0].builtin);
    EXPECT_FALSE(fs::exists(fs::join(h.dir(), anime::kWebsiteProvidersFile)));

    // Add: type the homepage; detection runs by itself.
    add_by_typing(h, "anime.fixture.test");
    ASSERT_TRUE(h.settled());
    EXPECT_EQ(h.top(), "add-provider");
    EXPECT_TRUE(h.requested("/"));                      // the homepage
    EXPECT_TRUE(h.requested("/anime/the-signal.html")); // a series opened while checking
    // Cross is now on Add Provider: the provider's home opens.
    h.press(input::Button::cross);
    ASSERT_EQ(h.top(), std::string{"provider-home:"} + kProvider);
    const anime::ProviderConfig *config = app.anime_registry().config(kProvider);
    ASSERT_NE(config, nullptr);
    EXPECT_EQ(config->type, anime::ProviderType::website);
    EXPECT_EQ(config->adapter, "schema-org");
    EXPECT_EQ(config->address, "https://anime.fixture.test/");
    EXPECT_TRUE(fs::exists(fs::join(h.dir(), anime::kWebsiteProvidersFile)));
    EXPECT_FALSE(fs::exists(fs::join(h.dir(), anime::kProvidersFile))); // 1.0.0's file untouched

    // The catalogue, from the site.
    ASSERT_TRUE(h.until(
        [&]
        {
            const anime::ProviderState *s = app.anime_registry().state(kProvider);
            return s && s->loaded;
        }));
    h.settled();
    EXPECT_EQ(app.anime_registry().state(kProvider)->series.size(), 4u);
    // Down to the catalogue, Cross on the first series.
    h.press(input::Button::down);
    h.press(input::Button::cross);
    ASSERT_EQ(h.top(), std::string{"series:"} + kProvider + "/" + kSignal);
    // Its seasons and episodes load from its page (and season 2's page).
    ASSERT_TRUE(h.until(
        [&]
        {
            const anime::Series *s = app.anime_registry().find_series(kProvider, kSignal);
            return s && !s->details_pending;
        }));
    h.settled();
    const anime::Series *signal = app.anime_registry().find_series(kProvider, kSignal);
    ASSERT_EQ(signal->seasons.size(), 2u);
    EXPECT_EQ(signal->seasons[1].episodes.size(), 2u);
    EXPECT_EQ(signal->seasons[0].episodes[0].availability, anime::Availability::playable);

    // Seasons, episodes, the first episode: AKENO's native player.
    h.press(input::Button::down);
    h.press(input::Button::down);
    h.press(input::Button::cross);
    ASSERT_TRUE(
        h.until([&] { return app.player().status().state == media::PlayerState::playing; }, 15))
        << media::state_name(app.player().status().state) << " " << app.player().status().error;
    ASSERT_TRUE(h.until([&] { return h.record().frames > 10 && h.record().audio_units > 10; }));
    EXPECT_EQ(h.record().width, 640u);
    EXPECT_TRUE(h.requested("/media/h264-aac-360p.mp4"));
    // Stop: back to the series, then the provider's home.
    h.press(input::Button::circle);
    h.step(10);
    ASSERT_EQ(h.top(), std::string{"series:"} + kProvider + "/" + kSignal);
    h.press(input::Button::circle);
    ASSERT_EQ(h.top(), std::string{"provider-home:"} + kProvider);

    // Search on the site (its SearchAction).
    h.press(input::Button::triangle);
    ASSERT_TRUE(app.keyboard_active());
    h.type("signal");
    ASSERT_TRUE(h.settled());
    EXPECT_TRUE(h.requested("/search.html?q=signal"));

    // Load More: the next catalogue page joins the list.
    h.press(input::Button::up, 4); // the action buttons
    h.press(input::Button::right); // Search > Load More
    h.press(input::Button::cross);
    ASSERT_TRUE(
        h.until([&] { return app.anime_registry().state(kProvider)->series.size() == 5u; }));
    EXPECT_TRUE(app.anime_registry().state(kProvider)->next_page.empty());

    // The same site again: its home opens, nothing is added twice.
    h.press(input::Button::circle);
    add_by_typing(h, "anime.fixture.test");
    h.step(5);
    EXPECT_EQ(h.top(), std::string{"provider-home:"} + kProvider);
    EXPECT_EQ(app.anime_registry().configs().size(), 2u);

    // Restart: the provider is still there, as it was saved.
    h.restart();
    App &again = h.app();
    const anime::ProviderConfig *kept = again.anime_registry().config(kProvider);
    ASSERT_NE(kept, nullptr);
    EXPECT_EQ(kept->type, anime::ProviderType::website);
    EXPECT_EQ(kept->settings.at("search"), "https://anime.fixture.test/search.html?q=AKENOQUERY");
    EXPECT_TRUE(again.anime_registry().storage_problems().empty());

    // Remove it from its home (Square, then confirm).
    again.push(make_provider_home_screen(again, kProvider));
    h.step(3);
    h.press(input::Button::square);
    h.press(input::Button::cross);
    h.step(3);
    EXPECT_EQ(again.anime_registry().config(kProvider), nullptr);
    const auto saved =
        json::parse(fs::read_text(fs::join(h.dir(), anime::kWebsiteProvidersFile)).value_or(""));
    ASSERT_TRUE(saved.ok);
    EXPECT_EQ(saved.value["providers"].size(), 0u);
    EXPECT_FALSE(fs::exists(fs::join(h.dir(), anime::kProvidersFile)));
}

TEST(ProviderEndToEnd, AWebPageWithoutAnAdapterIsNotAdded)
{
    Harness h;
    App &app = h.app();
    app.push(make_add_provider_screen(app, "https://anime.fixture.test/plain.html"));
    ASSERT_TRUE(h.settled());
    ASSERT_EQ(h.top(), "add-provider");
    // Add Provider stays disabled: nothing is saved.
    h.press(input::Button::down, 4);
    h.press(input::Button::cross);
    h.step(3);
    EXPECT_EQ(h.top(), "add-provider");
    EXPECT_EQ(app.anime_registry().configs().size(), 1u);
    EXPECT_FALSE(fs::exists(fs::join(h.dir(), anime::kWebsiteProvidersFile)));
}

TEST(ProviderEndToEnd, AnAddressThatIsAlreadyAProviderOpensItsHome)
{
    Harness h;
    App &app = h.app();
    anime::ProviderConfig c;
    ASSERT_TRUE(anime::check_source("https://anime.fixture.test/", "", {}, {}, &c).importable);
    std::string why, added;
    ASSERT_TRUE(app.anime_registry().add(c, &why, nullptr, &added)) << why;
    // From the Websites check or anywhere else that hands over an address.
    app.push(make_add_provider_screen(app, "http://www.anime.fixture.test"));
    h.step(3);
    EXPECT_EQ(h.top(), "provider-home:" + added);
}
