// AKENO STREAM PS5 - DualSense access.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/input.hpp"

#include <vector>

namespace akeno::platform
{
class Pad final
{
  public:
    // Opens the initial user's controller. Safe to call again to reconnect.
    bool open() noexcept;
    // Reads every queued sample and appends UI events.
    void poll(std::uint64_t now_ms, std::vector<input::Event> &events);
    [[nodiscard]] bool connected() const noexcept
    {
        return mapper_.connected();
    }
    [[nodiscard]] int handle() const noexcept
    {
        return handle_;
    }

  private:
    int handle_ = -1;
    int user_ = -1;
    std::uint64_t next_reopen_ms_ = 0;
    input::Mapper mapper_;
};
} // namespace akeno::platform
