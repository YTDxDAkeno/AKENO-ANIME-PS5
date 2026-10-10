// AKENO STREAM PS5 - Widget drawing on top of the rasterizer and fonts.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gfx/font.hpp"
#include "gfx/surface.hpp"
#include "ui/theme.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace akeno
{
struct MediaItem;
}

namespace akeno::ui
{
using gfx::Pixel;
using gfx::Rect;

enum class Glyph : std::uint8_t
{
    cross,
    circle,
    square,
    triangle,
    l1,
    r1,
    l2,
    r2,
    options,
    dpad,
};

enum class Icon : std::uint8_t
{
    none,
    play,
    pause,
    stop,
    search,
    star,
    star_outline,
    folder,
    film,
    gear,
    home,
    alert,
    check,
    qr,
    info,
    usb,
    rewind,
    forward,
    refresh,
    key,
    lock,
    tv,
    spark,
};

class Painter final
{
  public:
    Painter(gfx::Surface &surface, gfx::FontEngine &fonts) : s{surface}, fonts{fonts}
    {
    }

    gfx::Surface &s;
    gfx::FontEngine &fonts;

    // Text. y is the top of the line box. Returns the drawn width.
    int text(int x, int y, std::string_view value, const theme::Type &type, Pixel color,
             int max_width = 0);
    int text_right(int right, int y, std::string_view value, const theme::Type &type, Pixel color);
    int text_center(int center, int y, std::string_view value, const theme::Type &type, Pixel color,
                    int max_width = 0);
    int measure(std::string_view value, const theme::Type &type);
    int line_height(const theme::Type &type) const
    {
        return fonts.line_height(type.size);
    }
    // Returns the height used.
    int wrapped(int x, int y, std::string_view value, const theme::Type &type, Pixel color,
                int width, int max_lines, int spacing = 4);

    // Backdrop: vertical gradient with a soft glow in the mode's accent colour.
    void background(Pixel accent);
    void panel(const Rect &r, int radius = theme::kPanelRadius, Pixel color = theme::kSurface);
    void focus_ring(const Rect &r, int radius, Pixel color = theme::kFocus);
    // Pill button with optional icon. Returns its width.
    int button(int x, int y, std::string_view label, bool focused, Pixel accent,
               Icon icon = Icon::none, int height = 64);
    int button_width(std::string_view label, Icon icon = Icon::none, int height = 64);
    // Small rounded label; returns its width.
    int chip(int x, int y, std::string_view label, Pixel background, Pixel foreground);
    void progress(const Rect &r, double fraction, Pixel fill, Pixel track);
    void spinner(int cx, int cy, int radius, std::uint64_t now_ms, Pixel color);
    void glyph(Glyph glyph, int cx, int cy, int size);
    // Controller hint: glyph followed by a label. Returns its width.
    int hint(int x, int y, Glyph glyph, std::string_view label);
    void icon(Icon icon, int cx, int cy, int size, Pixel color);

    // Artwork card. art may be null (placeholder drawn from the item's accent).
    // icon (optional) is a small site icon drawn centred on the placeholder.
    void media_card(const Rect &image, const MediaItem &item, const gfx::Image *art, bool focused,
                    double progress, bool show_text = true, const gfx::Image *icon = nullptr);
    // Placeholder artwork for items without images.
    void placeholder(const Rect &r, const std::string &title, std::uint32_t accent, int radius);

    // Status text with a coloured dot.
    int status_line(int x, int y, Pixel dot, std::string_view label, const theme::Type &type,
                    Pixel color);
};

// Stable colour for a string (used for placeholders).
std::uint32_t accent_for(const std::string &text);
} // namespace akeno::ui
