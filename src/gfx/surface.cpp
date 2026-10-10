// AKENO STREAM PS5 - Linear 32-bit drawing surface.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gfx/surface.hpp"

#include <cmath>
#include <cstring>

namespace akeno::gfx
{
namespace
{
// Coverage (0..255) of a pixel centred at (px, py) inside a circle at (cx, cy).
unsigned circle_coverage(float px, float py, float cx, float cy, float radius) noexcept
{
    const float dx = px - cx, dy = py - cy;
    const float d = std::sqrt(dx * dx + dy * dy);
    const float c = radius + 0.5f - d;
    if (c <= 0.0f)
        return 0;
    if (c >= 1.0f)
        return 255;
    return static_cast<unsigned>(c * 255.0f + 0.5f);
}

std::uint8_t lerp8(unsigned a, unsigned b, unsigned t, unsigned span) noexcept
{
    return static_cast<std::uint8_t>((a * (span - t) + b * t + span / 2) / span);
}
} // namespace

Pixel mix(Pixel a, Pixel b, unsigned t) noexcept
{
    if (t > 255)
        t = 255;
    return rgba(lerp8(red(a), red(b), t, 255), lerp8(green(a), green(b), t, 255),
                lerp8(blue(a), blue(b), t, 255), lerp8(alpha(a), alpha(b), t, 255));
}

void Image::resize(int w, int h, Pixel fill)
{
    width = w > 0 ? w : 0;
    height = h > 0 ? h : 0;
    pixels.assign(static_cast<std::size_t>(width) * height, fill);
}

Image scale_image(const Image &source, int width, int height)
{
    Image out;
    if (!source.valid() || width <= 0 || height <= 0)
        return out;
    out.resize(width, height);
    const bool shrink = source.width >= width && source.height >= height;
    if (shrink)
    {
        // Box filter: average every source pixel that maps into the target pixel.
        for (int y = 0; y < height; ++y)
        {
            const int y0 = static_cast<int>(static_cast<long long>(y) * source.height / height);
            int y1 = static_cast<int>(static_cast<long long>(y + 1) * source.height / height);
            if (y1 <= y0)
                y1 = y0 + 1;
            for (int x = 0; x < width; ++x)
            {
                const int x0 = static_cast<int>(static_cast<long long>(x) * source.width / width);
                int x1 = static_cast<int>(static_cast<long long>(x + 1) * source.width / width);
                if (x1 <= x0)
                    x1 = x0 + 1;
                unsigned long long r = 0, g = 0, b = 0, a = 0;
                for (int sy = y0; sy < y1; ++sy)
                {
                    const Pixel *row = &source.pixels[static_cast<std::size_t>(sy) * source.width];
                    for (int sx = x0; sx < x1; ++sx)
                    {
                        const Pixel p = row[sx];
                        const unsigned pa = alpha(p);
                        r += red(p) * pa;
                        g += green(p) * pa;
                        b += blue(p) * pa;
                        a += pa;
                    }
                }
                const unsigned long long n = static_cast<unsigned long long>(y1 - y0) * (x1 - x0);
                out.pixels[static_cast<std::size_t>(y) * width + x] =
                    a == 0
                        ? 0
                        : rgba(static_cast<std::uint8_t>(r / a), static_cast<std::uint8_t>(g / a),
                               static_cast<std::uint8_t>(b / a), static_cast<std::uint8_t>(a / n));
            }
        }
        return out;
    }
    // Bilinear for enlargement.
    for (int y = 0; y < height; ++y)
    {
        const float fy = (static_cast<float>(y) + 0.5f) * source.height / height - 0.5f;
        const int sy = std::clamp(static_cast<int>(std::floor(fy)), 0, source.height - 1);
        const int sy1 = std::min(sy + 1, source.height - 1);
        const unsigned ty = static_cast<unsigned>(std::clamp(fy - sy, 0.0f, 1.0f) * 256.0f);
        for (int x = 0; x < width; ++x)
        {
            const float fx = (static_cast<float>(x) + 0.5f) * source.width / width - 0.5f;
            const int sx = std::clamp(static_cast<int>(std::floor(fx)), 0, source.width - 1);
            const int sx1 = std::min(sx + 1, source.width - 1);
            const unsigned tx = static_cast<unsigned>(std::clamp(fx - sx, 0.0f, 1.0f) * 256.0f);
            const Pixel p00 = source.pixels[static_cast<std::size_t>(sy) * source.width + sx];
            const Pixel p01 = source.pixels[static_cast<std::size_t>(sy) * source.width + sx1];
            const Pixel p10 = source.pixels[static_cast<std::size_t>(sy1) * source.width + sx];
            const Pixel p11 = source.pixels[static_cast<std::size_t>(sy1) * source.width + sx1];
            auto channel = [&](unsigned shift)
            {
                const unsigned a = (p00 >> shift) & 255, b = (p01 >> shift) & 255;
                const unsigned c = (p10 >> shift) & 255, d = (p11 >> shift) & 255;
                const unsigned top = a * (256 - tx) + b * tx;
                const unsigned bottom = c * (256 - tx) + d * tx;
                return ((top * (256 - ty) + bottom * ty) >> 16) & 255;
            };
            out.pixels[static_cast<std::size_t>(y) * width + x] =
                channel(0) | (channel(8) << 8) | (channel(16) << 16) | (channel(24) << 24);
        }
    }
    return out;
}

Image cover_image(const Image &source, int width, int height)
{
    if (!source.valid() || width <= 0 || height <= 0)
        return {};
    // Largest centred crop with the target aspect ratio.
    long long crop_w = source.width, crop_h = static_cast<long long>(source.width) * height / width;
    if (crop_h > source.height)
    {
        crop_h = source.height;
        crop_w = static_cast<long long>(source.height) * width / height;
    }
    const int ox = static_cast<int>((source.width - crop_w) / 2);
    const int oy = static_cast<int>((source.height - crop_h) / 2);
    Image cropped;
    cropped.resize(static_cast<int>(crop_w), static_cast<int>(crop_h));
    for (int y = 0; y < cropped.height; ++y)
        std::memcpy(&cropped.pixels[static_cast<std::size_t>(y) * cropped.width],
                    &source.pixels[static_cast<std::size_t>(y + oy) * source.width + ox],
                    static_cast<std::size_t>(cropped.width) * sizeof(Pixel));
    return scale_image(cropped, width, height);
}

Surface::Surface(Pixel *pixels, int width, int height, int stride) noexcept
    : pixels_{pixels}, width_{width}, height_{height}, stride_{stride}, clip_{0, 0, width, height}
{
}

void Surface::push_clip(const Rect &r) noexcept
{
    if (clip_depth_ < static_cast<int>(sizeof(clip_stack_) / sizeof(clip_stack_[0])))
        clip_stack_[clip_depth_++] = clip_;
    clip_ = clip_.intersect(r);
}

void Surface::pop_clip() noexcept
{
    clip_ = clip_depth_ > 0 ? clip_stack_[--clip_depth_] : bounds();
}

void Surface::blend_span(Pixel *dst, int count, Pixel color, unsigned coverage) noexcept
{
    const unsigned a = (alpha(color) * coverage + 127) / 255;
    if (a == 0)
        return;
    if (a == 255)
    {
        const Pixel solid = color | 0xff000000u;
        for (int i = 0; i < count; ++i)
            dst[i] = solid;
        return;
    }
    // Premultiply once for the span.
    const unsigned ia = 255 - a;
    const unsigned sr = red(color) * a, sg = green(color) * a, sb = blue(color) * a;
    for (int i = 0; i < count; ++i)
    {
        const Pixel d = dst[i];
        const unsigned r = (sr + red(d) * ia + 127) / 255;
        const unsigned g = (sg + green(d) * ia + 127) / 255;
        const unsigned b = (sb + blue(d) * ia + 127) / 255;
        dst[i] = r | (g << 8) | (b << 16) | 0xff000000u;
    }
}

void Surface::clear(Pixel color) noexcept
{
    const Pixel solid = color | 0xff000000u;
    for (int y = 0; y < height_; ++y)
    {
        Pixel *p = row(y);
        for (int x = 0; x < width_; ++x)
            p[x] = solid;
    }
}

void Surface::fill(const Rect &r, Pixel color) noexcept
{
    const Rect c = r.intersect(clip_);
    if (c.empty())
        return;
    for (int y = c.y; y < c.bottom(); ++y)
        blend_span(row(y) + c.x, c.w, color, 255);
}

void Surface::fill_rounded(const Rect &r, int radius, Pixel color) noexcept
{
    fill_rounded_gradient(r, radius, color, color);
}

void Surface::fill_rounded_gradient(const Rect &r, int radius, Pixel top_color,
                                    Pixel bottom_color) noexcept
{
    if (r.empty())
        return;
    radius = std::clamp(radius, 0, std::min(r.w, r.h) / 2);
    const Rect c = r.intersect(clip_);
    if (c.empty())
        return;
    const bool flat = top_color == bottom_color;
    const unsigned span = r.h > 1 ? static_cast<unsigned>(r.h - 1) : 1u;
    const auto row_color = [&](int y)
    {
        if (flat)
            return top_color;
        const unsigned t = static_cast<unsigned>(y - r.y);
        return rgba(lerp8(red(top_color), red(bottom_color), t, span),
                    lerp8(green(top_color), green(bottom_color), t, span),
                    lerp8(blue(top_color), blue(bottom_color), t, span),
                    lerp8(alpha(top_color), alpha(bottom_color), t, span));
    };
    if (radius == 0)
    {
        for (int y = c.y; y < c.bottom(); ++y)
            blend_span(row(y) + c.x, c.w, row_color(y), 255);
        return;
    }
    const float rad = static_cast<float>(radius);
    const float left_cx = r.x + rad - 0.5f, right_cx = r.right() - rad - 0.5f;
    const float top_cy = r.y + rad - 0.5f, bottom_cy = r.bottom() - rad - 0.5f;
    for (int y = c.y; y < c.bottom(); ++y)
    {
        Pixel *p = row(y);
        const Pixel color = row_color(y);
        const bool top = y < r.y + radius, bottom = y >= r.bottom() - radius;
        if (!top && !bottom)
        {
            blend_span(p + c.x, c.w, color, 255);
            continue;
        }
        const float cy = top ? top_cy : bottom_cy;
        for (int x = c.x; x < c.right(); ++x)
        {
            unsigned coverage = 255;
            if (x < r.x + radius)
                coverage =
                    circle_coverage(static_cast<float>(x), static_cast<float>(y), left_cx, cy, rad);
            else if (x >= r.right() - radius)
                coverage = circle_coverage(static_cast<float>(x), static_cast<float>(y), right_cx,
                                           cy, rad);
            if (coverage)
                blend_span(p + x, 1, color, coverage);
        }
    }
}

void Surface::stroke_rounded(const Rect &r, int radius, int thickness, Pixel color) noexcept
{
    if (r.empty() || thickness <= 0)
        return;
    radius = std::clamp(radius, 0, std::min(r.w, r.h) / 2);
    const Rect c = r.intersect(clip_);
    if (c.empty())
        return;
    const float outer = static_cast<float>(radius);
    const float inner = std::max(0.0f, outer - thickness);
    const float left_cx = r.x + outer - 0.5f, right_cx = r.right() - outer - 0.5f;
    const float top_cy = r.y + outer - 0.5f, bottom_cy = r.bottom() - outer - 0.5f;
    for (int y = c.y; y < c.bottom(); ++y)
    {
        Pixel *p = row(y);
        const bool in_top = y < r.y + radius, in_bottom = y >= r.bottom() - radius;
        if (!in_top && !in_bottom)
        {
            // Straight sides, and the horizontal edges when radius is small.
            const bool edge_row = y < r.y + thickness || y >= r.bottom() - thickness;
            if (edge_row)
            {
                blend_span(p + c.x, c.w, color, 255);
                continue;
            }
            const int l0 = std::max(c.x, r.x), l1 = std::min(c.right(), r.x + thickness);
            if (l1 > l0)
                blend_span(p + l0, l1 - l0, color, 255);
            const int r0 = std::max(c.x, r.right() - thickness),
                      r1 = std::min(c.right(), r.right());
            if (r1 > r0)
                blend_span(p + r0, r1 - r0, color, 255);
            continue;
        }
        const float cy = in_top ? top_cy : bottom_cy;
        for (int x = c.x; x < c.right(); ++x)
        {
            unsigned coverage;
            const bool corner_x = x < r.x + radius || x >= r.right() - radius;
            if (corner_x)
            {
                const float cx = x < r.x + radius ? left_cx : right_cx;
                const unsigned out_c =
                    circle_coverage(static_cast<float>(x), static_cast<float>(y), cx, cy, outer);
                const unsigned in_c =
                    circle_coverage(static_cast<float>(x), static_cast<float>(y), cx, cy, inner);
                coverage = out_c > in_c ? out_c - in_c : 0;
            }
            else
            {
                coverage = (y < r.y + thickness || y >= r.bottom() - thickness) ? 255 : 0;
            }
            if (coverage)
                blend_span(p + x, 1, color, coverage);
        }
    }
}

void Surface::gradient_vertical(const Rect &r, Pixel top, Pixel bottom) noexcept
{
    const Rect c = r.intersect(clip_);
    if (c.empty())
        return;
    const unsigned span = r.h > 1 ? static_cast<unsigned>(r.h - 1) : 1u;
    for (int y = c.y; y < c.bottom(); ++y)
    {
        const unsigned t = static_cast<unsigned>(y - r.y);
        const Pixel color = rgba(
            lerp8(red(top), red(bottom), t, span), lerp8(green(top), green(bottom), t, span),
            lerp8(blue(top), blue(bottom), t, span), lerp8(alpha(top), alpha(bottom), t, span));
        blend_span(row(y) + c.x, c.w, color, 255);
    }
}

void Surface::gradient_horizontal(const Rect &r, Pixel left, Pixel right) noexcept
{
    const Rect c = r.intersect(clip_);
    if (c.empty())
        return;
    const unsigned span = r.w > 1 ? static_cast<unsigned>(r.w - 1) : 1u;
    for (int y = c.y; y < c.bottom(); ++y)
    {
        Pixel *p = row(y);
        for (int x = c.x; x < c.right(); ++x)
        {
            const unsigned t = static_cast<unsigned>(x - r.x);
            const Pixel color = rgba(
                lerp8(red(left), red(right), t, span), lerp8(green(left), green(right), t, span),
                lerp8(blue(left), blue(right), t, span), lerp8(alpha(left), alpha(right), t, span));
            blend_span(p + x, 1, color, 255);
        }
    }
}

void Surface::fill_circle(int cx, int cy, int radius, Pixel color) noexcept
{
    if (radius <= 0)
        return;
    const Rect c = Rect{cx - radius, cy - radius, radius * 2 + 1, radius * 2 + 1}.intersect(clip_);
    for (int y = c.y; y < c.bottom(); ++y)
    {
        Pixel *p = row(y);
        for (int x = c.x; x < c.right(); ++x)
        {
            const unsigned coverage = circle_coverage(
                static_cast<float>(x), static_cast<float>(y), static_cast<float>(cx),
                static_cast<float>(cy), static_cast<float>(radius));
            if (coverage)
                blend_span(p + x, 1, color, coverage);
        }
    }
}

void Surface::fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, Pixel color) noexcept
{
    // Scanline fill with 4x vertical supersampling for smooth play/arrow icons.
    const int min_y = std::max({clip_.y, std::min({y0, y1, y2})});
    const int max_y = std::min({clip_.bottom() - 1, std::max({y0, y1, y2})});
    const float vx[3] = {static_cast<float>(x0), static_cast<float>(x1), static_cast<float>(x2)};
    const float vy[3] = {static_cast<float>(y0), static_cast<float>(y1), static_cast<float>(y2)};
    for (int y = min_y; y <= max_y; ++y)
    {
        float cover_l[4], cover_r[4];
        int samples = 0;
        for (int s = 0; s < 4; ++s)
        {
            const float sy = static_cast<float>(y) + (s + 0.5f) / 4.0f;
            float lo = 1e9f, hi = -1e9f;
            for (int e = 0; e < 3; ++e)
            {
                const int f = (e + 1) % 3;
                const float ya = vy[e], yb = vy[f];
                if ((sy < ya) == (sy < yb))
                    continue;
                const float t = (sy - ya) / (yb - ya);
                const float x = vx[e] + t * (vx[f] - vx[e]);
                lo = std::min(lo, x);
                hi = std::max(hi, x);
            }
            if (hi > lo)
            {
                cover_l[samples] = lo;
                cover_r[samples] = hi;
                ++samples;
            }
        }
        if (!samples)
            continue;
        float lo = cover_l[0], hi = cover_r[0];
        for (int s = 1; s < samples; ++s)
        {
            lo = std::min(lo, cover_l[s]);
            hi = std::max(hi, cover_r[s]);
        }
        const int x_begin = std::max(clip_.x, static_cast<int>(std::floor(lo)));
        const int x_end = std::min(clip_.right(), static_cast<int>(std::ceil(hi)));
        Pixel *p = row(y);
        for (int x = x_begin; x < x_end; ++x)
        {
            float total = 0.0f;
            for (int s = 0; s < samples; ++s)
            {
                const float a = std::max(cover_l[s], static_cast<float>(x));
                const float b = std::min(cover_r[s], static_cast<float>(x + 1));
                if (b > a)
                    total += b - a;
            }
            const unsigned coverage =
                static_cast<unsigned>(std::clamp(total / 4.0f, 0.0f, 1.0f) * 255.0f);
            if (coverage)
                blend_span(p + x, 1, color, coverage);
        }
    }
}

