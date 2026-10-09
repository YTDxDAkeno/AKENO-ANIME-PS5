// AKENO STREAM PS5 - Controller-driven on-screen keyboard.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/keyboard.hpp"

#include "gfx/font.hpp"

#include <cstdio>

namespace akeno::ui
{
namespace
{
constexpr const char *kRows[] = {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm_.@"};
constexpr int kLetterRows = 4;
constexpr const char *kActions[] = {"Shift", "Space", "Delete", "Done"};
constexpr int kActionCount = 4;

// Removes the last UTF-8 code point.
void pop_code_point(std::string &text)
{
    while (!text.empty())
    {
        const unsigned char c = static_cast<unsigned char>(text.back());
        text.pop_back();
        if ((c & 0xc0) != 0x80)
            break;
    }
}
} // namespace

void Keyboard::open(std::string title, std::string initial, std::size_t max_length, bool secret)
{
    title_ = std::move(title);
    text_ = std::move(initial);
    max_length_ = max_length;
    secret_ = secret;
    shift_ = false;
    active_ = true;
    row_ = 1;
    column_ = 0;
}

int Keyboard::columns(int row) const noexcept
{
    return row < kLetterRows ? 10 : kActionCount;
}

void Keyboard::press()
{
    if (row_ < kLetterRows)
    {
        if (text_.size() >= max_length_)
            return;
        char c = kRows[row_][column_];
        if (shift_ && c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
        text_ += c;
        return;
    }
    switch (column_)
    {
    case 0:
        shift_ = !shift_;
        break;
    case 1:
        if (text_.size() < max_length_ && !text_.empty() && text_.back() != ' ')
            text_ += ' ';
        break;
    case 2:
        pop_code_point(text_);
        break;
    default:
        break;
    }
}

Keyboard::Result Keyboard::handle(input::Button button)
{
    if (!active_)
        return Result::none;
    switch (button)
    {
    case input::Button::up:
        if (row_ > 0)
        {
            const int from = row_;
            --row_;
            if (from == kLetterRows)
                column_ = column_ * 10 / kActionCount + 1;
        }
        return Result::changed;
    case input::Button::down:
        if (row_ < kLetterRows)
        {
            ++row_;
            if (row_ == kLetterRows)
                column_ = column_ * kActionCount / 10;
        }
        return Result::changed;
    case input::Button::left:
        column_ = (column_ + columns(row_) - 1) % columns(row_);
        return Result::changed;
    case input::Button::right:
        column_ = (column_ + 1) % columns(row_);
        return Result::changed;
    case input::Button::cross:
        if (row_ == kLetterRows && column_ == 3)
        {
            active_ = false;
            return Result::submitted;
        }
        press();
        return Result::changed;
    case input::Button::square:
        pop_code_point(text_);
        return Result::changed;
    case input::Button::triangle:
        if (text_.size() < max_length_ && !text_.empty() && text_.back() != ' ')
            text_ += ' ';
        return Result::changed;
    case input::Button::l2:
    case input::Button::l1:
        shift_ = !shift_;
        return Result::changed;
    case input::Button::options:
    case input::Button::r2:
        active_ = false;
        return Result::submitted;
    case input::Button::circle:
        active_ = false;
        return Result::cancelled;
    default:
        return Result::none;
    }
}

void Keyboard::render(Painter &p, Pixel accent)
{
    if (!active_)
        return;
    p.s.dim(p.s.bounds(), 150);
    const Rect panel{360, 330, 1200, 640};
    p.panel(panel, 28, theme::kSurface);
    p.text(panel.x + 48, panel.y + 34, title_, theme::kHeading, theme::kText);
    // Input field.
    const Rect field{panel.x + 48, panel.y + 100, panel.w - 96, 72};
    p.s.fill_rounded(field, 14, theme::kBackgroundTop);
    p.s.stroke_rounded(field, 14, 3, accent);
    std::string shown = text_;
    if (secret_ && shown.size() > 6)
        shown = shown.substr(0, 4) + std::string(shown.size() - 8, '*') +
                shown.substr(shown.size() - 4);
    const int tw = p.text(field.x + 24, field.y + 18, shown.empty() ? std::string{} : shown,
                          theme::kBody, theme::kText, field.w - 60);
    p.s.fill({field.x + 26 + tw, field.y + 16, 3, 40}, accent);
    char counter[32];
    std::snprintf(counter, sizeof(counter), "%zu/%zu", text_.size(), max_length_);
    p.text_right(field.right(), field.bottom() + 8, counter, theme::kSmall, theme::kTextMuted);

    // Keys.
    const int key_w = 98, key_h = 70, gap = 12;
    const int origin_x = panel.x + (panel.w - (10 * key_w + 9 * gap)) / 2;
    int y = field.bottom() + 40;
    for (int r = 0; r < kLetterRows; ++r)
    {
        for (int c = 0; c < 10; ++c)
        {
            const Rect key{origin_x + c * (key_w + gap), y, key_w, key_h};
            const bool focused = row_ == r && column_ == c;
            p.s.fill_rounded(key, 12, focused ? theme::kText : theme::kSurfaceRaised);
            char label[2] = {kRows[r][c], 0};
            if (shift_ && label[0] >= 'a' && label[0] <= 'z')
                label[0] = static_cast<char>(label[0] - 'a' + 'A');
            p.text_center(key.x + key.w / 2,
                          key.y + (key.h - p.line_height(theme::kSubheading)) / 2, label,
                          theme::kSubheading, focused ? theme::kTextOnAccent : theme::kText);
        }
        y += key_h + gap;
    }
    const int action_w = (10 * key_w + 9 * gap - 3 * gap) / kActionCount;
    for (int c = 0; c < kActionCount; ++c)
    {
        const Rect key{origin_x + c * (action_w + gap), y, action_w, key_h};
        const bool focused = row_ == kLetterRows && column_ == c;
        const bool active_shift = c == 0 && shift_;
        const Pixel bg =
            focused ? theme::kText
                    : (c == 3 ? accent
                              : (active_shift ? theme::kSurfaceHighlight : theme::kSurfaceRaised));
        p.s.fill_rounded(key, 12, bg);
        const Pixel fg = focused || c == 3 ? theme::kTextOnAccent : theme::kText;
        p.text_center(key.x + key.w / 2, key.y + (key.h - p.line_height(theme::kBodyStrong)) / 2,
                      kActions[c], theme::kBodyStrong, fg);
    }
    // Hints.
    int hx = panel.x + 48;
    const int hy = panel.bottom() + 16;
    hx += p.hint(hx, hy, Glyph::square, "Delete") + 30;
    hx += p.hint(hx, hy, Glyph::triangle, "Space") + 30;
    hx += p.hint(hx, hy, Glyph::l1, "Shift") + 30;
    hx += p.hint(hx, hy, Glyph::options, "Done") + 30;
    p.hint(hx, hy, Glyph::circle, "Cancel");
}
} // namespace akeno::ui
