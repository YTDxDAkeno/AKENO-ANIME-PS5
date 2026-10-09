// AKENO STREAM PS5 - Controller input: raw DualSense samples to UI events.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/input.hpp"

namespace akeno::input
{
namespace
{
struct Mapping
{
    std::uint32_t bit;
    Button button;
};

constexpr Mapping kButtons[] = {
    {bits::cross, Button::cross},     {bits::circle, Button::circle},
    {bits::square, Button::square},   {bits::triangle, Button::triangle},
    {bits::l1, Button::l1},           {bits::r1, Button::r1},
    {bits::l2, Button::l2},           {bits::r2, Button::r2},
    {bits::options, Button::options}, {bits::touchpad, Button::touchpad},
    {bits::l3, Button::l3},           {bits::r3, Button::r3},
};

int direction_of(const Sample &s) noexcept
{
    if (s.buttons & bits::up)
        return static_cast<int>(Button::up);
    if (s.buttons & bits::down)
        return static_cast<int>(Button::down);
    if (s.buttons & bits::left)
        return static_cast<int>(Button::left);
    if (s.buttons & bits::right)
        return static_cast<int>(Button::right);
    const int dx = static_cast<int>(s.left_x) - 128;
    const int dy = static_cast<int>(s.left_y) - 128;
    const int ax = dx < 0 ? -dx : dx;
    const int ay = dy < 0 ? -dy : dy;
    if (ax < Mapper::kStickDeadzone && ay < Mapper::kStickDeadzone)
        return -1;
    if (ax > ay)
        return static_cast<int>(dx < 0 ? Button::left : Button::right);
    return static_cast<int>(dy < 0 ? Button::up : Button::down);
}
} // namespace

void Mapper::reset() noexcept
{
    previous_ = 0;
    held_direction_ = -1;
    held_since_ = 0;
    next_repeat_ = 0;
}

void Mapper::feed(const Sample &sample, std::uint64_t now_ms, std::vector<Event> &out)
{
    if (!sample.connected)
    {
        connected_ = false;
        reset();
        return;
    }
    connected_ = true;
    if (sample.buttons & bits::intercepted)
    {
        // Treat everything as released so nothing fires when focus returns.
        reset();
        previous_ = sample.buttons & ~bits::intercepted;
        return;
    }
    const std::uint32_t pressed = sample.buttons & ~previous_;
    previous_ = sample.buttons;
    for (const Mapping &m : kButtons)
        if (pressed & m.bit)
            out.push_back({m.button, false});

    const int direction = direction_of(sample);
    if (direction != held_direction_)
    {
        held_direction_ = direction;
        if (direction >= 0)
        {
            out.push_back({static_cast<Button>(direction), false});
            held_since_ = now_ms;
            next_repeat_ = now_ms + kRepeatDelayMs;
        }
        return;
    }
    tick(now_ms, out);
}

void Mapper::tick(std::uint64_t now_ms, std::vector<Event> &out)
{
    if (held_direction_ < 0 || now_ms < next_repeat_)
        return;
    out.push_back({static_cast<Button>(held_direction_), true});
    const std::uint64_t interval =
        now_ms - held_since_ >= kFastAfterMs ? kFastIntervalMs : kRepeatIntervalMs;
    next_repeat_ = now_ms + interval;
}
} // namespace akeno::input
