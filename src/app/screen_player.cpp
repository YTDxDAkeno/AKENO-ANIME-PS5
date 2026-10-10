// AKENO STREAM PS5 - Full-screen player with on-screen controls.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/screens.hpp"
#include "core/fs.hpp"
#include "media/subtitles.hpp"

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
constexpr std::uint64_t kOsdTimeoutMs = 4500;
constexpr std::uint64_t kSeekCommitMs = 650;
constexpr std::uint64_t kUpNextMs = 8000;

class PlayerScreen final : public Screen
{
  public:
    PlayerScreen(App &app, MediaItem item, std::shared_ptr<PlayerContext> context)
        : Screen{app}, item_{std::move(item)}, context_{std::move(context)}
    {
        // Subtitles start on (the first track) when the episode has any.
        if (context_ && !context_->subtitles.empty())
            select_subtitles(0, false);
    }
    ~PlayerScreen() override
    {
        *alive_ = false;
    }

    [[nodiscard]] bool full_screen() const override
    {
        return true;
    }
    [[nodiscard]] bool animating() const override
    {
        return true;
    }

    void handle(input::Button b) override
    {
        osd_until_ = app_.now_ms() + kOsdTimeoutMs;
        const media::PlayerState state = status_.state;
        const bool finished =
            state == media::PlayerState::ended || state == media::PlayerState::error;
        const bool up_next = state == media::PlayerState::ended && has_next();
        switch (b)
        {
        case input::Button::l2:
        case input::Button::r2:
            if (b == input::Button::r2 ? has_next() : has_previous())
                step_episode(b == input::Button::r2);
            else if (context_)
                app_.toast(b == input::Button::r2 ? "This is the last episode"
                                                  : "This is the first episode",
                           th::kInfo);
            return;
        case input::Button::circle:
            if (show_info_)
            {
                show_info_ = false;
                return;
            }
            app_.player().stop();
            app_.pop();
            return;
        case input::Button::cross:
            if (up_next)
            {
                step_episode(true);
                return;
            }
            if (finished)
            {
                restart(state == media::PlayerState::ended ? 0.0 : status_.position);
                return;
            }
            app_.player().toggle_pause();
            return;
        case input::Button::left:
        case input::Button::right:
        case input::Button::l1:
        case input::Button::r1:
        {
            if (!status_.seekable)
            {
                app_.toast(status_.live ? "Seeking is not available in live streams"
                                        : "This stream cannot seek",
                           th::kInfo);
                return;
            }
            const double step = (b == input::Button::l1 || b == input::Button::r1) ? 60.0 : 10.0;
            const double sign = (b == input::Button::left || b == input::Button::l1) ? -1.0 : 1.0;
            if (seek_pending_at_ == 0)
                seek_target_ = status_.position;
            seek_target_ =
                std::clamp(seek_target_ + sign * step, 0.0, std::max(0.0, status_.duration - 3.0));
            seek_pending_at_ = app_.now_ms();
            return;
        }
        case input::Button::up:
        case input::Button::down:
        {
            Settings s = app_.store().settings();
            s.volume = std::clamp(s.volume + (b == input::Button::up ? 10 : -10), 0, 100);
            app_.store().update_settings(s);
            app_.apply_settings();
            volume_until_ = app_.now_ms() + 1500;
            return;
        }
        case input::Button::square:
        {
            if (up_next)
            {
                restart(0.0); // replay this episode instead
                return;
            }
            // Cycle the quality cap and restart at the current position.
            Settings s = app_.store().settings();
            s.max_height = s.max_height >= 1080 ? 720 : s.max_height >= 720 ? 480 : 1080;
            app_.store().update_settings(s);
            app_.toast("Maximum quality: " + std::to_string(s.max_height) + "p", th::kInfo);
            if (item_.playable && item_.playable->kind == media::SourceKind::hls && !finished)
                restart(status_.position);
            return;
        }
        case input::Button::triangle:
            if (!context_ || context_->subtitles.empty())
            {
                app_.toast("This video has no subtitles", th::kInfo);
                return;
            }
            // Off -> first track -> ... -> off.
            select_subtitles(
                subtitle_ + 1 >= static_cast<int>(context_->subtitles.size()) ? -1 : subtitle_ + 1,
                true);
            return;
        case input::Button::options:
            show_info_ = !show_info_;
            return;
        default:
            return;
        }
    }

