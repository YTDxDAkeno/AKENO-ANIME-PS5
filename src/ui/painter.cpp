// AKENO STREAM PS5 - Widget drawing on top of the rasterizer and fonts.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/painter.hpp"

#include "providers/model.hpp"

#include <cmath>

namespace akeno::ui
{
namespace
{
constexpr float kPi = 3.14159265f;

void thick_line(gfx::Surface &s, float x0, float y0, float x1, float y1, float width, Pixel color)
{
    const float dx = x1 - x0, dy = y1 - y0;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.01f)
        return;
    const float nx = -dy / length * width * 0.5f, ny = dx / length * width * 0.5f;
    const int ax = static_cast<int>(std::lround(x0 + nx)),
              ay = static_cast<int>(std::lround(y0 + ny));
    const int bx = static_cast<int>(std::lround(x1 + nx)),
              by = static_cast<int>(std::lround(y1 + ny));
    const int cx = static_cast<int>(std::lround(x1 - nx)),
              cy = static_cast<int>(std::lround(y1 - ny));
    const int ex = static_cast<int>(std::lround(x0 - nx)),
              ey = static_cast<int>(std::lround(y0 - ny));
    s.fill_triangle(ax, ay, bx, by, cx, cy, color);
    s.fill_triangle(ax, ay, cx, cy, ex, ey, color);
}

void ring(gfx::Surface &s, int cx, int cy, int radius, int thickness, Pixel color)
{
    // Approximate with short thick segments.
    const int segments = std::max(24, radius * 2);
    for (int i = 0; i < segments; ++i)
    {
        const float a0 = 2 * kPi * i / segments, a1 = 2 * kPi * (i + 1) / segments;
        thick_line(s, cx + radius * std::cos(a0), cy + radius * std::sin(a0),
                   cx + radius * std::cos(a1), cy + radius * std::sin(a1),
                   static_cast<float>(thickness), color);
    }
}

void star(gfx::Surface &s, int cx, int cy, int size, Pixel color, bool outline)
{
    const float outer = size * 0.5f, inner = size * 0.21f;
    float vx[10], vy[10];
    for (int i = 0; i < 10; ++i)
    {
        const float a = -kPi / 2 + i * kPi / 5;
        const float r = (i % 2 == 0) ? outer : inner;
        vx[i] = cx + r * std::cos(a);
        vy[i] = cy + r * std::sin(a);
    }
    for (int i = 0; i < 10; ++i)
    {
        const int j = (i + 1) % 10;
        if (outline)
            thick_line(s, vx[i], vy[i], vx[j], vy[j], std::max(2.0f, size * 0.07f), color);
        else
            s.fill_triangle(cx, cy, static_cast<int>(vx[i]), static_cast<int>(vy[i]),
                            static_cast<int>(vx[j]), static_cast<int>(vy[j]), color);
    }
}
} // namespace

std::uint32_t accent_for(const std::string &text)
{
    static constexpr std::uint32_t palette[] = {0x4f8cff, 0xff7a3d, 0x35c79a, 0x9b7bff, 0xff3d5a,
                                                0x2bb5a8, 0xf2a33a, 0x5aa9ff, 0xe056fd, 0x22a6b3};
    std::uint32_t h = 2166136261u;
    for (unsigned char c : text)
        h = (h ^ c) * 16777619u;
    return palette[h % (sizeof(palette) / sizeof(palette[0]))];
}

int Painter::text(int x, int y, std::string_view value, const theme::Type &type, Pixel color,
                  int max_width)
{
    return fonts.draw(s, x, y, value, {type.size, type.weight, color}, max_width);
}

int Painter::measure(std::string_view value, const theme::Type &type)
{
    return fonts.measure(value, {type.size, type.weight, 0});
}

int Painter::text_right(int right, int y, std::string_view value, const theme::Type &type,
                        Pixel color)
{
    const int w = measure(value, type);
    return text(right - w, y, value, type, color);
}

int Painter::text_center(int center, int y, std::string_view value, const theme::Type &type,
                         Pixel color, int max_width)
{
    int w = measure(value, type);
    if (max_width > 0 && w > max_width)
        w = max_width;
    return text(center - w / 2, y, value, type, color, max_width);
}

