// AKENO STREAM PS5 - Hand-off of decoded video between decoder and UI threads.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/frame_store.hpp"

#include "platform/platform.hpp"

#include <cstring>
#include <utility>

namespace akeno::media
{
FrameStore::FrameStore(int width, int height) : width_{width}, height_{height}
{
    for (auto &slot : slots_)
        slot.resize(width, height, 0xff000000u);
}

bool FrameStore::publish(const gfx::Nv12Picture &picture, std::uint64_t pts_us)
{
    (void)pts_us;
    const std::uint64_t started = platform::monotonic_us();
    // write_ is owned by this thread between swaps.
    if (!gfx::convert_nv12(picture, slots_[static_cast<std::size_t>(write_)]))
        return false;
    {
        std::lock_guard<std::mutex> guard(lock_);
        std::swap(write_, ready_);
        fresh_ = true;
    }
    serial_.fetch_add(1, std::memory_order_acq_rel);
    const auto elapsed = static_cast<std::uint32_t>(platform::monotonic_us() - started);
    convert_us_.store(elapsed);
    if (elapsed > convert_max_us_.load())
        convert_max_us_.store(elapsed);
    return true;
}

bool FrameStore::draw(gfx::Surface &surface, const gfx::Rect &dst)
{
    {
        std::lock_guard<std::mutex> guard(lock_);
        if (fresh_)
        {
            std::swap(front_, ready_);
            fresh_ = false;
            front_valid_ = true;
        }
    }
    const gfx::Image &frame = slots_[static_cast<std::size_t>(front_)];
    if (!front_valid_)
    {
        surface.fill(dst, 0xff000000u);
        return false;
    }
    if (dst.x == 0 && dst.y == 0 && dst.w == frame.width && dst.h == frame.height &&
        surface.width() >= frame.width && surface.height() >= frame.height)
    {
        // Full-screen fast path: straight row copies.
        for (int y = 0; y < frame.height; ++y)
            std::memcpy(surface.row(y), &frame.pixels[static_cast<std::size_t>(y) * frame.width],
                        static_cast<std::size_t>(frame.width) * sizeof(gfx::Pixel));
    }
    else
    {
        surface.draw_image_scaled(frame, dst);
    }
    return true;
}

void FrameStore::clear()
{
    std::lock_guard<std::mutex> guard(lock_);
    fresh_ = false;
    front_valid_ = false;
}
} // namespace akeno::media
