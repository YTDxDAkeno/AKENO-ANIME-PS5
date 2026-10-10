// AKENO STREAM PS5 - Decoded-picture conversion for the CPU compositor.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gfx/surface.hpp"

#include <cstddef>
#include <cstdint>

namespace akeno::gfx
{
enum class YuvMatrix : std::uint8_t
{
    bt601,
    bt709,
    bt2020,
};

// H.273 matrix_coefficients/height to a matrix; unspecified HD uses BT.709.
YuvMatrix choose_matrix(std::uint32_t h273_matrix, std::uint32_t height) noexcept;

struct Nv12Picture
{
    const std::uint8_t *data = nullptr; // luma plane, then interleaved CbCr plane
    std::size_t bytes = 0;
    std::uint32_t pitch = 0;          // in samples (bytes for 8-bit, words for 10-bit)
    std::uint32_t surface_height = 0; // luma rows allocated before the chroma plane
    std::uint32_t width = 0, height = 0;
    std::uint32_t bit_depth = 8; // 8, or 10 stored low-aligned in 16-bit words
    YuvMatrix matrix = YuvMatrix::bt709;
    bool full_range = false;
};

// Rectangle inside a target of tw x th that shows a w x h picture without
// distortion (letterbox or pillarbox), honouring a sample aspect ratio of 1.
Rect fit_rect(std::uint32_t w, std::uint32_t h, int tw, int th) noexcept;

// Converts picture into target (which must already be sized). The picture is
// fitted inside the target; uncovered areas are filled with black. Returns
// false (and leaves target untouched) when the picture geometry is invalid.
bool convert_nv12(const Nv12Picture &picture, Image &target) noexcept;
} // namespace akeno::gfx