int Painter::wrapped(int x, int y, std::string_view value, const theme::Type &type, Pixel color,
                     int width, int max_lines, int spacing)
{
    return fonts.draw_wrapped(s, x, y, value, {type.size, type.weight, color}, width, max_lines,
                              spacing);
}

void Painter::background(Pixel accent)
{
    s.gradient_vertical(s.bounds(), theme::kBackgroundTop, theme::kBackgroundBottom);
    // Soft accent glow in the top-left corner, drawn as stacked translucent discs.
    for (int i = 0; i < 6; ++i)
        s.fill_circle(220, 40, 640 - i * 90,
                      gfx::with_alpha(accent, static_cast<std::uint8_t>(5 + i * 2)));
}

void Painter::panel(const Rect &r, int radius, Pixel color)
{
    s.fill_rounded(r, radius, color);
}

void Painter::focus_ring(const Rect &r, int radius, Pixel color)
{
    const int t = theme::kFocusThickness;
    s.stroke_rounded(r.inset(-t - 3), radius + t + 3, t, color);
    // A faint outer glow.
    s.stroke_rounded(r.inset(-t - 7), radius + t + 7, 4, gfx::with_alpha(color, 60));
}

int Painter::button_width(std::string_view label, Icon icon_kind, int height)
{
    const int icon_space = icon_kind == Icon::none ? 0 : height / 2 + 14;
    return measure(label, theme::kBodyStrong) + icon_space + height;
}

int Painter::button(int x, int y, std::string_view label, bool focused, Pixel accent,
                    Icon icon_kind, int height)
{
    const int w = button_width(label, icon_kind, height);
    const Rect r{x, y, w, height};
    if (focused)
    {
        s.fill_rounded(r, height / 2, theme::kText);
        focus_ring(r, height / 2, accent);
    }
    else
    {
        s.fill_rounded(r, height / 2, gfx::with_alpha(theme::kSurfaceHighlight, 230));
    }
    const Pixel fg = focused ? theme::kTextOnAccent : theme::kText;
    int tx = x + height / 2;
    if (icon_kind != Icon::none)
    {
        icon(icon_kind, tx + height / 4 - 4, y + height / 2, height / 2, fg);
        tx += height / 2 + 14;
    }
    text(tx, y + (height - line_height(theme::kBodyStrong)) / 2, label, theme::kBodyStrong, fg);
    return w;
}

int Painter::chip(int x, int y, std::string_view label, Pixel background_color, Pixel foreground)
{
    const int w = measure(label, theme::kSmall) + 24;
    const int h = line_height(theme::kSmall) + 8;
    s.fill_rounded({x, y, w, h}, h / 2, background_color);
    text(x + 12, y + 4, label, theme::kSmall, foreground);
    return w;
}

void Painter::progress(const Rect &r, double fraction, Pixel fill, Pixel track)
{
    if (!(fraction >= 0.0))
        fraction = 0.0;
    if (fraction > 1.0)
        fraction = 1.0;
    s.fill_rounded(r, r.h / 2, track);
    const int w = static_cast<int>(r.w * fraction);
    if (w > 0)
        s.fill_rounded({r.x, r.y, std::max(w, r.h), r.h}, r.h / 2, fill);
}

void Painter::spinner(int cx, int cy, int radius, std::uint64_t now_ms, Pixel color)
{
    const int dots = 10;
    const float phase = static_cast<float>(now_ms % 1000) / 1000.0f * dots;
    for (int i = 0; i < dots; ++i)
    {
        const float a = 2 * kPi * i / dots - kPi / 2;
        float age = phase - i;
        while (age < 0)
            age += dots;
        const auto alpha = static_cast<std::uint8_t>(255 - std::min(220.0f, age * 26.0f));
        s.fill_circle(cx + static_cast<int>(radius * std::cos(a)),
                      cy + static_cast<int>(radius * std::sin(a)), std::max(3, radius / 6),
                      gfx::with_alpha(color, alpha));
    }
}