    void update(std::uint64_t now_ms) override
    {
        const media::PlayerState before = status_.state;
        status_ = app_.player().status();
        // The episode ended: the next one starts after a short countdown.
        if (status_.state == media::PlayerState::ended && before != media::PlayerState::ended &&
            has_next())
            up_next_at_ = now_ms + kUpNextMs;
        if (status_.state != media::PlayerState::ended)
            up_next_at_ = 0;
        if (up_next_at_ && now_ms >= up_next_at_)
        {
            up_next_at_ = 0;
            step_episode(true);
            return;
        }
        if (seek_pending_at_ && now_ms - seek_pending_at_ >= kSeekCommitMs)
        {
            app_.player().seek_to(seek_target_);
            seek_pending_at_ = 0;
        }
    }

    void render(ui::Painter &p, std::uint64_t now_ms) override
    {
        const bool have_frame = app_.frames()->draw(p.s, p.s.bounds());
        const media::PlayerStatus &s = status_;
        const bool waiting = s.state == media::PlayerState::opening ||
                             s.state == media::PlayerState::buffering ||
                             s.state == media::PlayerState::seeking;
        const bool paused = s.state == media::PlayerState::paused;
        const bool finished =
            s.state == media::PlayerState::ended || s.state == media::PlayerState::error;
        const bool osd = now_ms < osd_until_ || paused || waiting || finished ||
                         seek_pending_at_ != 0 || !have_frame;

        if (!have_frame && !finished)
        {
            p.s.fill(p.s.bounds(), gfx::rgba(5, 7, 12));
            p.text_center(th::kWidth / 2, 600, item_.title, th::kHeading, th::kText, 1400);
        }
        if (waiting || (!have_frame && !finished))
        {
            p.spinner(th::kWidth / 2, 500, 40, now_ms, th::kText);
            p.text_center(th::kWidth / 2, 660,
                          s.state == media::PlayerState::seeking   ? "Seeking..."
                          : s.state == media::PlayerState::opening ? "Opening stream..."
                                                                   : "Buffering...",
                          th::kBody, th::kTextSecondary);
        }
        if (osd)
            render_osd(p, now_ms);
        if (paused && seek_pending_at_ == 0)
        {
            p.s.fill_circle(th::kWidth / 2, th::kHeight / 2, 70, gfx::rgba(0, 0, 0, 150));
            p.icon(Icon::pause, th::kWidth / 2, th::kHeight / 2, 64, th::kText);
        }
        if (subtitle_ >= 0 && !finished)
            render_subtitles(p, osd);
        if (now_ms < volume_until_)
            render_volume(p);
        if (finished && s.state == media::PlayerState::ended && has_next())
            render_up_next(p, now_ms);
        else if (finished)
            render_finished(p);
        if (show_info_)
            render_info(p);
    }

  private:
    // Replaces this screen with a fresh playback of the same item. pop()
    // destroys this object, so only locals are used afterwards.
    void restart(double position)
    {
        App &app = app_;
        const MediaItem item = item_;
        const auto context = context_;
        app.pop();
        app.play(item, position, context);
    }

    [[nodiscard]] bool has_next() const
    {
        return context_ && context_->play_next;
    }
    [[nodiscard]] bool has_previous() const
    {
        return context_ && context_->play_previous;
    }

    // Stops this episode (its progress is saved) and opens the next or
    // previous one. The screen leaves after the current update (it may be
    // running inside App::update's loop over screens); the episode opens on
    // the next frame.
    void step_episode(bool next)
    {
        if (closing())
            return;
        std::function<void()> go = next ? context_->play_next : context_->play_previous;
        app_.player().stop();
        close_later();
        app_.jobs().post(std::move(go));
    }