void Surface::blend_mask(int x, int y, const std::uint8_t *mask, int w, int h, int mask_stride,
                         Pixel color) noexcept
{
    if (!mask)
        return;
    const Rect c = Rect{x, y, w, h}.intersect(clip_);
    if (c.empty())
        return;
    const unsigned base = alpha(color);
    for (int yy = c.y; yy < c.bottom(); ++yy)
    {
        Pixel *p = row(yy);
        const std::uint8_t *m = mask + static_cast<std::ptrdiff_t>(yy - y) * mask_stride;
        for (int xx = c.x; xx < c.right(); ++xx)
        {
            const unsigned coverage = m[xx - x];
            if (!coverage)
                continue;
            const unsigned a = (base * coverage + 127) / 255;
            p[xx] = blend(p[xx], with_alpha(color, static_cast<std::uint8_t>(a)));
        }
    }
}

void Surface::draw_image(const Image &image, int x, int y, std::uint8_t opacity,
                         int radius) noexcept
{
    if (!image.valid() || opacity == 0)
        return;
    const Rect dst{x, y, image.width, image.height};
    const Rect c = dst.intersect(clip_);
    if (c.empty())
        return;
    radius = std::clamp(radius, 0, std::min(image.width, image.height) / 2);
    const float rad = static_cast<float>(radius);
    for (int yy = c.y; yy < c.bottom(); ++yy)
    {
        Pixel *p = row(yy);
        const Pixel *s = &image.pixels[static_cast<std::size_t>(yy - y) * image.width];
        const bool corner_row = radius && (yy < y + radius || yy >= dst.bottom() - radius);
        const float cy = yy < y + radius ? y + rad - 0.5f : dst.bottom() - rad - 0.5f;
        for (int xx = c.x; xx < c.right(); ++xx)
        {
            unsigned coverage = opacity;
            if (corner_row && (xx < x + radius || xx >= dst.right() - radius))
            {
                const float cx = xx < x + radius ? x + rad - 0.5f : dst.right() - rad - 0.5f;
                coverage =
                    coverage *
                    circle_coverage(static_cast<float>(xx), static_cast<float>(yy), cx, cy, rad) /
                    255;
            }
            const Pixel src = s[xx - x];
            if (coverage == 255 && alpha(src) == 255)
                p[xx] = src;
            else
                p[xx] = blend(p[xx], src, coverage);
        }
    }
}

