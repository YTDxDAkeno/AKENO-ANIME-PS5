// AKENO STREAM PS5 - Application shell: modes, screens, overlays.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/app.hpp"

#include "app/screens.hpp"
#include "core/fs.hpp"
#include "net/http.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace akeno
{
namespace th = ui::theme;

namespace
{
// Static texts for the crash reporter's stage.
const char *mode_stage(Mode mode) noexcept
{
    switch (mode)
    {
    case Mode::home:
        return "Home";
    case Mode::anime:
        return "Anime mode";
    case Mode::youtube:
        return "YouTube mode";
    case Mode::library:
        return "Library";
    case Mode::settings:
        return "Settings";
    }
    return "browsing";
}
} // namespace

const char *mode_name(Mode mode) noexcept
{
    switch (mode)
    {
    case Mode::home:
        return "Home";
    case Mode::anime:
        return "Anime";
    case Mode::youtube:
        return "YouTube";
    case Mode::library:
        return "Library";
    case Mode::settings:
        return "Settings";
    }
    return "";
}

const char *mode_id(Mode mode) noexcept
{
    switch (mode)
    {
    case Mode::home:
        return "home";
    case Mode::anime:
        return "anime";
    case Mode::youtube:
        return "youtube";
    case Mode::library:
        return "library";
    case Mode::settings:
        return "settings";
    }
    return "home";
}

ui::Pixel mode_accent(Mode mode) noexcept
{
    switch (mode)
    {
    case Mode::home:
        return th::kAccentHome;
    case Mode::anime:
        return th::kAccentAnime;
    case Mode::youtube:
        return th::kAccentYouTube;
    case Mode::library:
        return th::kAccentLibrary;
    case Mode::settings:
        return th::kAccentSettings;
    }
    return th::kAccentHome;
}

App::App(AppConfig config, gfx::FontEngine &fonts)
    : config_{std::move(config)}, fonts_{fonts}, jobs_{3}, store_{platform::data_dir()},
      images_{jobs_}, frames_{std::make_shared<media::FrameStore>(th::kWidth, th::kHeight)},
      open_{platform::app_dir(), platform::data_dir()},
      youtube_{[this] { return store_.youtube_api_key(); }}
{
    media::PlayerConfig pc;
    pc.frames = frames_;
    pc.make_sink = config_.make_sink;
    player_ = std::make_unique<media::Player>(pc);
}

App::~App()
{
    if (player_)
        player_->stop();
    jobs_.shutdown();
}

void App::start(std::uint64_t now_ms)
{
    now_ms_ = now_ms;
    store_.load();
    if (!store_.last_error().empty())
        report_error("storage", store_.last_error());
    // A YouTube key file in the install folder is picked up at every start.
    if (auto text = fs::read_text(fs::join(platform::app_dir(), "youtube-key.txt"), 4096))
    {
        const std::string key = extract_youtube_key(*text);
        if (key.empty())
            toast("youtube-key.txt does not contain a YouTube API key", th::kWarning);
        else if (key != store_.youtube_api_key())
        {
            store_.set_youtube_api_key(key);
            toast("YouTube API key imported - delete youtube-key.txt from the install folder",
                  th::kSuccess);
        }
    }
    // A crash report left by the previous run (platform crash reporter):
    // show it once, keep it as crash-previous.txt for the next report.
    const std::string crash_path = fs::join(platform::data_dir(), "crash.txt");
    if (auto text = fs::read_text(crash_path, 8192))
    {
        std::string first = text->substr(0, text->find('\n'));
        report_error("previous run", first);
        toast("The last session ended with a crash - details in Settings > Diagnostics",
              th::kWarning);
        (void)fs::write_atomic(fs::join(platform::data_dir(), "crash-previous.txt"), *text);
        (void)fs::remove_file(crash_path);
    }
    apply_settings();
    stacks_[static_cast<int>(Mode::home)].push_back(make_home_screen(*this));
    stacks_[static_cast<int>(Mode::anime)].push_back(make_anime_screen(*this));
    stacks_[static_cast<int>(Mode::youtube)].push_back(make_youtube_screen(*this));
    stacks_[static_cast<int>(Mode::library)].push_back(make_library_screen(*this));
    stacks_[static_cast<int>(Mode::settings)].push_back(make_settings_screen(*this));
    const std::string last = store_.settings().last_mode;
    for (int m = 0; m < kModeCount; ++m)
        if (last == mode_id(static_cast<Mode>(m)))
            mode_ = static_cast<Mode>(m);
    dirty_ = true;
}

void App::apply_settings()
{
    const Settings &s = store_.settings();
    anilist_.set_include_adult(s.show_adult_anime);
    if (config_.set_volume)
        config_.set_volume(s.volume);
    youtube_.set_region(s.youtube_region);
    youtube_.set_safe_search(s.youtube_safe_search);
}

std::vector<std::unique_ptr<Screen>> &App::stack()
{
    return stacks_[static_cast<int>(mode_)];
}

Screen &App::top()
{
    if (!overlay_.empty())
        return *overlay_.back();
    return *stack().back();
}

void App::switch_mode(Mode mode)
{
    if (mode == mode_)
        return;
    mode_ = mode;
    mode_switched_ms_ = now_ms_;
    Settings s = store_.settings();
    s.last_mode = mode_id(mode);
    store_.update_settings(s);
    stack().back()->resumed();
    dirty_ = true;
}

void App::push(std::unique_ptr<Screen> screen)
{
    if (screen->full_screen() || screen->modal())
        overlay_.push_back(std::move(screen));
    else
        stack().push_back(std::move(screen));
    dirty_ = true;
}

void App::pop()
{
    if (!overlay_.empty())
        overlay_.pop_back();
    else if (stack().size() > 1)
        stack().pop_back();
    top().resumed();
    dirty_ = true;
}

void App::toast(const std::string &message, ui::Pixel color)
{
    toasts_.push_back({message, color, now_ms_ + 3500});
    if (toasts_.size() > 3)
        toasts_.erase(toasts_.begin());
    dirty_ = true;
}

void App::report_error(const std::string &where, const std::string &message)
{
    diagnostics_.record_error(now_ms_, where, message);
    if (config_.log)
        config_.log(where + ": " + redact_secrets(message));
}

void App::open_keyboard(std::string title, std::string initial, std::size_t max_length, bool secret,
                        std::function<void(bool, const std::string &)> done)
{
    keyboard_.open(std::move(title), std::move(initial), max_length, secret);
    keyboard_done_ = std::move(done);
    dirty_ = true;
}

void App::set_controller_connected(bool connected)
{
    if (connected != controller_connected_)
        dirty_ = true;
    controller_connected_ = connected;
}

void App::set_display_stats(std::uint64_t frames, std::uint32_t frame_us, std::uint32_t present_us)
{
    display_frames_ = frames;
    frame_us_ = frame_us;
    present_us_ = present_us;
}

void App::open_item(const MediaItem &item)
{
    if (item.provider == "mode")
    {
        for (int m = 0; m < kModeCount; ++m)
            if (item.id == mode_id(static_cast<Mode>(m)))
            {
                switch_mode(static_cast<Mode>(m));
                return;
            }
        if (item.id == "open")
        {
            push(make_open_streams_screen(*this));
            return;
        }
        if (item.id == "crunchyroll")
        {
            show_provider_status(crunchyroll_);
            return;
        }
        return;
    }
    Provider *provider = nullptr;
    if (item.provider == "anilist")
        provider = &anilist_;
    else if (item.provider == "youtube")
        provider = &youtube_;
    else if (item.provider == "crunchyroll")
        provider = &crunchyroll_;
    push(make_details_screen(*this, item, provider, mode_accent(mode_)));
}

void App::play(const MediaItem &item, double start_seconds)
{
    if (!item.playable)
    {
        if (!item.external_url.empty())
            show_qr(item);
        else
            toast("This item cannot be played here.", th::kWarning);
        return;
    }
    finish_playback_record();
    if (start_seconds < 0.0)
        start_seconds =
            store_.settings().resume_playback ? store_.resume_position(item.key()) : 0.0;
    media::PlayRequest request;
    request.url = item.playable->url;
    request.kind = item.playable->kind;
    request.title = item.title;
    request.subtitle = item.subtitle;
    request.item_key = item.key();
    request.start_seconds = start_seconds;
    request.max_height = store_.settings().max_height;
    player_->play(request);
    playing_ = item;
    playing_active_ = true;
    last_progress_save_ms_ = now_ms_;
    push(make_player_screen(*this, item));
}

void App::finish_playback_record()
{
    if (!playing_active_)
        return;
    const media::PlayerStatus s = player_->status();
    if (s.state == media::PlayerState::error && s.position < 1.0)
    {
        playing_active_ = false;
        return;
    }
    if (!s.live && (s.position > 0.0 || s.state == media::PlayerState::ended))
        store_.record_progress(
            playing_, s.state == media::PlayerState::ended ? s.duration : s.position, s.duration);
    if (playing_.provider == "open" && playing_.id.rfind("bundled-", 0) == 0)
        record_hardware_test(s);
    playing_active_ = false;
}

// A bundled clip doubles as the hardware playback test in Diagnostics.
void App::record_hardware_test(const media::PlayerStatus &s)
{
    TestResult r;
    const bool ok =
        s.frames_presented > 0 && s.decoder_errors == 0 && s.state != media::PlayerState::error;
    r.state = ok ? TestResult::State::passed : TestResult::State::failed;
    char line[256];
    std::snprintf(line, sizeof(line),
                  "%s: %llu frames decoded, %llu presented, %llu dropped; audio errors %llu",
                  playing_.title.c_str(), static_cast<unsigned long long>(s.frames_decoded),
                  static_cast<unsigned long long>(s.frames_presented),
                  static_cast<unsigned long long>(s.frames_dropped),
                  static_cast<unsigned long long>(s.audio_errors));
    r.summary = line;
    r.details.push_back("Decoder: " + s.decoder);
    std::snprintf(line, sizeof(line), "Video: %s %dx%d; audio: %s; %llu underruns",
                  s.video_codec.c_str(), s.width, s.height,
                  s.audio_codec.empty() ? "none" : s.audio_codec.c_str(),
                  static_cast<unsigned long long>(s.audio_underruns));
    r.details.push_back(line);
    if (!s.error.empty())
        r.details.push_back("Error: " + s.error);
    diagnostics_.hardware_playback = std::move(r);
}

void App::show_qr(const MediaItem &item)
{
    push(make_qr_screen(*this, item));
}

void App::show_provider_status(const Provider &provider)
{
    push(make_status_screen(*this, provider.info()));
}

void App::handle(const input::Event &event)
{
    dirty_ = true;
    if (keyboard_.active())
    {
        const auto result = keyboard_.handle(event.button);
        if (result == ui::Keyboard::Result::submitted || result == ui::Keyboard::Result::cancelled)
        {
            auto done = std::move(keyboard_done_);
            keyboard_done_ = nullptr;
            if (done)
                done(result == ui::Keyboard::Result::submitted, keyboard_.text());
        }
        return;
    }
    Screen &screen = top();
    // Mode switching with L1/R1 everywhere except full-screen views.
    if (overlay_.empty() && !screen.full_screen())
    {
        if (event.button == input::Button::l1 && !event.repeat)
        {
            switch_mode(static_cast<Mode>((static_cast<int>(mode_) + kModeCount - 1) % kModeCount));
            return;
        }
        if (event.button == input::Button::r1 && !event.repeat)
        {
            switch_mode(static_cast<Mode>((static_cast<int>(mode_) + 1) % kModeCount));
            return;
        }
    }
    screen.handle(event.button);
}

void App::update(std::uint64_t now_ms)
{
    now_ms_ = now_ms;
    if (jobs_.drain())
        dirty_ = true;
    images_.tick();
    if (images_.take_changed())
        dirty_ = true;
    const auto before = toasts_.size();
    toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(),
                                 [&](const Toast &t) { return t.until_ms <= now_ms; }),
                  toasts_.end());
    if (toasts_.size() != before)
        dirty_ = true;
    // Save resume points periodically while playing.
    if (playing_active_)
    {
        const media::PlayerStatus s = player_->status();
        if (s.state == media::PlayerState::ended || s.state == media::PlayerState::error ||
            s.state == media::PlayerState::stopped)
        {
            if (s.state == media::PlayerState::error)
                report_error("playback", s.error);
            finish_playback_record();
        }
        else if (now_ms - last_progress_save_ms_ > 15000 && s.position > 10.0 && !s.live)
        {
            store_.record_progress(playing_, s.position, s.duration);
            last_progress_save_ms_ = now_ms;
        }
    }
    for (auto &screen : overlay_)
        screen->update(now_ms);
    stack().back()->update(now_ms);
    platform::set_stage(player_ && player_->active() ? "playback" : mode_stage(mode_));
}