    void select_subtitles(int index, bool announce)
    {
        subtitle_ = index;
        cues_.clear();
        if (index < 0)
        {
            if (announce)
                app_.toast("Subtitles off", th::kInfo);
            return;
        }
        const anime::SubtitleTrack track = context_->subtitles[static_cast<std::size_t>(index)];
        if (announce)
            app_.toast("Subtitles: " + track.label, th::kInfo);
        auto alive = alive_;
        App &app = app_;
        app_.jobs().run(
            [track, index, alive, &app, this]
            {
                std::string text;
                if (!track.url.empty() && track.url.front() == '/')
                {
                    if (fs::safe_path(track.url))
                        text = fs::read_text(track.url, 4u * 1024u * 1024u).value_or("");
                }
                else
                {
                    const net::Response r = net::get(track.url, {}, 4u * 1024u * 1024u);
                    if (r.ok())
                        text = r.body;
                }
                media::SubtitleDocument doc = media::parse_subtitles(text, track.format);
                app.jobs().post(
                    [doc = std::move(doc), index, alive, &app, this]() mutable
                    {
                        if (!*alive || index != subtitle_)
                            return;
                        if (!doc.ok)
                        {
                            app.toast("Subtitles could not be read: " + doc.error, th::kWarning);
                            return;
                        }
                        cues_ = std::move(doc.cues);
                    });
            });
    }

