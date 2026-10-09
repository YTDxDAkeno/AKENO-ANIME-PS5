// AKENO STREAM PS5 - Full-screen output of composed frames.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gfx/surface.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace akeno::platform
{
class Display final
{
  public:
    static constexpr int kWidth = 1920;
    static constexpr int kHeight = 1080;

    Display();
    ~Display();
    Display(const Display &) = delete;
    Display &operator=(const Display &) = delete;

    // Opens VideoOut and registers the scan-out buffers.
    bool open(std::string *error);
    // Copies a finished 1920x1080 frame to the next scan-out buffer and
    // queues it for the next vertical blank.
    void present(const gfx::Surface &frame);
    // Blocks until the next vertical blank (paces the main loop at 60 Hz).
    void wait_vblank();
    [[nodiscard]] std::uint64_t presented_frames() const noexcept;
    [[nodiscard]] std::uint32_t last_present_us() const noexcept; // copy + flush cost

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace akeno::platform
