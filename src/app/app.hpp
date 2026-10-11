// AKENO STREAM PS5 - Application shell: modes, screens, overlays.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Portable: the console entry point (src/main.cpp) and the host screenshot
// tool both drive it with input events and render it into a 1920x1080 surface.
#pragma once

#include "anime/library.hpp"
#include "anime/registry.hpp"
#include "app/diagnostics.hpp"
#include "app/input.hpp"
#include "app/store.hpp"
#include "app/version.hpp"
#include "core/jobs.hpp"
#include "gfx/font.hpp"
#include "media/player.hpp"
#include "platform/web_view.hpp"
#include "providers/anilist.hpp"
#include "providers/crunchyroll.hpp"
#include "providers/discover.hpp"
#include "providers/open_catalog.hpp"
#include "providers/youtube.hpp"
#include "ui/image_cache.hpp"
#include "ui/keyboard.hpp"
#include "ui/painter.hpp"
#include "web/address.hpp"
#include "web/local_pages.hpp"
#include "web/playback_check.hpp"
#include "web/web_tests.hpp"
#include "web/websites.hpp"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace akeno
{
enum class Mode : std::uint8_t
{
    home,
    anime,
    youtube,
    websites,
    discover,
    library,
    sources,
    settings,
    site, // a website the user made a mode (its own tab after Websites)
};
inline constexpr int kBuiltInModes = 8;              // the fixed modes (not Mode::site)
inline constexpr int kModeCount = kBuiltInModes + 1; // screen stacks, Mode::site included
const char *mode_name(Mode mode) noexcept;
const char *mode_id(Mode mode) noexcept;
ui::Pixel mode_accent(Mode mode) noexcept;

// One tab of the mode bar, in order: Home, YouTube, Anime, Websites, the
// website modes, Discover, Library, Sources, Settings.
struct ModeTab
{
    Mode mode = Mode::home;
    std::string site_id; // Mode::site: the website
    std::string label;
    ui::Pixel accent{};
};

class App;

class Screen
{
  public:
    explicit Screen(App &app) : app_{app}
    {
    }
    virtual ~Screen() = default;
    virtual void handle(input::Button button) = 0;
    virtual void update(std::uint64_t now_ms)
    {
        (void)now_ms;
    }
    virtual void render(ui::Painter &p, std::uint64_t now_ms) = 0;
    // Requests continuous redraws (spinners, video, animations).
    [[nodiscard]] virtual bool animating() const
    {
        return false;
    }
    // Full-screen screens hide the mode bar (player).
    [[nodiscard]] virtual bool full_screen() const
    {
        return false;
    }
    // Modal screens (QR codes, status sheets) float above every mode.
    [[nodiscard]] virtual bool modal() const
    {
        return false;
    }
    // Hint bar entries for this screen.
    struct Hint
    {
        ui::Glyph glyph;
        std::string label;
    };
    [[nodiscard]] virtual std::vector<Hint> hints() const
    {
        return {};
    }
    // Called when the screen becomes visible again (after a pop).
    virtual void resumed()
    {
    }
    // What the screen shows, for diagnostics and tests ("provider-home:web_x");
    // empty for screens that do not say.
    [[nodiscard]] virtual std::string screen_id() const
    {
        return {};
    }
    // The id of the item that has the focus, for tests ("web_x/series-id");
    // empty for screens that do not say.
    [[nodiscard]] virtual std::string focus_id() const
    {
        return {};
    }
    // Asks the app to remove this screen once the current update is done
    // (a screen cannot pop itself from inside update()).
    void close_later() noexcept
    {
        closing_ = true;
    }
    [[nodiscard]] bool closing() const noexcept
    {
        return closing_;
    }

  protected:
    App &app_;

  private:
    bool closing_ = false;
};

struct AppConfig
{
    std::string version = kAppVersion;
    std::string build = "dev";
    media::SinkFactory make_sink;
    std::function<void(const std::string &)> log;
    // Applies the playback volume (0..100) to the audio output.
    std::function<void(int)> set_volume;
};

// What the player knows beyond the media address: sidecar subtitles and,
// for an anime episode, the episodes before and after it.
struct PlayerContext
{
    std::vector<anime::SubtitleTrack> subtitles;
    std::string next_label;     // "S1 E4 - Title"; empty: no next episode
    std::string previous_label; // empty: no previous episode
    std::function<void()> play_next;
    std::function<void()> play_previous;
};

// One visit to the embedded browser (platform/web_view.hpp).
struct WebSession
{
    enum class Kind : std::uint8_t
    {
        website,         // a site the user chose: opened as is, nothing added to it
        youtube,         // AKENO STREAM's page with the official YouTube player
        capability_test, // AKENO STREAM's browser capability test page
        video,           // AKENO STREAM's video-only player: one official embed
    };
    Kind kind = Kind::website;
    std::string url;     // Kind::website: the address
    std::string title;   // shown while it opens
    std::string site_id; // the saved website, if it is one
    web::YouTubeTarget youtube;
    web::EmbedTarget embed;  // Kind::video: the video and where to start
    MediaItem item;          // Kind::video: progress is saved under its key (empty id: not saved)
    bool check_first = true; // look at the site before opening (Settings)
    // Runs on the next frame after the browser has closed (to ask what the
    // user saw, for example).
    std::function<void()> after;
};

class App final
{
  public:
    App(AppConfig config, gfx::FontEngine &fonts);
    ~App();

    void start(std::uint64_t now_ms);
    void handle(const input::Event &event);
    void update(std::uint64_t now_ms);
    void render(gfx::Surface &surface, std::uint64_t now_ms);
    // True when the next frame differs from the last one presented.
    [[nodiscard]] bool needs_redraw() const;
    [[nodiscard]] bool keyboard_active() const noexcept
    {
        return keyboard_.active();
    }
    void mark_dirty()
    {
        dirty_ = true;
    }

    // Navigation
    void switch_mode(Mode mode);
    [[nodiscard]] Mode mode() const noexcept
    {
        return mode_;
    }
    // Website modes: open one as the current tab; call after a website's
    // mode, name or icon changed (a removed mode returns to Websites).
    void open_site_mode(const std::string &site_id);
    void site_modes_changed();
    [[nodiscard]] const std::string &site_mode() const noexcept
    {
        return site_mode_;
    }
    [[nodiscard]] std::vector<ModeTab> tabs() const;
    // The accent of the tab on screen (a website mode has its own).
    [[nodiscard]] ui::Pixel accent() const;
    void push(std::unique_ptr<Screen> screen);
    void pop();
    // True when this screen is the one on top (it may replace itself).
    [[nodiscard]] bool showing(const Screen *screen) const;
    // The screen_id() of the screen on top.
    [[nodiscard]] std::string top_screen_id() const;
    [[nodiscard]] std::string top_focus_id() const;
    // The newest message on screen (empty: none), for tests.
    [[nodiscard]] std::string latest_toast() const
    {
        return toasts_.empty() ? std::string{} : toasts_.back().text;
    }
    void open_item(const MediaItem &item);
    void play(const MediaItem &item, double start_seconds = -1.0,
              std::shared_ptr<PlayerContext> context = nullptr);
    // Anime: loads the episode providers that are not loaded yet (all of
    // them with force) in the background; anime_generation() changes when
    // one finished.
    void refresh_anime(bool force);
    // The same for one provider (only: its id; empty: all of them).
    void refresh_anime_provider(bool force, const std::string &only);
    [[nodiscard]] std::uint64_t anime_generation() const noexcept
    {
        return anime_generation_;
    }
    [[nodiscard]] bool anime_loading() const noexcept
    {
        return !anime_loading_.empty();
    }
    [[nodiscard]] bool anime_provider_loading(const std::string &provider_id) const
    {
        return anime_loading_.count(provider_id) > 0;
    }
    // Website providers: loads a catalogue entry's seasons and episodes and
    // checks a few of its episodes in the background; anime_generation()
    // changes when it is done, anime_series_error() says why it failed.
    void load_anime_series(const std::string &provider_id, const std::string &series_id);
    [[nodiscard]] bool anime_series_loading(const std::string &provider_id,
                                            const std::string &series_id) const;
    [[nodiscard]] std::string anime_series_error(const std::string &provider_id,
                                                 const std::string &series_id) const;
    // Website providers: the next page of the site's catalogue.
    void load_more_anime(const std::string &provider_id);
    [[nodiscard]] bool anime_more_loading(const std::string &provider_id) const
    {
        return anime_more_loading_.count(provider_id) > 0;
    }
    // Resolves an episode with its provider, checks it and plays it in the
    // native player with its subtitles and the next episode; explains why
    // when it cannot play. An episode with only an official embed, or one
    // whose format AKENO's player cannot play while its service offers an
    // embed, plays on the video-only page instead (anime/playback_route.hpp).
    // start < 0: resume from history.
    void play_episode(const std::string &provider_id, const std::string &series_id,
                      anime::EpisodeRef ref, double start = -1.0);
    // The same episode in its service's official embedded player on the
    // video-only page (anime/playback_route.hpp), sharing its saved position
    // with AKENO's player. False (and a message) when it has none.
    // target: the embed to use (default: the episode's own).
    bool play_episode_embedded(const std::string &provider_id, const std::string &series_id,
                               anime::EpisodeRef ref, double start = -1.0,
                               std::optional<web::EmbedTarget> target = std::nullopt);
    void show_qr(const MediaItem &item);
    void show_provider_status(const Provider &provider);
    // The embedded browser: websites, the official YouTube player and the
    // browser capability test open inside AKENO STREAM (screen_browser.cpp).
    void open_web(WebSession session);
    // Address bar: an address opens, other text is searched for.
    void open_address(const std::string &typed);
    void play_youtube(const web::YouTubeTarget &target, const std::string &title);
    // The video-only player: one video in its service's official embedded
    // player (web/embed.hpp), progress saved under item's key (an anime
    // episode's, so native and embedded playback share it). start < 0
    // resumes from history, else from the target's own start.
    void play_embed(web::EmbedTarget target, const std::string &title, const MediaItem &item = {},
                    double start = -1.0);
    // Asks for a YouTube or PeerTube video address and plays it there.
    void ask_embed_link();
    // Asks for a YouTube link or video ID and plays it in the official player.
    void ask_youtube_link();
    // youtube.com in the embedded browser.
    void open_youtube_site();
    void run_browser_test();
    // A DRM-free video address played in AKENO STREAM's own player (HLS,
    // MPEG-TS, MP4, MKV); asks for the address with the keyboard.
    void ask_play_link();
    void play_link(const std::string &address);
    [[nodiscard]] bool browser_active() const noexcept
    {
        return browser_active_;
    }
    void set_browser_active(bool active) noexcept
    {
        browser_active_ = active;
    }
    // Hand-off to the official YouTube app (experimental: firmware-dependent).
    void open_youtube_app();
    void toast(const std::string &message, ui::Pixel color = ui::theme::kInfo);
    void report_error(const std::string &where, const std::string &message);
    void open_keyboard(std::string title, std::string initial, std::size_t max_length, bool secret,
                       std::function<void(bool, const std::string &)> done);

    // Services
    Jobs &jobs()
    {
        return jobs_;
    }
    Store &store()
    {
        return store_;
    }
    ui::ImageCache &images()
    {
        return images_;
    }
    gfx::FontEngine &fonts()
    {
        return fonts_;
    }
    media::Player &player()
    {
        return *player_;
    }
    const std::shared_ptr<media::FrameStore> &frames()
    {
        return frames_;
    }
    OpenCatalog &open_catalog()
    {
        return open_;
    }
    AniList &anilist()
    {
        return anilist_;
    }
    YouTube &youtube()
    {
        return youtube_;
    }
    Crunchyroll &crunchyroll()
    {
        return crunchyroll_;
    }
    PeerTube &peertube()
    {
        return peertube_;
    }
    InternetArchive &archive()
    {
        return archive_;
    }
    Discover &discover()
    {
        return discover_;
    }
    Diagnostics &diagnostics()
    {
        return diagnostics_;
    }
    platform::WebView &web_view()
    {
        return *web_;
    }
    web::WebsiteStore &websites()
    {
        return websites_;
    }
    web::WebTestLog &web_tests()
    {
        return web_tests_;
    }
    web::LocalPages &local_pages()
    {
        return pages_;
    }
    web::PlaybackLog &playback_checks()
    {
        return playback_checks_;
    }
    anime::Registry &anime_registry()
    {
        return anime_;
    }
    anime::Library &anime_library()
    {
        return anime_library_;
    }
    // Backups of the data folder (app/backup.hpp): where they are looked for,
    // and what the last backup did ("10 files saved at start", or why not).
    [[nodiscard]] std::vector<std::string> backup_locations() const;
    [[nodiscard]] const std::string &backup_status() const noexcept
    {
        return backup_status_;
    }
    void back_up_now();
    [[nodiscard]] const AppConfig &config() const
    {
        return config_;
    }
    [[nodiscard]] std::uint64_t now_ms() const noexcept
    {
        return now_ms_;
    }
    void set_controller_connected(bool connected);
    [[nodiscard]] bool controller_connected() const noexcept
    {
        return controller_connected_;
    }
    void set_display_stats(std::uint64_t frames, std::uint32_t frame_us, std::uint32_t present_us);
    [[nodiscard]] DiagnosticSnapshot snapshot() const;
    void apply_settings();

  private:
    void render_chrome(ui::Painter &p, Screen &screen);
    void render_tabs(ui::Painter &p, int left, int right);
    void step_tab(int direction);
    void remember_mode();
    void render_overlays(ui::Painter &p, std::uint64_t now_ms);
    Screen &top();
    std::vector<std::unique_ptr<Screen>> &stack();
    void finish_playback_record();
    void record_hardware_test(const media::PlayerStatus &status);
    void verify_native_playback(const media::PlayerStatus &status);

    AppConfig config_;
    gfx::FontEngine &fonts_;
    Jobs jobs_;
    Store store_;
    ui::ImageCache images_;
    std::shared_ptr<media::FrameStore> frames_;
    std::unique_ptr<media::Player> player_;
    OpenCatalog open_;
    AniList anilist_;
    YouTube youtube_;
    Crunchyroll crunchyroll_;
    PeerTube peertube_;
    InternetArchive archive_;
    Discover discover_{peertube_, archive_};
    Diagnostics diagnostics_;
    std::unique_ptr<platform::WebView> web_;
    web::WebsiteStore websites_;
    web::WebTestLog web_tests_;
    web::LocalPages pages_;
    web::PlaybackLog playback_checks_;
    anime::Registry anime_;
    anime::Library anime_library_;
    std::uint64_t anime_generation_ = 0;
    std::set<std::string> anime_loading_;
    std::set<std::string> anime_series_loading_; // "provider/series"
    std::map<std::string, std::string> anime_series_errors_;
    std::set<std::string> anime_more_loading_;
    bool browser_active_ = false;
    std::string backup_status_;

    Mode mode_ = Mode::home;
    std::string site_mode_; // the website shown in Mode::site
    std::array<std::vector<std::unique_ptr<Screen>>, kModeCount> stacks_;
    std::vector<std::unique_ptr<Screen>> overlay_; // player, QR, status: above every mode
    ui::Keyboard keyboard_;
    std::function<void(bool, const std::string &)> keyboard_done_;

    struct Toast
    {
        std::string text;
        ui::Pixel color;
        std::uint64_t until_ms;
    };
    std::vector<Toast> toasts_;
    bool dirty_ = true;
    std::uint64_t now_ms_ = 0;
    std::uint64_t mode_switched_ms_ = 0;
    bool controller_connected_ = true;
    std::uint64_t display_frames_ = 0;
    std::uint32_t frame_us_ = 0, present_us_ = 0;

    // Playback bookkeeping for resume.
    MediaItem playing_;
    bool playing_active_ = false;
    bool playing_verified_ = false; // this episode already verified its provider
    std::uint64_t last_progress_save_ms_ = 0;
};
} // namespace akeno
