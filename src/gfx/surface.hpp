// AKENO STREAM PS5 - Linear 32-bit drawing surface.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Portable software rasterizer used for the whole interface. Pixels are stored
// as the console's RGBA8 framebuffer format: byte order R, G, B, A, i.e. a
// little-endian uint32 of 0xAABBGGRR. The surface lives in ordinary cached
// memory; the platform layer copies finished frames into the tiled VideoOut
// buffer. Every operation clips against the surface and the active clip rect.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace akeno::gfx
{
using Pixel = std::uint32_t;

constexpr Pixel rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) noexcept
{
    return static_cast<Pixel>(r) | (static_cast<Pixel>(g) << 8) | (static_cast<Pixel>(b) << 16) |
           (static_cast<Pixel>(a) << 24);
}

// 0xRRGGBB literal in the usual designer notation.
constexpr Pixel hex(std::uint32_t rgb, std::uint8_t a = 255) noexcept
{
    return rgba(static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8),
                static_cast<std::uint8_t>(rgb), a);
}

constexpr std::uint8_t red(Pixel p) noexcept
{
    return static_cast<std::uint8_t>(p);
}
constexpr std::uint8_t green(Pixel p) noexcept
{
    return static_cast<std::uint8_t>(p >> 8);
}
constexpr std::uint8_t blue(Pixel p) noexcept
{
    return static_cast<std::uint8_t>(p >> 16);
}
constexpr std::uint8_t alpha(Pixel p) noexcept
{
    return static_cast<std::uint8_t>(p >> 24);
}

constexpr Pixel with_alpha(Pixel p, std::uint8_t a) noexcept
{
    return (p & 0x00ffffffu) | (static_cast<Pixel>(a) << 24);
}

// Multiplies the existing alpha by opacity/255.
constexpr Pixel fade(Pixel p, std::uint8_t opacity) noexcept
{
    return with_alpha(p, static_cast<std::uint8_t>((alpha(p) * opacity + 127) / 255));
}

Pixel mix(Pixel a, Pixel b, unsigned t255) noexcept; // t=0 -> a, t=255 -> b

struct Rect
{
    int x = 0, y = 0, w = 0, h = 0;

    [[nodiscard]] constexpr int right() const noexcept
    {
        return x + w;
    }
    [[nodiscard]] constexpr int bottom() const noexcept
    {
        return y + h;
    }
    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return w <= 0 || h <= 0;
    }
    [[nodiscard]] constexpr Rect inset(int d) const noexcept
    {
        return {x + d, y + d, w - 2 * d, h - 2 * d};
    }
    [[nodiscard]] constexpr Rect offset(int dx, int dy) const noexcept
    {
        return {x + dx, y + dy, w, h};
    }
    [[nodiscard]] constexpr bool contains(int px, int py) const noexcept
    {
        return px >= x && py >= y && px < right() && py < bottom();
    }
    [[nodiscard]] Rect intersect(const Rect &o) const noexcept
    {
        const int l = std::max(x, o.x), t = std::max(y, o.y);
        const int r = std::min(right(), o.right()), b = std::min(bottom(), o.bottom());
        return r > l && b > t ? Rect{l, t, r - l, b - t} : Rect{l, t, 0, 0};
    }
};

// An RGBA image in ordinary memory (decoded thumbnail, video frame, glyph atlas).
struct Image
{
    int width = 0, height = 0;
    std::vector<Pixel> pixels;

    [[nodiscard]] bool valid() const noexcept
    {
        return width > 0 && height > 0 && pixels.size() == static_cast<std::size_t>(width) * height;
    }
    void resize(int w, int h, Pixel fill = 0);
};

// Resamples with a box filter when shrinking and bilinear when enlarging.
Image scale_image(const Image &source, int width, int height);
// Crops the centre of source to the target aspect ratio, then scales ("cover").
Image cover_image(const Image &source, int width, int height);

