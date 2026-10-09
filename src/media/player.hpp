// AKENO STREAM PS5 - Playback engine.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One worker thread per playback session reads the source (HLS over HTTP(S)
// or a local file remuxed by FFmpeg), feeds MPEG-TS bytes to the vendored
// ProsperoTV demuxer (iptv_stream), which hands complete access units to a
// DecodeSink. On the console the sink is the hardware decoder backend
// (Videodec2 + Audiodec/AudioOut, paced to presentation time) and pictures
// arrive in a FrameStore for the interface to composite. The UI thread only
// calls the non-blocking control methods and reads status snapshots.
#pragma once

#include "media/frame_store.hpp"
#include "net/http.hpp"

#include "iptv_stream.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace akeno::media
{
enum class SourceKind : std::uint8_t
{
    hls,        // http(s) URL of a master or media playlist
    http_ts,    // http(s) URL of a single MPEG-TS file
    local_file, // path of a .ts/.mp4/.mkv/.m4v on console storage
};

struct PlayRequest
{
    std::string url;
    SourceKind kind = SourceKind::hls;
    std::string title;
    std::string subtitle;
    std::string item_key; // provider:id, for resume/history
    double start_seconds = 0.0;
    int max_height = 1080; // quality cap for HLS variant selection
};

enum class PlayerState : std::uint8_t
{
    idle,
    opening,
    buffering,
    playing,
    paused,
    seeking,
    ended,
    stopped,
    error,
};
const char *state_name(PlayerState state) noexcept;

// Everything the player and diagnostics screens show. Copyable snapshot.
struct PlayerStatus
{
    PlayerState state = PlayerState::idle;
    std::string title;
    std::string error;      // set in PlayerState::error
    std::string notice;     // non-fatal limitation (e.g. no audio)
    std::string source_url; // redacted
    double position = 0.0;  // seconds
    double duration = 0.0;  // 0 when unknown/live
    bool live = false;
    bool seekable = false;

    // Stream
    std::string container = "MPEG-TS";
    std::string video_codec;
    std::string audio_codec;
    int width = 0, height = 0;
    int audio_rate = 0, audio_channels = 0;
    std::string variant; // "1280x720 @ 2.1 Mbps"
    int variant_count = 0;

    // Network
    long last_http_status = 0;
    std::uint64_t bytes_downloaded = 0;
    std::uint32_t throughput_kbps = 0;
    int segments_loaded = 0;
    int segments_total = 0;
    int retries = 0;

    // Decode / presentation (from the sink)
    std::uint64_t access_units = 0;
    std::uint64_t audio_frames = 0;
    std::uint64_t frames_decoded = 0;
    std::uint64_t frames_presented = 0;
    std::uint64_t frames_dropped = 0;
    std::uint32_t fps_x100 = 0;
    std::uint64_t decoder_errors = 0;
    std::int32_t last_native_result = 0;
    std::uint64_t audio_underruns = 0;
    std::uint64_t audio_errors = 0;
    std::uint32_t convert_us = 0;
    std::string decoder; // "PS5 hardware (Videodec2)" ...
    std::string demux_error;
};

// The decode backend behind iptv_stream. Implementations: the PS5 hardware
// sink (src/media/native) and test sinks.
class DecodeSink
{
  public:
    virtual ~DecodeSink() = default;
    // Called on the worker thread before the stream opens.
    virtual bool init(std::string *error) = 0;
    // iptv_stream callbacks bound to this sink.
    virtual iptv_stream_backend_t callbacks() = 0;
    // Thread-safe controls.
    virtual void set_paused(bool paused) = 0;
    virtual void request_stop() = 0; // releases blocked submissions
    // Presentation clock: PTS of the newest presented picture, or UINT64_MAX.
    [[nodiscard]] virtual std::uint64_t presented_pts_us() const = 0;
    virtual void fill_status(PlayerStatus &status) const = 0;
    // Worker thread, after iptv_stream_cleanup: release native resources.
    virtual void shutdown() = 0;
};

using SinkFactory = std::function<std::unique_ptr<DecodeSink>()>;

struct PlayerConfig
{
    SinkFactory make_sink;
    std::shared_ptr<FrameStore> frames;
    // HLS live edge: start this many segments before the newest.
    int live_edge_segments = 3;
    int segment_retries = 3;
};

class Player final
{
  public:
    explicit Player(PlayerConfig config);
    ~Player();
    Player(const Player &) = delete;
    Player &operator=(const Player &) = delete;

    // Starts playback, stopping any previous session first.
    void play(const PlayRequest &request);
    void toggle_pause();
    void seek_to(double seconds);
    void seek_by(double delta_seconds);
    // Stops and joins the worker. Safe to call repeatedly.
    void stop();

    [[nodiscard]] PlayerStatus status() const;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] const std::shared_ptr<FrameStore> &frames() const noexcept
    {
        return config_.frames;
    }

  private:
    struct Session;
    static void *thread_entry(void *self);
    void run();

    PlayerConfig config_;
    std::unique_ptr<Session> session_;
    mutable std::mutex status_lock_;
    PlayerStatus status_;
};
} // namespace akeno::media