bool App::needs_redraw() const
{
    if (dirty_ || keyboard_.active() || !toasts_.empty())
        return true;
    if (!overlay_.empty())
        return overlay_.back()->animating();
    return stacks_[static_cast<int>(mode_)].back()->animating() ||
           now_ms_ - mode_switched_ms_ < 300;
}

void App::render_chrome(ui::Painter &p, Screen &screen)
{
    const ui::Pixel accent = mode_accent(mode_);
    // Brand.
    p.s.fill_rounded({th::kMarginX, 52, 16, 16}, 8, accent);
    p.text(th::kMarginX + 28, 38, "AKENO", {40, gfx::Weight::bold}, th::kText);
    p.text(th::kMarginX + 28 + p.measure("AKENO", {40, gfx::Weight::bold}) + 10, 50, "STREAM",
           th::kCaptionStrong, th::kTextSecondary);

    // Mode tabs, centred.
    int total = 0;
    std::array<int, kModeCount> widths{};
    for (int m = 0; m < kModeCount; ++m)
    {
        widths[static_cast<std::size_t>(m)] =
            p.measure(mode_name(static_cast<Mode>(m)), th::kBodyStrong) + 56;
        total += widths[static_cast<std::size_t>(m)] + 8;
    }
    const int glyph_space = 66;
    int x = (th::kWidth - total) / 2;
    p.glyph(ui::Glyph::l1, x - glyph_space + 18, 62, 34);
    for (int m = 0; m < kModeCount; ++m)
    {
        const bool active = m == static_cast<int>(mode_);
        const int w = widths[static_cast<std::size_t>(m)];
        const ui::Rect tab{x, 36, w, 54};
        if (active)
            p.s.fill_rounded(tab, 27, th::kText);
        p.text_center(tab.x + w / 2, tab.y + (tab.h - p.line_height(th::kBodyStrong)) / 2,
                      mode_name(static_cast<Mode>(m)), th::kBodyStrong,
                      active ? th::kTextOnAccent : th::kTextSecondary);
        if (active)
            p.s.fill_rounded({tab.x + w / 2 - 14, tab.bottom() + 8, 28, 5}, 2, accent);
        x += w + 8;
    }
    p.glyph(ui::Glyph::r1, x + glyph_space - 30, 62, 34);

    // Status on the right: clock and controller.
    const std::uint64_t now = platform::wall_clock_seconds();
    if (now > 946684800)
    {
        const std::time_t t = static_cast<std::time_t>(now);
        if (const std::tm *local = std::localtime(&t))
        {
            char clock[16];
            std::snprintf(clock, sizeof(clock), "%02d:%02d", local->tm_hour, local->tm_min);
            p.text_right(th::kWidth - th::kMarginX, 46, clock, th::kSubheading, th::kTextSecondary);
        }
    }
    if (!controller_connected_)
        p.chip(th::kWidth - th::kMarginX - 330, 50, "Controller disconnected", th::kError,
               th::kText);

    // Hint bar (the keyboard shows its own hints).
    if (keyboard_.active())
        return;
    int hx = th::kMarginX;
    for (const auto &h : screen.hints())
        hx += p.hint(hx, th::kHintBarY, h.glyph, h.label) + 36;
    if (overlay_.empty())
        p.hint(th::kWidth - th::kMarginX - 230, th::kHintBarY, ui::Glyph::r1, "Switch mode");
}

