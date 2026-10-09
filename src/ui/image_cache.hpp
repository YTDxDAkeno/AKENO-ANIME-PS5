// AKENO STREAM PS5 - Asynchronous artwork loading and caching.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/jobs.hpp"
#include "gfx/surface.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>

namespace akeno::ui
{
class ImageCache final
{
  public:
    explicit ImageCache(Jobs &jobs, std::size_t budget_bytes = 160u * 1024u * 1024u);

    // The image cover-scaled to w x h once loaded; nullptr while loading or
    // after a failure. Call from the UI thread every frame the image is visible.
    const gfx::Image *get(const std::string &url, int w, int h);
    // Starts queued downloads (UI thread, once per frame).
    void tick();
    // True once after an image finished loading (a redraw is needed).
    bool take_changed();

    struct Stats
    {
        std::size_t images = 0;
        std::size_t bytes = 0;
        std::size_t loading = 0;
        std::size_t failed = 0;
    };
    [[nodiscard]] Stats stats() const;

    static constexpr int kMaxInFlight = 4;
    static constexpr std::size_t kMaxQueued = 48;

  private:
    enum class State : std::uint8_t
    {
        queued,
        loading,
        ready,
        failed,
    };
    struct Entry
    {
        State state = State::queued;
        gfx::Image image;
        std::string url;
        int w = 0, h = 0;
        std::uint64_t last_used = 0;
    };
    void evict();

    Jobs &jobs_;
    std::size_t budget_;
    std::size_t bytes_ = 0;
    std::uint64_t clock_ = 0;
    int in_flight_ = 0;
    bool changed_ = false;
    std::unordered_map<std::string, Entry> entries_;
    std::deque<std::string> queue_; // most recent requests at the front
};
} // namespace akeno::ui
