// Akeno Anime - open test-stream decoder and frame presenter (v0.3 experimental).
// Copyright (C) 2026 Akeno Anime contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "demo_renderer.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <pthread.h>

namespace akeno
{
enum class VideoStage : std::uint8_t
{
    idle,
    fetching,
    demuxing,
    playing,
    paused,
    finished,
    stopped,
    error,
};

struct VideoSnapshot
{
    VideoStage stage;
    unsigned frames;
    int error;
    int http;
    unsigned segment_bytes;
    int ts_sync;
    int demuxer;
    int av_error;
    int stream_info_error;
};

// Downloads one small, public, unencrypted MPEG-TS HLS segment and decodes it.
// Audio is deliberately not implemented in this experiment.
class VideoPreview final
{
  public:
    VideoPreview() noexcept;
    VideoPreview(const VideoPreview &) = delete;
    VideoPreview &operator=(const VideoPreview &) = delete;

    bool start() noexcept;
    void toggle_pause() noexcept;
    void stop() noexcept;
    VideoSnapshot snapshot() const noexcept;
    void draw_frame(ps5::demo::Canvas &canvas) noexcept;

  private:
    static void *worker_entry(void *argument) noexcept;
    void run() noexcept;
    bool should_stop() const noexcept;

    static constexpr unsigned kWidth = 320;
    static constexpr unsigned kHeight = 180;
    static constexpr std::size_t kFrameBytes = kWidth * kHeight * 4u;
    pthread_mutex_t mutex_{};
    std::uint8_t pixels_[kFrameBytes]{};
    bool frame_ready_ = false;
    std::atomic<VideoStage> stage_{VideoStage::idle};
    std::atomic<unsigned> frames_{0};
    std::atomic<int> error_{0};
    std::atomic<int> http_{0};
    std::atomic<unsigned> segment_bytes_{0};
    std::atomic<int> ts_sync_{0};
    std::atomic<int> demuxer_{0};
    std::atomic<int> av_error_{0};
    std::atomic<int> stream_info_error_{0};
    std::atomic<bool> stop_{false};
    std::atomic<bool> paused_{false};
};
} // namespace akeno
