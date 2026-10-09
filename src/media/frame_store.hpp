// AKENO STREAM PS5 - Hand-off of decoded video between decoder and UI threads.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Triple buffer: the decoder converts each picture into the write slot and
// publishes it; the UI thread takes the newest published slot when it draws.
// Neither side waits for the other beyond swapping two indices, so a slow
// interface frame never stalls the paced decoder and vice versa.
#pragma once

#include "gfx/surface.hpp"
#include "gfx/yuv.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace akeno::media
{
class FrameStore final
{
  public:
    FrameStore(int width, int height);

    // Decoder thread: converts a picture into the write slot and publishes it.
    bool publish(const gfx::Nv12Picture &picture, std::uint64_t pts_us);
    // UI thread: draws the newest frame (or black if none) into dst.
    // Returns true if a frame was available.
    bool draw(gfx::Surface &surface, const gfx::Rect &dst);
    // Forget all frames (new session); the next draw shows black.
    void clear();

    [[nodiscard]] std::uint64_t serial() const noexcept
    {
        return serial_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint32_t last_convert_us() const noexcept
    {
        return convert_us_.load();
    }
    [[nodiscard]] std::uint32_t max_convert_us() const noexcept
    {
        return convert_max_us_.load();
    }
    [[nodiscard]] int width() const noexcept
    {
        return width_;
    }
    [[nodiscard]] int height() const noexcept
    {
        return height_;
    }

  private:
    int width_, height_;
    std::array<gfx::Image, 3> slots_;
    int write_ = 0, ready_ = 1, front_ = 2;
    bool fresh_ = false; // ready_ holds a frame newer than front_
    bool front_valid_ = false;
    std::mutex lock_;
    std::atomic<std::uint64_t> serial_{0};
    std::atomic<std::uint32_t> convert_us_{0};
    std::atomic<std::uint32_t> convert_max_us_{0};
};
} // namespace akeno::media
