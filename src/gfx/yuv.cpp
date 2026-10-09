// AKENO STREAM PS5 - Decoded-picture conversion for the CPU compositor.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gfx/yuv.hpp"

#include <array>
#include <vector>

namespace akeno::gfx
{
namespace
{
struct Tables
{
    std::array<int, 256> y{}, rv{}, gu{}, gv{}, bu{};
};

// Fixed point with 6 fractional bits. Coefficients from ITU-R BT.601, BT.709
// and BT.2020 (non-constant luminance).
Tables make_tables(YuvMatrix matrix, bool full_range) noexcept
{
    double kr, kb;
    switch (matrix)
    {
    case YuvMatrix::bt601:
        kr = 0.299;
        kb = 0.114;
        break;
    case YuvMatrix::bt2020:
        kr = 0.2627;
        kb = 0.0593;
        break;
    case YuvMatrix::bt709:
    default:
        kr = 0.2126;
        kb = 0.0722;
        break;
    }
    const double kg = 1.0 - kr - kb;
    const double y_scale = full_range ? 1.0 : 255.0 / 219.0;
    const double c_scale = full_range ? 1.0 : 255.0 / 224.0;
    const double y_offset = full_range ? 0.0 : 16.0;
    Tables t;
    for (int i = 0; i < 256; ++i)
    {
        const double yv = (i - y_offset) * y_scale;
        const double c = (i - 128.0) * c_scale;
        t.y[i] = static_cast<int>(yv * 64.0 + (yv >= 0 ? 0.5 : -0.5));
        t.rv[i] = static_cast<int>(c * 2.0 * (1.0 - kr) * 64.0);
        t.bu[i] = static_cast<int>(c * 2.0 * (1.0 - kb) * 64.0);
        t.gu[i] = static_cast<int>(-c * 2.0 * (1.0 - kb) * kb / kg * 64.0);
        t.gv[i] = static_cast<int>(-c * 2.0 * (1.0 - kr) * kr / kg * 64.0);
    }
    return t;
}

const Tables &tables(YuvMatrix matrix, bool full_range) noexcept
{
    static const Tables cache[3][2] = {
        {make_tables(YuvMatrix::bt601, false), make_tables(YuvMatrix::bt601, true)},
        {make_tables(YuvMatrix::bt709, false), make_tables(YuvMatrix::bt709, true)},
        {make_tables(YuvMatrix::bt2020, false), make_tables(YuvMatrix::bt2020, true)},
    };
    return cache[static_cast<int>(matrix)][full_range ? 1 : 0];
}

inline std::uint32_t clamp8(int v) noexcept
{
    v >>= 6;
    return static_cast<std::uint32_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

template <typename Sample, int Shift>
void convert_rows(const Nv12Picture &p, Image &target, const Rect &dst, const Tables &t) noexcept
{
    const auto *luma = reinterpret_cast<const Sample *>(p.data);
    const Sample *chroma = luma + static_cast<std::size_t>(p.pitch) * p.surface_height;
    std::vector<std::uint32_t> xmap(static_cast<std::size_t>(dst.w));
    for (int i = 0; i < dst.w; ++i)
        xmap[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(i) * p.width / static_cast<unsigned>(dst.w));
    for (int j = 0; j < dst.h; ++j)
    {
        const std::uint32_t sy = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(j) * p.height / static_cast<unsigned>(dst.h));
        const Sample *yrow = luma + static_cast<std::size_t>(sy) * p.pitch;
        const Sample *crow = chroma + static_cast<std::size_t>(sy / 2) * p.pitch;
        Pixel *out = &target.pixels[static_cast<std::size_t>(dst.y + j) * target.width + dst.x];
        for (int i = 0; i < dst.w; ++i)
        {
            const std::uint32_t sx = xmap[static_cast<std::size_t>(i)];
            const unsigned yv = static_cast<unsigned>(yrow[sx] >> Shift) & 255u;
            const std::uint32_t cx = sx & ~1u;
            const unsigned u = static_cast<unsigned>(crow[cx] >> Shift) & 255u;
            const unsigned v = static_cast<unsigned>(crow[cx + 1] >> Shift) & 255u;
            const int base = t.y[yv];
            out[i] = clamp8(base + t.rv[v]) | (clamp8(base + t.gu[u] + t.gv[v]) << 8) |
                     (clamp8(base + t.bu[u]) << 16) | 0xff000000u;
        }
    }
}
} // namespace

YuvMatrix choose_matrix(std::uint32_t h273_matrix, std::uint32_t height) noexcept
{
    switch (h273_matrix)
    {
    case 1:
        return YuvMatrix::bt709;
    case 5:
    case 6:
        return YuvMatrix::bt601;
    case 9:
    case 10:
        return YuvMatrix::bt2020;
    default:
        return height > 576 ? YuvMatrix::bt709 : YuvMatrix::bt601;
    }
}

Rect fit_rect(std::uint32_t w, std::uint32_t h, int tw, int th) noexcept
{
    if (w == 0 || h == 0 || tw <= 0 || th <= 0)
        return {0, 0, 0, 0};
    // Compare aspect ratios without floating point.
    int out_w = tw, out_h = th;
    if (static_cast<std::uint64_t>(w) * static_cast<unsigned>(th) >
        static_cast<std::uint64_t>(h) * static_cast<unsigned>(tw))
        out_h = static_cast<int>(static_cast<std::uint64_t>(tw) * h / w);
    else
        out_w = static_cast<int>(static_cast<std::uint64_t>(th) * w / h);
    out_w &= ~1;
    out_h &= ~1;
    return {(tw - out_w) / 2, (th - out_h) / 2, out_w, out_h};
}

bool convert_nv12(const Nv12Picture &p, Image &target) noexcept
{
    if (!p.data || !target.valid() || p.width < 2 || p.height < 2 || p.pitch < p.width ||
        p.surface_height < p.height || (p.bit_depth != 8 && p.bit_depth != 10))
        return false;
    const std::size_t sample = p.bit_depth == 10 ? 2 : 1;
    const std::size_t required =
        static_cast<std::size_t>(p.pitch) * (p.surface_height + (p.height + 1) / 2) * sample;
    if (p.bytes < required)
        return false;
    const Rect dst = fit_rect(p.width, p.height, target.width, target.height);
    if (dst.empty())
        return false;
    // Black bars only where the picture does not cover the target.
    for (int y = 0; y < target.height; ++y)
    {
        Pixel *row = &target.pixels[static_cast<std::size_t>(y) * target.width];
        if (y < dst.y || y >= dst.bottom())
        {
            std::fill(row, row + target.width, 0xff000000u);
            continue;
        }
        std::fill(row, row + dst.x, 0xff000000u);
        std::fill(row + dst.right(), row + target.width, 0xff000000u);
    }
    const Tables &t = tables(p.matrix, p.full_range);
    if (p.bit_depth == 10)
        convert_rows<std::uint16_t, 2>(p, target, dst, t);
    else
        convert_rows<std::uint8_t, 0>(p, target, dst, t);
    return true;
}
} // namespace akeno::gfx