void App::render_overlays(ui::Painter &p, std::uint64_t now_ms)
{
    if (keyboard_.active())
        keyboard_.render(p, mode_accent(mode_));
    int y = 150;
    for (const auto &t : toasts_)
    {
        const int w = std::min(760, p.measure(t.text, th::kBody) + 80);
        const ui::Rect r{th::kWidth - th::kMarginX - w, y, w, 64};
        p.s.fill_rounded(r, 18, gfx::with_alpha(th::kSurfaceRaised, 245));
        p.s.fill_rounded({r.x + 18, r.y + 22, 20, 20}, 10, t.color);
        p.text(r.x + 50, r.y + 17, t.text, th::kBody, th::kText, w - 70);
        y += 76;
    }
    (void)now_ms;
}

void App::render(gfx::Surface &surface, std::uint64_t now_ms)
{
    ui::Painter p{surface, fonts_};
    if (!overlay_.empty() && overlay_.front()->full_screen())
    {
        // Full-screen overlay (player): it draws everything itself; other
        // overlays (QR, status) stack on top of it.
        for (auto &screen : overlay_)
            screen->render(p, now_ms);
    }
    else
    {
        Screen &base = *stack().back();
        p.background(mode_accent(mode_));
        base.render(p, now_ms);
        for (auto &screen : overlay_)
            screen->render(p, now_ms);
        render_chrome(p, overlay_.empty() ? base : *overlay_.back());
    }
    render_overlays(p, now_ms);
    dirty_ = false;
}

DiagnosticSnapshot App::snapshot() const
{
    DiagnosticSnapshot s;
    const platform::SystemInfo info = platform::system_info();
    s.app_version = config_.version;
    s.build = config_.build;
    s.firmware = info.firmware + (info.firmware_raw.empty() ? "" : " (" + info.firmware_raw + ")");
    s.platform = info.platform;
    s.data_dir = info.data_dir;
    s.fonts = fonts_.status();
    s.curl_version = net::Client::library_version();
    s.ffmpeg = media::ffmpeg_build_info();
    const Settings &st = store_.settings();
    s.settings_summary = "quality " + std::to_string(st.max_height) + "p, volume " +
                         std::to_string(st.volume) + "%, resume " +
                         (st.resume_playback ? "on" : "off") + ", YouTube region " +
                         st.youtube_region;
    s.youtube_key_present = !store_.youtube_api_key().empty();
    s.frames_presented = display_frames_;
    s.last_frame_us = frame_us_;
    s.last_present_us = present_us_;
    const auto stats = images_.stats();
    s.images_cached = stats.images;
    s.image_bytes = stats.bytes;
    s.controller_connected = controller_connected_;
    s.player = player_->status();
    return s;
}
} // namespace akeno
