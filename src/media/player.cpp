// AKENO STREAM PS5 - Playback engine.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/player.hpp"

#include "core/url.hpp"
#include "media/hls.hpp"
#include "media/remux.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace akeno::media
{
namespace
{
constexpr std::size_t kPlaylistBytes = 4u * 1024u * 1024u;
constexpr std::size_t kSegmentBytes = 48u * 1024u * 1024u;
constexpr std::size_t kPushChunk = 188u * 348u; // ~64 KiB of whole TS packets
constexpr std::size_t kWorkerStack = 1024u * 1024u;

enum class RunResult
{
    finished, // source exhausted, drained normally
    stopped,  // user stop
    seek,     // restart at a new position
    failed,
};

// Wraps a sink's callbacks to observe the first presentation timestamps and
// count submissions before forwarding.
struct Tap
{
    iptv_stream_backend_t inner{};
    std::atomic<std::uint64_t> first_pts{UINT64_MAX};
    std::atomic<std::uint64_t> video_units{0};
    std::atomic<std::uint64_t> audio_units{0};
    std::atomic<bool> opened{false};
    iptv_stream_format_t format{};
    std::mutex format_lock;

    static int open(void *self, const iptv_stream_format_t *format)
    {
        auto &t = *static_cast<Tap *>(self);
        {
            std::lock_guard<std::mutex> guard(t.format_lock);
            t.format = *format;
        }
        const int result = t.inner.open ? t.inner.open(t.inner.context, format) : -1;
        t.opened.store(result == 0);
        return result;
    }
    static int video(void *self, const std::uint8_t *data, std::size_t bytes, std::uint64_t pts)
    {
        auto &t = *static_cast<Tap *>(self);
        // B-frames arrive in decode order: the smallest of the first few PTS
        // values is the first presented one.
        if (pts != IPTV_STREAM_PTS_UNKNOWN && t.video_units.load() < 8 && pts < t.first_pts.load())
            t.first_pts.store(pts);
        t.video_units.fetch_add(1);
        return t.inner.submit_video(t.inner.context, data, bytes, pts);
    }
    static int audio(void *self, const std::uint8_t *data, std::size_t bytes, std::uint64_t pts)
    {
        auto &t = *static_cast<Tap *>(self);
        t.audio_units.fetch_add(1);
        return t.inner.submit_audio ? t.inner.submit_audio(t.inner.context, data, bytes, pts) : 0;
    }
    static int disable_audio(void *self)
    {
        auto &t = *static_cast<Tap *>(self);
        return t.inner.disable_audio ? t.inner.disable_audio(t.inner.context) : 0;
    }
    static int discontinuity(void *self)
    {
        auto &t = *static_cast<Tap *>(self);
        return t.inner.discontinuity ? t.inner.discontinuity(t.inner.context) : 0;
    }
    static int drain(void *self)
    {
        auto &t = *static_cast<Tap *>(self);
        return t.inner.drain ? t.inner.drain(t.inner.context) : 0;
    }
    static void close(void *self)
    {
        auto &t = *static_cast<Tap *>(self);
        if (t.inner.close)
            t.inner.close(t.inner.context);
    }
    static int select_audio(void *self, std::uint32_t type)
    {
        auto &t = *static_cast<Tap *>(self);
        return t.inner.select_audio ? t.inner.select_audio(t.inner.context, type) : 0;
    }
    static int programme_boundary(void *self)
    {
        auto &t = *static_cast<Tap *>(self);
        return t.inner.programme_boundary ? t.inner.programme_boundary(t.inner.context) : 0;
    }

    iptv_stream_backend_t callbacks()
    {
        iptv_stream_backend_t b{};
        b.context = this;
        b.open = open;
        b.submit_video = video;
        b.submit_audio = audio;
        b.disable_audio = disable_audio;
        b.discontinuity = discontinuity;
        b.drain = drain;
        b.close = close;
        b.select_audio = inner.select_audio ? select_audio : nullptr;
        b.programme_boundary = inner.programme_boundary ? programme_boundary : nullptr;
        b.hardware_validated = inner.hardware_validated;
        return b;
    }
};

std::string describe_variant(const hls::Variant &v)
{
    char text[96];
    if (v.width > 0)
        std::snprintf(text, sizeof(text), "%dx%d @ %.1f Mbps", v.width, v.height,
                      v.bandwidth / 1.0e6);
    else
        std::snprintf(text, sizeof(text), "%.1f Mbps", v.bandwidth / 1.0e6);
    return text;
}

const char *codec_name(std::uint32_t video_codec)
{
    switch (video_codec)
    {
    case IPTV_STREAM_VIDEO_H264:
        return "H.264";
    case IPTV_STREAM_VIDEO_HEVC:
        return "HEVC";
    case IPTV_STREAM_VIDEO_VP9:
        return "VP9";
    default:
        return "";
    }
}

const char *audio_name(std::uint32_t stream_type)
{
    switch (stream_type)
    {
    case 0x0f:
        return "AAC";
    case 0x11:
        return "AAC (LATM)";
    case 0x03:
    case 0x04:
        return "MPEG audio";
    case 0x81:
        return "AC-3";
    case 0x87:
        return "E-AC-3";
    case 0:
        return "";
    default:
        return "other";
    }
}
} // namespace

const char *state_name(PlayerState state) noexcept
{
    switch (state)
    {
    case PlayerState::idle:
        return "Idle";
    case PlayerState::opening:
        return "Opening";
    case PlayerState::buffering:
        return "Buffering";
    case PlayerState::playing:
        return "Playing";
    case PlayerState::paused:
        return "Paused";
    case PlayerState::seeking:
        return "Seeking";
    case PlayerState::ended:
        return "Ended";
    case PlayerState::stopped:
        return "Stopped";
    case PlayerState::error:
        return "Error";
    }
    return "Unknown";
}

struct Player::Session
{
    PlayRequest request;
    platform::Thread thread;
    std::atomic<bool> stop{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> seek_pending{false};
    std::atomic<bool> finished{false};
    std::mutex control_lock; // guards sink, cancel, seek_target
    std::unique_ptr<DecodeSink> sink;
    net::CancelFlag cancel = net::make_cancel_flag();
    double seek_target = 0.0;
    std::atomic<bool> stop_or_seek{false}; // shared stop flag handed to the remuxer

    // Timeline of the current attempt.
    std::atomic<double> base_seconds{0.0};
    Tap *tap = nullptr;
    double duration = 0.0;
    bool live = false;
    bool seekable = false;
};

Player::Player(PlayerConfig config) : config_{std::move(config)}
{
}

Player::~Player()
{
    stop();
}

bool Player::active() const noexcept
{
    return session_ && !session_->finished.load();
}

void Player::play(const PlayRequest &request)
{
    stop();
    if (config_.frames)
        config_.frames->clear();
    session_ = std::make_unique<Session>();
    session_->request = request;
    {
        std::lock_guard<std::mutex> guard(status_lock_);
        status_ = PlayerStatus{};
        status_.state = PlayerState::opening;
        status_.title = request.title;
        status_.source_url = url::redact(request.url);
        status_.container = request.kind == SourceKind::local_file ? "File" : "MPEG-TS";
    }
    if (!platform::start_thread(session_->thread, thread_entry, this, kWorkerStack, "akeno-player"))
    {
        std::lock_guard<std::mutex> guard(status_lock_);
        status_.state = PlayerState::error;
        status_.error = "could not start the playback thread";
        session_->finished.store(true);
    }
}

void Player::toggle_pause()
{
    if (!session_ || session_->finished.load())
        return;
    const bool pause = !session_->paused.load();
    session_->paused.store(pause);
    std::lock_guard<std::mutex> guard(session_->control_lock);
    if (session_->sink)
        session_->sink->set_paused(pause);
}

void Player::seek_to(double seconds)
{
    if (!session_ || session_->finished.load())
        return;
    {
        std::lock_guard<std::mutex> lock(status_lock_);
        if (!status_.seekable)
            return;
        if (status_.duration > 0.0)
            seconds = std::min(seconds, std::max(0.0, status_.duration - 3.0));
        status_.state = PlayerState::seeking;
        status_.position = std::max(0.0, seconds);
    }
    std::lock_guard<std::mutex> guard(session_->control_lock);
    session_->seek_target = std::max(0.0, seconds);
    session_->seek_pending.store(true);
    session_->stop_or_seek.store(true);
    session_->cancel->store(true);
    if (session_->sink)
        session_->sink->request_stop();
}

void Player::seek_by(double delta)
{
    double position;
    {
        std::lock_guard<std::mutex> lock(status_lock_);
        position = status_.position;
    }
    seek_to(position + delta);
}

void Player::stop()
{
    if (!session_)
        return;
    session_->stop.store(true);
    {
        std::lock_guard<std::mutex> guard(session_->control_lock);
        session_->stop_or_seek.store(true);
        session_->cancel->store(true);
        if (session_->sink)
            session_->sink->request_stop();
    }
    platform::join_thread(session_->thread);
    session_.reset();
    std::lock_guard<std::mutex> guard(status_lock_);
    if (status_.state != PlayerState::error && status_.state != PlayerState::ended)
        status_.state = PlayerState::stopped;
}

PlayerStatus Player::status() const
{
    PlayerStatus out;
    {
        std::lock_guard<std::mutex> guard(status_lock_);
        out = status_;
    }
    if (!session_)
        return out;
    std::lock_guard<std::mutex> guard(session_->control_lock);
    if (session_->sink)
    {
        session_->sink->fill_status(out);
        const std::uint64_t presented = session_->sink->presented_pts_us();
        const Tap *tap = session_->tap;
        if (tap && presented != UINT64_MAX && tap->first_pts.load() != UINT64_MAX &&
            out.state != PlayerState::seeking)
        {
            const std::uint64_t first = tap->first_pts.load();
            const double offset =
                presented > first ? static_cast<double>(presented - first) / 1.0e6 : 0.0;
            out.position = session_->base_seconds.load() + offset;
            if (out.duration > 0.0)
                out.position = std::min(out.position, out.duration);
        }
        if (tap)
        {
            out.access_units = tap->video_units.load();
            out.audio_frames = tap->audio_units.load();
        }
        if (Tap *open_tap = session_->tap; open_tap && open_tap->opened.load())
        {
            std::lock_guard<std::mutex> format_guard(open_tap->format_lock);
            const iptv_stream_format_t &f = open_tap->format;
            out.video_codec = codec_name(f.video_codec);
            out.audio_codec = audio_name(f.audio_stream_type);
            if (f.visible_width)
            {
                out.width = static_cast<int>(f.visible_width);
                out.height = static_cast<int>(f.visible_height);
            }
        }
    }
    if (session_->paused.load() && out.state == PlayerState::playing)
        out.state = PlayerState::paused;
    if (config_.frames)
        out.convert_us = config_.frames->last_convert_us();
    return out;
}

void *Player::thread_entry(void *self)
{
    static_cast<Player *>(self)->run();
    return nullptr;
}

void Player::run()
{
    Session &s = *session_;
    const auto set_state = [&](PlayerState state)
    {
        std::lock_guard<std::mutex> guard(status_lock_);
        if (status_.state != PlayerState::error)
            status_.state = state;
    };
    const auto set_error = [&](const std::string &why)
    {
        std::lock_guard<std::mutex> guard(status_lock_);
        status_.state = PlayerState::error;
        status_.error = why;
    };
    const auto add_notice = [&](const std::string &notice)
    {
        if (notice.empty())
            return;
        std::lock_guard<std::mutex> guard(status_lock_);
        if (status_.notice.find(notice) == std::string::npos)
            status_.notice = status_.notice.empty() ? notice : status_.notice + "; " + notice;
    };

    net::Client http;
    hls::Playlist media;
    std::string media_url;
    double start = std::max(0.0, s.request.start_seconds);

    // Resolve the HLS playlist once per session.
    if (s.request.kind == SourceKind::hls)
    {
        const auto fetch = [&](const std::string &target, std::string *body,
                               std::string *error) -> bool
        {
            for (int attempt = 0; attempt < 3 && !s.stop.load(); ++attempt)
            {
                net::Request r;
                r.url = target;
                r.max_bytes = kPlaylistBytes;
                r.cancel = s.cancel;
                const net::Response response = http.perform(r);
                {
                    std::lock_guard<std::mutex> guard(status_lock_);
                    status_.last_http_status = response.status;
                    status_.bytes_downloaded += response.bytes;
                }
                if (response.ok())
                {
                    *body = response.body;
                    return true;
                }
                *error = response.describe();
                if (response.outcome == net::Outcome::http_error && response.status >= 400 &&
                    response.status < 500)
                    break;
                platform::sleep_us(400000u * static_cast<unsigned>(attempt + 1));
            }
            return false;
        };
        std::string body, error;
        if (!fetch(s.request.url, &body, &error))
        {
            set_error("Could not load the playlist: " + error);
            s.finished.store(true);
            return;
        }
        auto parsed = hls::parse(body, s.request.url);
        if (!parsed.ok)
        {
            set_error("Invalid HLS playlist: " + parsed.error);
            s.finished.store(true);
            return;
        }
        media_url = s.request.url;
        if (parsed.playlist.kind == hls::Kind::master)
        {
            hls::Selection selection;
            selection.max_height = s.request.max_height;
            selection.max_width = s.request.max_height * 16 / 9 + 16;
            std::string why;
            const int index = hls::select_variant(parsed.playlist, selection, &why);
            if (index < 0)
            {
                set_error("No playable stream: " + why);
                s.finished.store(true);
                return;
            }
            add_notice(why);
            const hls::Variant &variant = parsed.playlist.variants[static_cast<std::size_t>(index)];
            {
                std::lock_guard<std::mutex> guard(status_lock_);
                status_.variant = describe_variant(variant);
                status_.variant_count = static_cast<int>(parsed.playlist.variants.size());
            }
            media_url = variant.uri;
            if (!parsed.playlist.unsupported.empty())
                add_notice(parsed.playlist.unsupported);
            if (!fetch(media_url, &body, &error))
            {
                set_error("Could not load the stream playlist: " + error);
                s.finished.store(true);
                return;
            }
            parsed = hls::parse(body, media_url);
            if (!parsed.ok || parsed.playlist.kind != hls::Kind::media)
            {
                set_error("Invalid stream playlist: " +
                          (parsed.ok ? std::string{"not a media playlist"} : parsed.error));
                s.finished.store(true);
                return;
            }
        }
        media = std::move(parsed.playlist);
        if (!media.unsupported.empty())
        {
            set_error("This stream cannot be played: " + media.unsupported);
            s.finished.store(true);
            return;
        }
        s.live = media.is_live();
        s.duration = s.live ? 0.0 : media.total_duration;
        s.seekable = !s.live && media.total_duration > 0.0;
        std::lock_guard<std::mutex> guard(status_lock_);
        status_.live = s.live;
        status_.duration = s.duration;
        status_.seekable = s.seekable;
        status_.segments_total = static_cast<int>(media.segments.size());
    }

    // One attempt per start position; a seek starts a new attempt.
    for (;;)
    {
        if (s.stop.load())
            break;
        set_state(PlayerState::buffering);
        std::unique_ptr<DecodeSink> sink = config_.make_sink ? config_.make_sink() : nullptr;
        std::string error;
        if (!sink || !sink->init(&error))
        {
            set_error("Decoder unavailable: " +
                      (error.empty() ? std::string{"no decoder"} : error));
            break;
        }
        Tap tap;
        tap.inner = sink->callbacks();
        {
            std::lock_guard<std::mutex> guard(s.control_lock);
            s.sink = std::move(sink);
            s.tap = &tap;
            s.cancel = net::make_cancel_flag();
            s.stop_or_seek.store(false);
            s.seek_pending.store(false);
            if (s.paused.load())
                s.sink->set_paused(true);
        }
        iptv_stream_session_t stream{};
        iptv_stream_init(&stream);
        iptv_stream_backend_t callbacks = tap.callbacks();
        RunResult result = RunResult::failed;
        std::string failure;
        if (iptv_stream_open(&stream, nullptr, &callbacks) != IPTV_STREAM_OK ||
            iptv_stream_start(&stream) != IPTV_STREAM_OK)
        {
            failure = "MPEG-TS demuxer could not start";
        }
        else
        {
            const auto interrupted = [&] { return s.stop.load() || s.seek_pending.load(); };
            // Pushes bytes into the demuxer in whole-packet chunks.
            const auto push = [&](const std::uint8_t *data, std::size_t bytes) -> bool
            {
                while (bytes)
                {
                    if (interrupted())
                        return false;
                    const std::size_t chunk = std::min(bytes, kPushChunk);
                    const int pushed = iptv_stream_push(&stream, data, chunk);
                    if (pushed != IPTV_STREAM_OK)
                    {
                        if (!interrupted())
                        {
                            const iptv_stream_telemetry_t *t = iptv_stream_telemetry(&stream);
                            failure = t && t->last_error[0] ? t->last_error : "MPEG-TS demux error";
                        }
                        return false;
                    }
                    data += chunk;
                    bytes -= chunk;
                }
                if (tap.opened.load() && !s.paused.load())
                    set_state(PlayerState::playing);
                return true;
            };

            if (s.request.kind == SourceKind::local_file)
            {
                Remuxer remux;
                if (!remux.open(s.request.url, &failure))
                {
                    result = RunResult::failed;
                }
                else
                {
                    const RemuxInfo &info = remux.info();
                    s.duration = info.duration;
                    s.seekable = info.duration > 0.0;
                    {
                        std::lock_guard<std::mutex> guard(status_lock_);
                        status_.duration = s.duration;
                        status_.seekable = s.seekable;
                        status_.container = info.container;
                    }
                    add_notice(info.notice);
                    if (start > 0.0 && !remux.seek(start))
                        start = 0.0;
                    s.base_seconds.store(start);
                    const int outcome =
                        remux.run([&](const std::uint8_t *d, std::size_t n) { return push(d, n); },
                                  s.stop_or_seek, &failure, &s.base_seconds);
                    result = outcome == 1   ? RunResult::finished
                             : outcome == 0 ? RunResult::stopped
                                            : RunResult::failed;
                    if (outcome != -1 && !failure.empty() && !interrupted())
                        result = RunResult::failed;
                }
            }
            else if (s.request.kind == SourceKind::http_ts)
            {
                s.base_seconds.store(0.0);
                net::Request r;
                r.url = s.request.url;
                r.max_bytes = static_cast<std::size_t>(8) * 1024 * 1024 * 1024;
                r.total_timeout_ms = 0;
                r.cancel = s.cancel;
                r.on_data = [&](const std::uint8_t *d, std::size_t n) { return push(d, n); };
                const net::Response response = http.perform(r);
                {
                    std::lock_guard<std::mutex> guard(status_lock_);
                    status_.last_http_status = response.status;
                    status_.bytes_downloaded += response.bytes;
                }
                if (response.ok())
                    result = RunResult::finished;
                else if (interrupted())
                    result = RunResult::stopped;
                else
                {
                    result = RunResult::failed;
                    if (failure.empty())
                        failure = "Download failed: " + response.describe();
                }
            }
            else
            {
                // HLS segments.
                std::size_t index = 0;
                if (s.live)
                    index =
                        media.segments.size() > static_cast<std::size_t>(config_.live_edge_segments)
                            ? media.segments.size() -
                                  static_cast<std::size_t>(config_.live_edge_segments)
                            : 0;
                else
                    index = hls::segment_at(media, start);
                s.base_seconds.store(
                    s.live || media.segments.empty() ? 0.0 : media.segments[index].start);
                std::uint64_t next_sequence =
                    index < media.segments.size() ? media.segments[index].sequence : 0;
                std::uint64_t last_reload_us = platform::monotonic_us();
                int stale_reloads = 0;
                result = RunResult::finished;
                for (;;)
                {
                    if (interrupted())
                    {
                        result = RunResult::stopped;
                        break;
                    }
                    // Find the next segment by sequence number (live playlists slide).
                    const auto it = std::find_if(media.segments.begin(), media.segments.end(),
                                                 [&](const hls::Segment &seg)
                                                 { return seg.sequence >= next_sequence; });
                    if (it == media.segments.end())
                    {
                        if (!s.live)
                            break; // VOD complete
                        // Live: wait about half a target duration, then reload.
                        const std::uint64_t wait_us = static_cast<std::uint64_t>(
                            std::max(1.0, media.target_duration / 2.0) * 1.0e6);
                        while (!interrupted() &&
                               platform::monotonic_us() - last_reload_us < wait_us)
                            platform::sleep_us(50000);
                        if (interrupted())
                            continue;
                        net::Request r;
                        r.url = media_url;
                        r.max_bytes = kPlaylistBytes;
                        r.cancel = s.cancel;
                        const net::Response reload = http.perform(r);
                        last_reload_us = platform::monotonic_us();
                        auto parsed =
                            reload.ok() ? hls::parse(reload.body, media_url) : hls::ParseResult{};
                        if (parsed.ok && parsed.playlist.kind == hls::Kind::media)
                        {
                            const bool grew =
                                !parsed.playlist.segments.empty() &&
                                parsed.playlist.segments.back().sequence >= next_sequence;
                            stale_reloads = grew ? 0 : stale_reloads + 1;
                            media = std::move(parsed.playlist);
                            if (!media.is_live())
                                s.live = false; // the event ended (ENDLIST appeared)
                        }
                        else
                        {
                            ++stale_reloads;
                        }
                        if (stale_reloads > 12)
                        {
                            failure = "The live stream stopped updating";
                            result = RunResult::failed;
                            break;
                        }
                        continue;
                    }
                    const hls::Segment segment = *it;
                    if (segment.sequence > next_sequence && s.live)
                        (void)iptv_stream_discontinuity(&stream); // fell behind the live window
                    std::string body;
                    bool downloaded = false;
                    net::Response response;
                    for (int attempt = 0; attempt <= config_.segment_retries && !interrupted();
                         ++attempt)
                    {
                        net::Request r;
                        r.url = segment.uri;
                        r.max_bytes = kSegmentBytes;
                        r.cancel = s.cancel;
                        response = http.perform(r);
                        {
                            std::lock_guard<std::mutex> guard(status_lock_);
                            status_.last_http_status = response.status;
                            status_.bytes_downloaded += response.bytes;
                            if (response.ok() && response.elapsed_ms > 0)
                                status_.throughput_kbps = static_cast<std::uint32_t>(
                                    response.bytes * 8u / response.elapsed_ms);
                            if (attempt > 0)
                                ++status_.retries;
                        }
                        if (response.ok())
                        {
                            body = std::move(response.body);
                            downloaded = true;
                            break;
                        }
                        if (response.outcome == net::Outcome::cancelled)
                            break;
                        platform::sleep_us(500000u * static_cast<unsigned>(attempt + 1));
                    }
                    if (!downloaded)
                    {
                        if (interrupted())
                        {
                            result = RunResult::stopped;
                            break;
                        }
                        failure = "Segment download failed: " + response.describe();
                        result = RunResult::failed;
                        break;
                    }
                    if (body.size() < 188 || static_cast<unsigned char>(body[0]) != 0x47)
                    {
                        failure = "A segment is not MPEG-TS (first byte " +
                                  std::to_string(
                                      body.empty() ? -1 : static_cast<unsigned char>(body[0])) +
                                  ")";
                        result = RunResult::failed;
                        break;
                    }
                    if (segment.discontinuity &&
                        segment.sequence != media.segments.front().sequence)
                        (void)iptv_stream_discontinuity(&stream);
                    if (!push(reinterpret_cast<const std::uint8_t *>(body.data()), body.size()))
                    {
                        result = interrupted() ? RunResult::stopped : RunResult::failed;
                        break;
                    }
                    next_sequence = segment.sequence + 1;
                    std::lock_guard<std::mutex> guard(status_lock_);
                    ++status_.segments_loaded;
                }
            }
        }

        if (s.seek_pending.load() && !s.stop.load())
            result = RunResult::seek;
        if (result == RunResult::finished)
        {
            // Let queued pictures and audio play out.
            const int stopped = iptv_stream_stop(&stream);
            if (stopped != IPTV_STREAM_OK && !s.stop.load() && !s.seek_pending.load())
            {
                const iptv_stream_telemetry_t *t = iptv_stream_telemetry(&stream);
                if (tap.video_units.load() == 0)
                {
                    result = RunResult::failed;
                    failure =
                        t && t->last_error[0] ? t->last_error : "no video was found in the stream";
                }
            }
            if (s.seek_pending.load() && !s.stop.load())
                result = RunResult::seek;
        }
        {
            const iptv_stream_telemetry_t *t = iptv_stream_telemetry(&stream);
            std::lock_guard<std::mutex> guard(status_lock_);
            if (t)
            {
                status_.video_codec = codec_name(t->format.video_codec);
                status_.audio_codec = audio_name(t->format.audio_stream_type);
                if (t->format.visible_width)
                {
                    status_.width = static_cast<int>(t->format.visible_width);
                    status_.height = static_cast<int>(t->format.visible_height);
                }
                if (t->last_error[0])
                    status_.demux_error = t->last_error;
            }
        }
        (void)iptv_stream_cleanup(&stream);
        {
            std::lock_guard<std::mutex> guard(s.control_lock);
            if (s.sink)
            {
                // Keep the final decoder counters and position for the status screen.
                PlayerStatus last;
                s.sink->fill_status(last);
                const std::uint64_t presented = s.sink->presented_pts_us();
                const std::uint64_t first = tap.first_pts.load();
                if (presented != UINT64_MAX && first != UINT64_MAX && presented >= first)
                {
                    std::lock_guard<std::mutex> status_guard(status_lock_);
                    status_.position =
                        s.base_seconds.load() + static_cast<double>(presented - first) / 1.0e6;
                }
                s.sink->shutdown();
                std::lock_guard<std::mutex> status_guard(status_lock_);
                status_.frames_decoded = last.frames_decoded;
                status_.frames_presented = last.frames_presented;
                status_.decoder = last.decoder;
                status_.decoder_errors = last.decoder_errors;
                status_.last_native_result = last.last_native_result;
            }
            s.sink.reset();
            s.tap = nullptr;
        }

        if (result == RunResult::seek)
        {
            std::lock_guard<std::mutex> guard(s.control_lock);
            start = s.seek_target;
            set_state(PlayerState::seeking);
            if (config_.frames)
                config_.frames->clear();
            continue;
        }
        if (result == RunResult::failed)
        {
            set_error(failure.empty() ? "Playback failed" : failure);
            break;
        }
        if (result == RunResult::stopped || s.stop.load())
        {
            set_state(PlayerState::stopped);
            break;
        }
        {
            std::lock_guard<std::mutex> guard(status_lock_);
            if (status_.state != PlayerState::error)
            {
                status_.state = PlayerState::ended;
                if (s.duration > 0.0)
                    status_.position = s.duration;
            }
        }
        break;
    }
    s.finished.store(true);
}
} // namespace akeno::media
