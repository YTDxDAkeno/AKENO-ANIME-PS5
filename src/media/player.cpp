// AKENO STREAM PS5 - Playback engine.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/player.hpp"

#include "core/url.hpp"
#include "media/byte_source.hpp"
#include "media/hls.hpp"
#include "media/hls_source.hpp"
#include "media/remux.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string_view>
#include <vector>

namespace akeno::media
{
namespace
{
constexpr std::size_t kPlaylistBytes = 4u * 1024u * 1024u;
constexpr std::size_t kSniffBytes = 4096;
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
// Shown until the source reports its real container.
const char *container_label(SourceKind kind)
{
    switch (kind)
    {
    case SourceKind::hls:
        return "HLS";
    case SourceKind::http_ts:
        return "MPEG-TS";
    case SourceKind::local_file:
    case SourceKind::http_file:
        return "File";
    case SourceKind::automatic:
        break;
    }
    return "Detecting";
}

bool starts_with_text(const std::string &bytes, std::string_view prefix)
{
    std::size_t i = 0;
    if (bytes.compare(0, 3, "\xEF\xBB\xBF") == 0)
        i = 3; // UTF-8 byte order mark
    while (i < bytes.size() &&
           (bytes[i] == ' ' || bytes[i] == '\r' || bytes[i] == '\n' || bytes[i] == '\t'))
        ++i;
    return bytes.compare(i, prefix.size(), prefix) == 0;
}
} // namespace

SourceKind sniff_source(std::string_view content_type, const std::string &first_bytes)
{
    std::string type(content_type);
    for (char &c : type)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (starts_with_text(first_bytes, "#EXTM3U") || type.find("mpegurl") != std::string::npos)
        return SourceKind::hls;
    const auto byte = [&](std::size_t i)
    { return i < first_bytes.size() ? static_cast<unsigned char>(first_bytes[i]) : 0u; };
    if (byte(0) == 0x47 && (first_bytes.size() <= 188 || byte(188) == 0x47))
        return SourceKind::http_ts;
    return SourceKind::http_file;
}

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
        status_.container = container_label(request.kind);
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
    if (config_.frames && config_.frames->last_convert_us())
        out.convert_us = config_.frames->last_convert_us();
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
    hls::Playlist audio_media; // separate audio rendition, when the variant has one
    std::string audio_url;
    bool hls_via_ffmpeg = false;
    double start = std::max(0.0, s.request.start_seconds);

    // Network counters for the diagnostics screen.
    const SegmentObserver segment_observer = [this](const SegmentStats &stats)
    {
        std::lock_guard<std::mutex> guard(status_lock_);
        if (stats.segment_done)
        {
            ++status_.segments_loaded;
            return;
        }
        status_.last_http_status = stats.status;
        status_.bytes_downloaded += stats.bytes;
        if (stats.status >= 200 && stats.status < 300 && stats.elapsed_ms > 0)
            status_.throughput_kbps =
                static_cast<std::uint32_t>(stats.bytes * 8u / stats.elapsed_ms);
        if (stats.retry)
            ++status_.retries;
    };
    const std::uint64_t transfer_started = platform::monotonic_us();
    const TransferObserver transfer_observer =
        [this, transfer_started](long code, std::size_t bytes)
    {
        std::lock_guard<std::mutex> guard(status_lock_);
        status_.last_http_status = code;
        status_.bytes_downloaded += bytes;
        const std::uint64_t ms = (platform::monotonic_us() - transfer_started) / 1000u;
        if (ms > 0)
            status_.throughput_kbps =
                static_cast<std::uint32_t>(status_.bytes_downloaded * 8u / ms);
    };