void Painter::glyph(Glyph g, int cx, int cy, int size)
{
    const int r = size / 2;
    switch (g)
    {
    case Glyph::cross:
    case Glyph::circle:
    case Glyph::square:
    case Glyph::triangle:
    {
        s.fill_circle(cx, cy, r, theme::kSurfaceHighlight);
        const float k = size * 0.22f;
        const float w = std::max(2.0f, size * 0.09f);
        if (g == Glyph::cross)
        {
            thick_line(s, cx - k, cy - k, cx + k, cy + k, w, theme::kCross);
            thick_line(s, cx - k, cy + k, cx + k, cy - k, w, theme::kCross);
        }
        else if (g == Glyph::circle)
        {
            ring(s, cx, cy, static_cast<int>(size * 0.24f), static_cast<int>(w), theme::kCircle);
        }
        else if (g == Glyph::square)
        {
            const int h = static_cast<int>(size * 0.21f);
            s.stroke_rounded({cx - h, cy - h, h * 2, h * 2}, 2, static_cast<int>(w),
                             theme::kSquare);
        }
        else
        {
            const float t = size * 0.26f;
            thick_line(s, cx, cy - t, cx + t, cy + t * 0.7f, w, theme::kTriangle);
            thick_line(s, cx + t, cy + t * 0.7f, cx - t, cy + t * 0.7f, w, theme::kTriangle);
            thick_line(s, cx - t, cy + t * 0.7f, cx, cy - t, w, theme::kTriangle);
        }
        break;
    }
    case Glyph::l1:
    case Glyph::r1:
    case Glyph::l2:
    case Glyph::r2:
    case Glyph::options:
    {
        const char *label = g == Glyph::l1   ? "L1"
                            : g == Glyph::r1 ? "R1"
                            : g == Glyph::l2 ? "L2"
                            : g == Glyph::r2 ? "R2"
                                             : "";
        const int w = g == Glyph::options ? size : static_cast<int>(size * 1.35f);
        const Rect pill{cx - w / 2, cy - r * 7 / 10, w, r * 14 / 10};
        s.fill_rounded(pill, pill.h / 3, theme::kSurfaceHighlight);
        if (g == Glyph::options)
        {
            for (int i = -1; i <= 1; ++i)
                s.fill({cx - size / 4, cy + i * size / 7 - 1, size / 2, 3}, theme::kText);
        }
        else
        {
            const theme::Type type{std::max(12, size * 9 / 20), gfx::Weight::bold};
            text_center(cx, cy - line_height(type) / 2, label, type, theme::kText);
        }
        break;
    }
    case Glyph::dpad:
    {
        const int arm = size / 5;
        s.fill_rounded({cx - arm, cy - r + 2, arm * 2, size - 4}, 3, theme::kSurfaceHighlight);
        s.fill_rounded({cx - r + 2, cy - arm, size - 4, arm * 2}, 3, theme::kSurfaceHighlight);
        break;
    }
    }
}

int Painter::hint(int x, int y, Glyph g, std::string_view label)
{
    const int size = 34;
    const int glyph_w =
        (g == Glyph::l1 || g == Glyph::r1 || g == Glyph::l2 || g == Glyph::r2) ? 46 : size;
    glyph(g, x + glyph_w / 2, y + size / 2, size);
    const int tw = text(x + glyph_w + 10, y + (size - line_height(theme::kCaption)) / 2, label,
                        theme::kCaption, theme::kTextSecondary);
    return glyph_w + 10 + tw;
}

