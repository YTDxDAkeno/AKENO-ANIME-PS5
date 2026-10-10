// AKENO STREAM PS5 - Controller-driven on-screen keyboard.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/input.hpp"
#include "ui/painter.hpp"

#include <string>

namespace akeno::ui
{
class Keyboard final
{
  public:
    enum class Result : std::uint8_t
    {
        none,
        changed,
        submitted,
        cancelled,
    };

    void open(std::string title, std::string initial, std::size_t max_length, bool secret = false);
    [[nodiscard]] bool active() const noexcept
    {
        return active_;
    }
    void close() noexcept
    {
        active_ = false;
    }
    [[nodiscard]] const std::string &text() const noexcept
    {
        return text_;
    }

    Result handle(input::Button button);
    // Draws the keyboard panel in the middle of the screen.
    void render(Painter &p, Pixel accent);
    // Exposed for tests.
    [[nodiscard]] int row() const noexcept
    {
        return row_;
    }
    [[nodiscard]] int column() const noexcept
    {
        return column_;
    }

    [[nodiscard]] bool symbols() const noexcept
    {
        return symbols_;
    }

  private:
    [[nodiscard]] int columns(int row) const noexcept;
    void press();

    bool active_ = false;
    bool shift_ = false;
    bool symbols_ = false;
    bool secret_ = false;
    std::string title_;
    std::string text_;
    std::size_t max_length_ = 64;
    int row_ = 1, column_ = 0;
};

// What a secret field shows: the first and last four characters of long
// values (so a pasted key can be checked), stars for the rest.
std::string mask_secret(const std::string &text);
} // namespace akeno::ui