class Surface final
{
  public:
    Surface() = default;
    Surface(Pixel *pixels, int width, int height, int stride) noexcept;

    [[nodiscard]] int width() const noexcept
    {
        return width_;
    }
    [[nodiscard]] int height() const noexcept
    {
        return height_;
    }
    [[nodiscard]] int stride() const noexcept
    {
        return stride_;
    }
    [[nodiscard]] Pixel *row(int y) noexcept
    {
        return pixels_ + static_cast<std::ptrdiff_t>(y) * stride_;
    }
    [[nodiscard]] const Pixel *row(int y) const noexcept
    {
        return pixels_ + static_cast<std::ptrdiff_t>(y) * stride_;
    }
    [[nodiscard]] Rect bounds() const noexcept
    {
        return {0, 0, width_, height_};
    }
    [[nodiscard]] Rect clip() const noexcept
    {
        return clip_;
    }

    void push_clip(const Rect &r) noexcept;
    void pop_clip() noexcept;

    void clear(Pixel color) noexcept;
    // Opaque colours are copied, translucent ones blended over the destination.
    void fill(const Rect &r, Pixel color) noexcept;
    void fill_rounded(const Rect &r, int radius, Pixel color) noexcept;
    // Rounded rectangle filled with a vertical gradient.
    void fill_rounded_gradient(const Rect &r, int radius, Pixel top, Pixel bottom) noexcept;
    void stroke_rounded(const Rect &r, int radius, int thickness, Pixel color) noexcept;
    void gradient_vertical(const Rect &r, Pixel top, Pixel bottom) noexcept;
    void gradient_horizontal(const Rect &r, Pixel left, Pixel right) noexcept;
    void fill_circle(int cx, int cy, int radius, Pixel color) noexcept;
    void fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, Pixel color) noexcept;
    // Blends an 8-bit coverage mask tinted with color (glyphs, icons).
    void blend_mask(int x, int y, const std::uint8_t *mask, int w, int h, int mask_stride,
                    Pixel color) noexcept;
    // Draws image at its size; opacity scales source alpha. radius > 0 rounds corners.
    void draw_image(const Image &image, int x, int y, std::uint8_t opacity = 255,
                    int radius = 0) noexcept;
    // Draws image with its left, top and bottom edges fading to transparent over
    // the given distances, so artwork blends into whatever lies underneath.
    void draw_image_faded(const Image &image, int x, int y, std::uint8_t opacity, int fade_left,
                          int fade_top, int fade_bottom) noexcept;
    // Draws a region of image scaled to dst with nearest sampling (fast path for
    // large images such as video frames).
    void draw_image_scaled(const Image &image, const Rect &dst,
                           std::uint8_t opacity = 255) noexcept;
    // Darkens a region (scrims behind text over artwork).
    void dim(const Rect &r, std::uint8_t amount) noexcept;

  private:
    void blend_span(Pixel *dst, int count, Pixel color, unsigned coverage) noexcept;

    Pixel *pixels_ = nullptr;
    int width_ = 0, height_ = 0, stride_ = 0;
    Rect clip_{};
    Rect clip_stack_[16]{};
    int clip_depth_ = 0;
};

// Blends src over dst (straight alpha), coverage in [0,255].
inline Pixel blend(Pixel dst, Pixel src, unsigned coverage = 255) noexcept
{
    const unsigned a = (alpha(src) * coverage + 127) / 255;
    if (a == 0)
        return dst;
    if (a == 255)
        return src | 0xff000000u;
    const unsigned ia = 255 - a;
    const unsigned r = (red(src) * a + red(dst) * ia + 127) / 255;
    const unsigned g = (green(src) * a + green(dst) * ia + 127) / 255;
    const unsigned b = (blue(src) * a + blue(dst) * ia + 127) / 255;
    const unsigned da = alpha(dst);
    const unsigned oa = a + (da * ia + 127) / 255;
    return r | (g << 8) | (b << 16) | (oa << 24);
}
} // namespace akeno::gfx