void Painter::icon(Icon kind, int cx, int cy, int size, Pixel color)
{
    const float h = size * 0.5f;
    const float w = std::max(2.0f, size * 0.11f);
    switch (kind)
    {
    case Icon::none:
        break;
    case Icon::play:
        s.fill_triangle(static_cast<int>(cx - h * 0.62f), static_cast<int>(cy - h * 0.78f),
                        static_cast<int>(cx - h * 0.62f), static_cast<int>(cy + h * 0.78f),
                        static_cast<int>(cx + h * 0.82f), cy, color);
        break;
    case Icon::pause:
        s.fill_rounded({static_cast<int>(cx - h * 0.62f), static_cast<int>(cy - h * 0.72f),
                        static_cast<int>(h * 0.42f), static_cast<int>(h * 1.44f)},
                       2, color);
        s.fill_rounded({static_cast<int>(cx + h * 0.2f), static_cast<int>(cy - h * 0.72f),
                        static_cast<int>(h * 0.42f), static_cast<int>(h * 1.44f)},
                       2, color);
        break;
    case Icon::stop:
        s.fill_rounded({static_cast<int>(cx - h * 0.62f), static_cast<int>(cy - h * 0.62f),
                        static_cast<int>(h * 1.24f), static_cast<int>(h * 1.24f)},
                       3, color);
        break;
    case Icon::rewind:
    case Icon::forward:
    {
        const int dir = kind == Icon::forward ? 1 : -1;
        for (int k = 0; k < 2; ++k)
        {
            const float ox = (k == 0 ? -0.45f : 0.35f) * h * dir;
            s.fill_triangle(
                static_cast<int>(cx + ox - dir * h * 0.4f), static_cast<int>(cy - h * 0.6f),
                static_cast<int>(cx + ox - dir * h * 0.4f), static_cast<int>(cy + h * 0.6f),
                static_cast<int>(cx + ox + dir * h * 0.5f), cy, color);
        }
        break;
    }
    case Icon::search:
        ring(s, static_cast<int>(cx - h * 0.18f), static_cast<int>(cy - h * 0.18f),
             static_cast<int>(h * 0.52f), static_cast<int>(w), color);
        thick_line(s, cx + h * 0.22f, cy + h * 0.22f, cx + h * 0.78f, cy + h * 0.78f, w * 1.3f,
                   color);
        break;
    case Icon::star:
        star(s, cx, cy, size, color, false);
        break;
    case Icon::star_outline:
        star(s, cx, cy, size, color, true);
        break;
    case Icon::folder:
        s.fill_rounded({static_cast<int>(cx - h * 0.85f), static_cast<int>(cy - h * 0.62f),
                        static_cast<int>(h * 0.8f), static_cast<int>(h * 0.4f)},
                       3, color);
        s.fill_rounded({static_cast<int>(cx - h * 0.85f), static_cast<int>(cy - h * 0.42f),
                        static_cast<int>(h * 1.7f), static_cast<int>(h * 1.1f)},
                       4, color);
        break;
    case Icon::film:
    {
        const Rect body{static_cast<int>(cx - h * 0.85f), static_cast<int>(cy - h * 0.7f),
                        static_cast<int>(h * 1.7f), static_cast<int>(h * 1.4f)};
        s.fill_rounded(body, 4, color);
        const int hole = std::max(2, size / 10);
        for (int i = 0; i < 4; ++i)
        {
            const int x = body.x + body.w * (2 * i + 1) / 8 - hole / 2;
            s.fill({x, body.y + hole / 2 + 1, hole, hole}, gfx::rgba(0, 0, 0, 160));
            s.fill({x, body.bottom() - hole - hole / 2 - 1, hole, hole}, gfx::rgba(0, 0, 0, 160));
        }
        break;
    }
    case Icon::gear:
    {
        for (int i = 0; i < 8; ++i)
        {
            const float a = i * kPi / 4;
            thick_line(s, cx + std::cos(a) * h * 0.45f, cy + std::sin(a) * h * 0.45f,
                       cx + std::cos(a) * h * 0.9f, cy + std::sin(a) * h * 0.9f, h * 0.32f, color);
        }
        s.fill_circle(cx, cy, static_cast<int>(h * 0.66f), color);
        s.fill_circle(cx, cy, static_cast<int>(h * 0.26f), gfx::rgba(0, 0, 0, 170));
        break;
    }
    case Icon::home:
        s.fill_triangle(static_cast<int>(cx - h * 0.95f), static_cast<int>(cy - h * 0.05f), cx,
                        static_cast<int>(cy - h * 0.9f), static_cast<int>(cx + h * 0.95f),
                        static_cast<int>(cy - h * 0.05f), color);
        s.fill({static_cast<int>(cx - h * 0.62f), static_cast<int>(cy - h * 0.1f),
                static_cast<int>(h * 1.24f), static_cast<int>(h * 0.9f)},
               color);
        break;
    case Icon::alert:
        s.fill_triangle(cx, static_cast<int>(cy - h * 0.9f), static_cast<int>(cx + h),
                        static_cast<int>(cy + h * 0.8f), static_cast<int>(cx - h),
                        static_cast<int>(cy + h * 0.8f), color);
        s.fill({cx - static_cast<int>(w / 2), static_cast<int>(cy - h * 0.35f), static_cast<int>(w),
                static_cast<int>(h * 0.6f)},
               gfx::rgba(0, 0, 0, 200));
        s.fill_circle(cx, static_cast<int>(cy + h * 0.48f), static_cast<int>(w * 0.6f),
                      gfx::rgba(0, 0, 0, 200));
        break;
    case Icon::check:
        thick_line(s, cx - h * 0.7f, cy, cx - h * 0.18f, cy + h * 0.55f, w * 1.4f, color);
        thick_line(s, cx - h * 0.18f, cy + h * 0.55f, cx + h * 0.75f, cy - h * 0.6f, w * 1.4f,
                   color);
        break;
    case Icon::qr:
        for (const auto &[ox, oy] : {std::pair{-1, -1}, std::pair{1, -1}, std::pair{-1, 1}})
        {
            const int bx = static_cast<int>(cx + ox * h * 0.5f - h * 0.38f);
            const int by = static_cast<int>(cy + oy * h * 0.5f - h * 0.38f);
            s.stroke_rounded({bx, by, static_cast<int>(h * 0.76f), static_cast<int>(h * 0.76f)}, 2,
                             std::max(2, static_cast<int>(w * 0.8f)), color);
        }
        s.fill({static_cast<int>(cx + h * 0.25f), static_cast<int>(cy + h * 0.25f),
                static_cast<int>(h * 0.5f), static_cast<int>(h * 0.5f)},
               color);
        break;
    case Icon::info:
        ring(s, cx, cy, static_cast<int>(h * 0.85f), static_cast<int>(w), color);
        s.fill({cx - static_cast<int>(w / 2), static_cast<int>(cy - h * 0.1f), static_cast<int>(w),
                static_cast<int>(h * 0.55f)},
               color);
        s.fill_circle(cx, static_cast<int>(cy - h * 0.38f), static_cast<int>(w * 0.65f), color);
        break;
    case Icon::usb:
        s.fill_rounded({static_cast<int>(cx - h * 0.35f), static_cast<int>(cy - h * 0.2f),
                        static_cast<int>(h * 0.7f), static_cast<int>(h * 1.1f)},
                       3, color);
        s.stroke_rounded({static_cast<int>(cx - h * 0.25f), static_cast<int>(cy - h * 0.85f),
                          static_cast<int>(h * 0.5f), static_cast<int>(h * 0.65f)},
                         2, static_cast<int>(w * 0.8f), color);
        break;
    case Icon::refresh:
        ring(s, cx, cy, static_cast<int>(h * 0.7f), static_cast<int>(w), color);
        s.fill_triangle(static_cast<int>(cx + h * 0.35f), static_cast<int>(cy - h * 0.95f),
                        static_cast<int>(cx + h * 1.0f), static_cast<int>(cy - h * 0.6f),
                        static_cast<int>(cx + h * 0.35f), static_cast<int>(cy - h * 0.25f), color);
        break;
    case Icon::key:
        ring(s, static_cast<int>(cx - h * 0.45f), cy, static_cast<int>(h * 0.38f),
             static_cast<int>(w), color);
        thick_line(s, cx - h * 0.08f, cy, cx + h * 0.9f, cy, w * 1.1f, color);
        thick_line(s, cx + h * 0.55f, cy, cx + h * 0.55f, cy + h * 0.35f, w, color);
        thick_line(s, cx + h * 0.85f, cy, cx + h * 0.85f, cy + h * 0.3f, w, color);
        break;
    case Icon::lock:
        s.fill_rounded({static_cast<int>(cx - h * 0.7f), static_cast<int>(cy - h * 0.1f),
                        static_cast<int>(h * 1.4f), static_cast<int>(h * 1.0f)},
                       4, color);
        ring(s, cx, static_cast<int>(cy - h * 0.15f), static_cast<int>(h * 0.42f),
             static_cast<int>(w), color);
        break;
    case Icon::tv:
        s.stroke_rounded({static_cast<int>(cx - h * 0.95f), static_cast<int>(cy - h * 0.7f),
                          static_cast<int>(h * 1.9f), static_cast<int>(h * 1.2f)},
                         4, static_cast<int>(w), color);
        s.fill({static_cast<int>(cx - h * 0.4f), static_cast<int>(cy + h * 0.65f),
                static_cast<int>(h * 0.8f), static_cast<int>(w)},
               color);
        break;
    case Icon::spark:
        s.fill_triangle(cx, static_cast<int>(cy - h), static_cast<int>(cx + h * 0.22f), cy,
                        static_cast<int>(cx - h * 0.22f), cy, color);
        s.fill_triangle(cx, static_cast<int>(cy + h), static_cast<int>(cx + h * 0.22f), cy,
                        static_cast<int>(cx - h * 0.22f), cy, color);
        s.fill_triangle(static_cast<int>(cx - h), cy, cx, static_cast<int>(cy - h * 0.22f), cx,
                        static_cast<int>(cy + h * 0.22f), color);
        s.fill_triangle(static_cast<int>(cx + h), cy, cx, static_cast<int>(cy - h * 0.22f), cx,
                        static_cast<int>(cy + h * 0.22f), color);
        break;
    }
}