    // A web address of unknown kind: its first bytes decide.
    if (s.request.kind == SourceKind::automatic)
    {
        std::string first;
        long code = 0;
        net::Request r;
        r.url = s.request.url;
        r.range = "0-" + std::to_string(kSniffBytes - 1);
        r.cancel = s.cancel;
        r.on_head = [&](const net::Head &head)
        {
            code = head.status;
            return head.status >= 200 && head.status < 300;
        };
        r.on_data = [&](const std::uint8_t *d, std::size_t n)
        {
            first.append(reinterpret_cast<const char *>(d),
                         std::min(n, kSniffBytes - first.size()));
            return first.size() < kSniffBytes;
        };
        const net::Response response = http.perform(r);
        {
            std::lock_guard<std::mutex> guard(status_lock_);
            status_.last_http_status = code;
        }
        if (s.stop.load())
        {
            s.finished.store(true);
            return;
        }
        if (code < 200 || code > 299 || (first.empty() && !response.ok()))
        {
            set_error("Could not open the address: " +
                      (code >= 300 ? "HTTP " + std::to_string(code) : response.describe()));
            s.finished.store(true);
            return;
        }
        s.request.kind = sniff_source(response.content_type, first);
        std::lock_guard<std::mutex> guard(status_lock_);
        status_.container = container_label(s.request.kind);
    }

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
            const hls::Playlist master = std::move(parsed.playlist);
            hls::Selection selection;
            selection.max_height = s.request.max_height;
            selection.max_width = s.request.max_height * 16 / 9 + 16;
            std::string why;
            const int index = hls::select_variant(master, selection, &why);
            if (index < 0)
            {
                set_error("No playable stream: " + why);
                s.finished.store(true);
                return;
            }
            add_notice(why);
            const hls::Variant &variant = master.variants[static_cast<std::size_t>(index)];
            {
                std::lock_guard<std::mutex> guard(status_lock_);
                status_.variant = describe_variant(variant);
                status_.variant_count = static_cast<int>(master.variants.size());
            }
            media_url = variant.uri;
            if (!master.unsupported.empty())
                add_notice(master.unsupported);
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
            // Audio in its own rendition (EXT-X-MEDIA TYPE=AUDIO with a URI).
            if (const hls::Rendition *rendition = hls::audio_rendition(master, variant))
            {
                std::string audio_body;
                auto audio = fetch(rendition->uri, &audio_body, &error)
                                 ? hls::parse(audio_body, rendition->uri)
                                 : hls::ParseResult{};
                if (audio.ok && audio.playlist.kind == hls::Kind::media &&
                    audio.playlist.unsupported.empty() && !audio.playlist.segments.empty())
                {
                    audio_url = rendition->uri;
                    audio_media = std::move(audio.playlist);
                }
                else if (!s.stop.load())
                {
                    const std::string reason =
                        !audio.ok ? (audio.error.empty() ? error : audio.error)
                        : !audio.playlist.unsupported.empty() ? audio.playlist.unsupported
                                                              : std::string{"not a media playlist"};
                    add_notice("the audio track could not be loaded (" + reason +
                               "); playing video only");
                }
            }
        }
        media = std::move(parsed.playlist);
        if (!media.unsupported.empty())
        {
            set_error("This stream cannot be played: " + media.unsupported);
            s.finished.store(true);
            return;
        }
        if (media.segments.empty() && !media.is_live())
        {
            set_error("This stream cannot be played: the playlist lists no segments");
            s.finished.store(true);
            return;
        }
        hls_via_ffmpeg = media.fragmented_mp4() || !audio_url.empty();
        s.live = media.is_live();
        s.duration = s.live ? 0.0 : media.total_duration;
        s.seekable = !s.live && media.total_duration > 0.0;
        std::lock_guard<std::mutex> guard(status_lock_);
        status_.live = s.live;
        status_.duration = s.duration;
        status_.seekable = s.seekable;
        status_.segments_total = static_cast<int>(media.segments.size());
        status_.container = media.fragmented_mp4() ? "HLS (fragmented MP4)" : "HLS (MPEG-TS)";
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

            // Remuxed sources (files, web files, fMP4/separate-audio HLS) run
            // through FFmpeg; update_base takes the first video time as the
            // position origin (after a seek in a file).
            const auto run_remux = [&](Remuxer &remux, bool update_base)
            {
                add_notice(remux.info().notice);
                const int outcome =
                    remux.run([&](const std::uint8_t *d, std::size_t n) { return push(d, n); },
                              s.stop_or_seek, &failure, update_base ? &s.base_seconds : nullptr);
                RunResult r = outcome == 1   ? RunResult::finished
                              : outcome == 0 ? RunResult::stopped
                                             : RunResult::failed;
                if (outcome != -1 && !failure.empty() && !interrupted())
                    r = RunResult::failed;
                return r;
            };
            const auto take_file_info = [&](const Remuxer &remux)
            {
                const RemuxInfo &info = remux.info();
                s.duration = info.duration;
                s.seekable = info.duration > 0.0 && remux.seekable();
                std::lock_guard<std::mutex> guard(status_lock_);
                status_.duration = s.duration;
                status_.seekable = s.seekable;
                status_.container = info.container;
            };

            if (s.request.kind == SourceKind::local_file || s.request.kind == SourceKind::http_file)
            {
                Remuxer remux;
                std::unique_ptr<ByteSource> input =
                    s.request.kind == SourceKind::local_file
                        ? open_file_source(s.request.url, &failure)
                        : open_http_source(s.request.url, s.stop_or_seek, transfer_observer,
                                           &failure);
                if (!input || !remux.open(std::move(input), nullptr, &failure))
                {
                    result = interrupted() ? RunResult::stopped : RunResult::failed;
                }
                else
                {
                    take_file_info(remux);
                    if (start > 0.0 && !remux.seek(start))
                    {
                        add_notice("this file cannot be positioned; playing from the start");
                        start = 0.0;
                    }
                    s.base_seconds.store(start);
                    result = run_remux(remux, true);
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
            else if (hls_via_ffmpeg)
            {
                // Fragmented MP4 and/or a separate audio rendition: FFmpeg
                // demuxes the renditions and remuxes them into one TS stream.
                double base = 0.0;
                auto video = open_hls_source(media_url, media, start, s.stop_or_seek, s.cancel,
                                             segment_observer, config_.live_edge_segments,
                                             config_.segment_retries, &base);
                std::unique_ptr<ByteSource> audio;
                if (!audio_url.empty())
                    audio = open_hls_source(audio_url, audio_media, start, s.stop_or_seek, s.cancel,
                                            segment_observer, config_.live_edge_segments,
                                            config_.segment_retries, nullptr);
                s.base_seconds.store(base);
                Remuxer remux;
                if (!remux.open(std::move(video), std::move(audio), &failure))
                    result = interrupted() ? RunResult::stopped : RunResult::failed;
                else
                    result = run_remux(remux, false);
            }
            else
            {
                // MPEG-TS segments straight into the native demuxer.
                SegmentCursor cursor(http, media_url, media, s.stop_or_seek,
                                     config_.live_edge_segments);
                SegmentFetcher fetcher(http, s.stop_or_seek, s.cancel, segment_observer,
                                       config_.segment_retries);
                s.base_seconds.store(cursor.start_at(start));
                bool first_segment = true;
                result = RunResult::finished;
                for (;;)
                {
                    SegmentRef ref;
                    std::string why;
                    const SegmentCursor::Next next = cursor.next(&ref, &why);
                    if (next == SegmentCursor::Next::end)
                        break;
                    if (next == SegmentCursor::Next::stopped)
                    {
                        result = RunResult::stopped;
                        break;
                    }
                    if (next == SegmentCursor::Next::failed)
                    {
                        failure = why;
                        result = RunResult::failed;
                        break;
                    }
                    if (ref.jumped)
                        (void)iptv_stream_discontinuity(&stream); // fell behind the live window
                    std::string body;
                    if (!fetcher.fetch(ref, &body, &why))
                    {
                        if (interrupted())
                            result = RunResult::stopped;
                        else
                        {
                            failure = why;
                            result = RunResult::failed;
                        }
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
                    if (ref.segment.discontinuity && !first_segment)
                        (void)iptv_stream_discontinuity(&stream);
                    first_segment = false;
                    if (!push(reinterpret_cast<const std::uint8_t *>(body.data()), body.size()))
                    {
                        result = interrupted() ? RunResult::stopped : RunResult::failed;
                        break;
                    }
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
                // A stop or seek cancels the decoder (-125); that is not an error.
                if (t->last_error[0] && result != RunResult::stopped && result != RunResult::seek)
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
                status_.frames_dropped = last.frames_dropped;
                status_.audio_underruns = last.audio_underruns;
                status_.audio_errors = last.audio_errors;
                status_.access_units = tap.video_units.load();
                status_.audio_frames = tap.audio_units.load();
                if (config_.frames)
                    status_.convert_us = config_.frames->last_convert_us();
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