namespace
{
// 0..255 ramp with an eased (smoothstep) profile.
unsigned edge_ramp(int distance, int length) noexcept
{
    if (length <= 0 || distance >= length)
        return 255;
    if (distance <= 0)
        return 0;
    const unsigned t = static_cast<unsigned>(distance) * 255u / static_cast<unsigned>(length);
    return t * t * (765u - 2u * t) / (255u * 255u);
}
} // namespace

void Surface::draw_image_faded(const Image &image, int x, int y, std::uint8_t opacity,
                               int fade_left, int fade_top, int fade_bottom) noexcept
{
    if (!image.valid() || opacity == 0)
        return;
    const Rect dst{x, y, image.width, image.height};
    const Rect c = dst.intersect(clip_);
    if (c.empty())
        return;
    std::vector<std::uint8_t> column(static_cast<std::size_t>(c.w));
    for (int xx = c.x; xx < c.right(); ++xx)
        column[static_cast<std::size_t>(xx - c.x)] =
            static_cast<std::uint8_t>(edge_ramp(xx - x, fade_left));
    for (int yy = c.y; yy < c.bottom(); ++yy)
    {
        const unsigned vertical =
            std::min(edge_ramp(yy - y, fade_top), edge_ramp(dst.bottom() - 1 - yy, fade_bottom));
        const unsigned row_alpha = vertical * opacity / 255;
        if (row_alpha == 0)
            continue;
        Pixel *p = row(yy);
        const Pixel *s = &image.pixels[static_cast<std::size_t>(yy - y) * image.width];
        for (int xx = c.x; xx < c.right(); ++xx)
        {
            const unsigned coverage = row_alpha * column[static_cast<std::size_t>(xx - c.x)] / 255;
            if (coverage)
                p[xx] = blend(p[xx], s[xx - x], coverage);
        }
    }
}

void Surface::draw_image_scaled(const Image &image, const Rect &dst, std::uint8_t opacity) noexcept
{
    if (!image.valid() || dst.empty())
        return;
    const Rect c = dst.intersect(clip_);
    if (c.empty())
        return;
    for (int yy = c.y; yy < c.bottom(); ++yy)
    {
        const int sy = static_cast<int>(static_cast<long long>(yy - dst.y) * image.height / dst.h);
        const Pixel *s = &image.pixels[static_cast<std::size_t>(sy) * image.width];
        Pixel *p = row(yy);
        for (int xx = c.x; xx < c.right(); ++xx)
        {
            const int sx =
                static_cast<int>(static_cast<long long>(xx - dst.x) * image.width / dst.w);
            const Pixel src = s[sx];
            if (opacity == 255 && alpha(src) == 255)
                p[xx] = src;
            else
                p[xx] = blend(p[xx], src, opacity);
        }
    }
}

void Surface::dim(const Rect &r, std::uint8_t amount) noexcept
{
    fill(r, rgba(0, 0, 0, amount));
}
} // namespace akeno::gfx