    void render_subtitles(ui::Painter &p, bool osd)
    {
        const std::string text = media::cue_text_at(cues_, status_.position);
        if (text.empty())
            return;
        std::vector<std::string> lines;
        std::size_t start = 0;
        while (start <= text.size() && lines.size() < 3)
        {
            const std::size_t end = text.find('\n', start);
            lines.push_back(
                text.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
        const int line_h = p.line_height(th::kHeading) + 6;
        int y =
            (osd ? th::kHeight - 330 : th::kHeight - 120) - line_h * static_cast<int>(lines.size());
        for (const std::string &line : lines)
        {
            const int w = std::min(p.measure(line, th::kHeading), th::kWidth - 240);
            const Rect box{(th::kWidth - w) / 2 - 18, y - 4, w + 36, line_h};
            p.s.fill_rounded(box, 10, gfx::rgba(0, 0, 0, 170));
            p.text_center(th::kWidth / 2, y, line, th::kHeading, th::kText, th::kWidth - 240);
            y += line_h;
        }
    }

    void render_up_next(ui::Painter &p, std::uint64_t now_ms)
    {
        const Rect panel{360, 300, 1200, 330};
        p.s.dim(p.s.bounds(), 120);
        p.panel(panel, 28, gfx::with_alpha(th::kSurface, 245));
        p.text(panel.x + 60, panel.y + 44, "Up next", th::kCaptionStrong, th::kAccentAnime);
        p.text(panel.x + 60, panel.y + 82, context_->next_label, th::kHeading, th::kText,
               panel.w - 120);
        p.text(panel.x + 60, panel.y + 136, item_.title, th::kBody, th::kTextSecondary,
               panel.w - 120);
        const double left =
            up_next_at_ > now_ms ? static_cast<double>(up_next_at_ - now_ms) / 1000.0 : 0.0;
        p.progress({panel.x + 60, panel.y + 196, panel.w - 120, 8},
                   1.0 - left / (kUpNextMs / 1000.0), th::kAccentAnime,
                   gfx::rgba(255, 255, 255, 50));
        int x = panel.x + 60;
        const int y = panel.bottom() - 70;
        x += p.hint(x, y, Glyph::cross,
                    "Play now (" + std::to_string(static_cast<int>(left + 0.99)) + " s)") +
             40;
        x += p.hint(x, y, Glyph::square, "Watch again") + 40;
        p.hint(x, y, Glyph::circle, "Back");
    }

    void render_osd(ui::Painter &p, std::uint64_t now_ms)
    {
        const media::PlayerStatus &s = status_;
        p.s.gradient_vertical({0, 0, th::kWidth, 230}, gfx::rgba(0, 0, 0, 200),
                              gfx::rgba(0, 0, 0, 0));
        p.s.gradient_vertical({0, th::kHeight - 300, th::kWidth, 300}, gfx::rgba(0, 0, 0, 0),
                              gfx::rgba(0, 0, 0, 220));
        p.text(th::kMarginX, 54, item_.title, th::kTitle, th::kText, 1300);
        if (!item_.subtitle.empty())
            p.text(th::kMarginX, 118, item_.subtitle, th::kBody, th::kTextSecondary, 1300);
        if (!s.notice.empty())
            p.text(th::kMarginX, 160, s.notice, th::kCaption, th::kWarning, 1500);
        if (s.live)
            p.chip(th::kWidth - th::kMarginX - 90, 60, "LIVE", th::kError, th::kText);

        const int bar_y = th::kHeight - 150;
        const double shown = seek_pending_at_ ? seek_target_ : s.position;
        if (s.duration > 0.0)
        {
            const Rect bar{th::kMarginX, bar_y, th::kWidth - 2 * th::kMarginX, 10};
            p.progress(bar, shown / s.duration, th::kAccentAnime, gfx::rgba(255, 255, 255, 60));
            const int knob =
                bar.x + static_cast<int>(bar.w * std::clamp(shown / s.duration, 0.0, 1.0));
            p.s.fill_circle(knob, bar.y + 5, seek_pending_at_ ? 16 : 11, th::kText);
            p.text(th::kMarginX, bar_y + 26, format_clock(shown), th::kBodyStrong, th::kText);
            p.text_right(th::kWidth - th::kMarginX, bar_y + 26,
                         "-" + format_clock(std::max(0.0, s.duration - shown)), th::kBodyStrong,
                         th::kTextSecondary);
        }
        else if (!s.live)
        {
            p.text(th::kMarginX, bar_y + 26, format_clock(shown), th::kBodyStrong, th::kText);
        }
        // Controls.
        int x = th::kMarginX;
        const int y = th::kHeight - 66;
        x += p.hint(x, y, Glyph::cross, s.state == media::PlayerState::paused ? "Play" : "Pause") +
             34;
        if (s.seekable)
        {
            x += p.hint(x, y, Glyph::dpad, "Seek 10 s") + 34;
            x += p.hint(x, y, Glyph::l1, "") - 4;
            x += p.hint(x, y, Glyph::r1, "1 min") + 34;
        }
        x += p.hint(x, y, Glyph::square, "Quality") + 34;
        if (context_ && !context_->subtitles.empty())
            x += p.hint(x, y, Glyph::triangle,
                        subtitle_ < 0
                            ? "Subtitles: off"
                            : "Subtitles: " +
                                  context_->subtitles[static_cast<std::size_t>(subtitle_)].label) +
                 34;
        if (has_previous() || has_next())
        {
            x += p.hint(x, y, Glyph::l2, "") - 4;
            x += p.hint(x, y, Glyph::r2, "Episode") + 34;
        }
        x += p.hint(x, y, Glyph::options, "Info") + 34;
        p.hint(x, y, Glyph::circle, "Stop");
        (void)now_ms;
    }

    void render_volume(ui::Painter &p)
    {
        const int volume = app_.store().settings().volume;
        const Rect panel{th::kWidth - th::kMarginX - 360, 200, 360, 90};
        p.panel(panel, 20, gfx::with_alpha(th::kSurfaceRaised, 235));
        p.text(panel.x + 26, panel.y + 14, "Volume " + std::to_string(volume) + "%",
               th::kBodyStrong, th::kText);
        p.progress({panel.x + 26, panel.y + 60, panel.w - 52, 10}, volume / 100.0, th::kText,
                   gfx::rgba(255, 255, 255, 50));
    }

    void render_finished(ui::Painter &p)
    {
        const media::PlayerStatus &s = status_;
        const bool error = s.state == media::PlayerState::error;
        const Rect panel{360, 330, 1200, error ? 400 : 260};
        p.s.dim(p.s.bounds(), 120);
        p.panel(panel, 28, gfx::with_alpha(th::kSurface, 245));
        p.icon(error ? Icon::alert : Icon::check, panel.x + 80, panel.y + 86, 64,
               error ? th::kWarning : th::kSuccess);
        p.text(panel.x + 140, panel.y + 52, error ? "Playback stopped" : "Finished", th::kHeading,
               th::kText);
        if (error)
            p.wrapped(panel.x + 140, panel.y + 106, s.error, th::kBody, th::kTextSecondary,
                      panel.w - 200, 5);
        else
            p.text(panel.x + 140, panel.y + 106, item_.title, th::kBody, th::kTextSecondary,
                   panel.w - 200);
        int x = panel.x + 140;
        const int y = panel.bottom() - 70;
        x += p.hint(x, y, Glyph::cross, error ? "Try again" : "Replay") + 40;
        x += p.hint(x, y, Glyph::options, "Technical details") + 40;
        p.hint(x, y, Glyph::circle, "Back");
    }

    void render_info(ui::Painter &p)
    {
        const media::PlayerStatus &s = status_;
        const Rect panel{th::kWidth - th::kMarginX - 700, 200, 700, 680};
        p.panel(panel, 24, gfx::with_alpha(th::kSurface, 240));
        p.text(panel.x + 34, panel.y + 26, "Stream information", th::kHeading, th::kText);
        char line[200];
        std::vector<std::pair<std::string, std::string>> rows;
        rows.emplace_back("State", media::state_name(s.state));
        rows.emplace_back("Decoder", s.decoder.empty() ? "-" : s.decoder);
        std::snprintf(line, sizeof(line), "%s %dx%d", s.video_codec.c_str(), s.width, s.height);
        rows.emplace_back("Video", line);
        std::snprintf(line, sizeof(line), "%s",
                      s.audio_codec.empty() ? "none" : s.audio_codec.c_str());
        rows.emplace_back("Audio", line);
        rows.emplace_back("Container", s.container);
        if (!s.variant.empty())
            rows.emplace_back("Variant",
                              s.variant + " (" + std::to_string(s.variant_count) + " available)");
        std::snprintf(line, sizeof(line), "%.2f fps, %llu decoded, %llu shown, %llu dropped",
                      s.fps_x100 / 100.0, static_cast<unsigned long long>(s.frames_decoded),
                      static_cast<unsigned long long>(s.frames_presented),
                      static_cast<unsigned long long>(s.frames_dropped));
        rows.emplace_back("Frames", line);
        std::snprintf(line, sizeof(line), "%llu video AUs, %llu audio frames",
                      static_cast<unsigned long long>(s.access_units),
                      static_cast<unsigned long long>(s.audio_frames));
        rows.emplace_back("Demuxer", line);
        std::snprintf(line, sizeof(line), "HTTP %ld, %.1f MB, %u kbit/s, %d retries",
                      s.last_http_status, s.bytes_downloaded / 1.0e6, s.throughput_kbps, s.retries);
        rows.emplace_back("Network", line);
        if (s.segments_total)
        {
            std::snprintf(line, sizeof(line), "%d of %d loaded", s.segments_loaded,
                          s.segments_total);
            rows.emplace_back("Segments", line);
        }
        std::snprintf(line, sizeof(line), "%llu underruns, %llu output errors",
                      static_cast<unsigned long long>(s.audio_underruns),
                      static_cast<unsigned long long>(s.audio_errors));
        rows.emplace_back("Audio out", line);
        std::snprintf(line, sizeof(line), "%u us per frame", s.convert_us);
        rows.emplace_back("Colour conv.", line);
        if (s.decoder_errors || s.last_native_result)
        {
            std::snprintf(line, sizeof(line), "%llu errors, last result %d",
                          static_cast<unsigned long long>(s.decoder_errors), s.last_native_result);
            rows.emplace_back("Decoder errors", line);
        }
        int y = panel.y + 90;
        for (const auto &[k, v] : rows)
        {
            p.text(panel.x + 34, y, k, th::kCaptionStrong, th::kTextMuted);
            p.text(panel.x + 230, y, v, th::kCaption, th::kText, panel.w - 260);
            y += 40;
        }
    }

    MediaItem item_;
    std::shared_ptr<PlayerContext> context_;
    int subtitle_ = -1; // the track shown, -1: off
    std::vector<media::Cue> cues_;
    std::uint64_t up_next_at_ = 0;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    media::PlayerStatus status_;
    std::uint64_t osd_until_ = 0;
    std::uint64_t volume_until_ = 0;
    std::uint64_t seek_pending_at_ = 0;
    double seek_target_ = 0.0;
    bool show_info_ = false;
};
} // namespace

std::unique_ptr<Screen> make_player_screen(App &app, MediaItem item,
                                           std::shared_ptr<PlayerContext> context)
{
    return std::make_unique<PlayerScreen>(app, std::move(item), std::move(context));
}
} // namespace akeno