void Painter::placeholder(const Rect &r, const std::string &title, std::uint32_t accent, int radius)
{
    const std::uint32_t base = accent ? accent : accent_for(title);
    const Pixel top = gfx::mix(gfx::hex(base), theme::kSurface, 90);
    const Pixel bottom = gfx::mix(gfx::hex(base), theme::kBackgroundTop, 190);
    s.fill_rounded_gradient(r, radius, top, bottom);
    // Initials as a watermark; lower-case words ("of", "the") are skipped.
    std::string capitals, all;
    bool next = true;
    for (char c : title)
    {
        if (c == ' ' || c == '-' || c == ':')
        {
            next = true;
            continue;
        }
        if (next && static_cast<unsigned char>(c) < 0x80)
        {
            const char upper = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
            if (all.size() < 2)
                all += upper;
            if (!(c >= 'a' && c <= 'z') && capitals.size() < 2)
                capitals += upper;
        }
        next = false;
    }
    const std::string &initials = capitals.empty() ? all : capitals;
    const theme::Type big{std::max(24, r.h / 3), gfx::Weight::bold};
    text_center(r.x + r.w / 2, r.y + (r.h - line_height(big)) / 2, initials, big,
                gfx::rgba(255, 255, 255, 150));
}

void Painter::media_card(const Rect &image, const MediaItem &item, const gfx::Image *art,
                         bool focused, double progress_fraction, bool show_text,
                         const gfx::Image *icon)
{
    const int radius = theme::kCardRadius;
    if (art && art->valid())
        s.draw_image(*art, image.x, image.y, 255, radius);
    else if (icon && icon->valid())
    {
        // A site icon on a tile in the site's colour.
        const std::uint32_t base = item.accent ? item.accent : accent_for(item.title);
        s.fill_rounded_gradient(image, radius, gfx::mix(gfx::hex(base), theme::kSurface, 120),
                                gfx::mix(gfx::hex(base), theme::kBackgroundTop, 200));
        const int tile = icon->height + 36;
        s.fill_rounded({image.x + (image.w - tile) / 2, image.y + (image.h - tile) / 2, tile, tile},
                       24, gfx::rgba(255, 255, 255, 235));
        s.draw_image(*icon, image.x + (image.w - icon->width) / 2,
                     image.y + (image.h - icon->height) / 2, 255, 10);
    }
    else
        placeholder(image, item.title, item.accent, radius);

    if (!item.badge.empty())
    {
        const int bw = measure(item.badge, theme::kSmall) + 20;
        const int bh = line_height(theme::kSmall) + 6;
        const Pixel bg = item.badge == "LIVE" ? theme::kError : gfx::rgba(8, 10, 16, 200);
        s.fill_rounded({image.right() - bw - 10, image.y + 10, bw, bh}, 8, bg);
        text(image.right() - bw, image.y + 13, item.badge, theme::kSmall, theme::kText);
    }
    if (progress_fraction > 0.0)
        this->progress({image.x + 14, image.bottom() - 18, image.w - 28, 6}, progress_fraction,
                       theme::kAccentAnime, gfx::rgba(255, 255, 255, 70));
    if (focused)
        focus_ring(image, radius);
    if (!show_text)
        return;
    const int ty = image.bottom() + 14;
    text(image.x + 2, ty, item.title, theme::kBodyStrong,
         focused ? theme::kText : theme::kTextSecondary, image.w);
    if (!item.subtitle.empty())
        text(image.x + 2, ty + line_height(theme::kBodyStrong), item.subtitle, theme::kCaption,
             theme::kTextMuted, image.w);
}

int Painter::status_line(int x, int y, Pixel dot, std::string_view label, const theme::Type &type,
                         Pixel color)
{
    const int lh = line_height(type);
    s.fill_circle(x + 7, y + lh / 2, 7, dot);
    return 22 + text(x + 22, y, label, type, color);
}
} // namespace akeno::ui
