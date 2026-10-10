// AKENO STREAM PS5 - Bounded JPEG/PNG/GIF/BMP/ICO decoding for artwork.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gfx/surface.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace akeno::gfx
{
// Images come from the network; refuse anything implausibly large before
// allocating pixels for it.
inline constexpr int kMaxImageDimension = 4096;
inline constexpr std::size_t kMaxImageBytes = 12u * 1024u * 1024u;

// Decodes into straight-alpha RGBA. On failure returns an invalid image and
// writes a short reason into *error when provided.
Image decode_image(const std::uint8_t *bytes, std::size_t size, std::string *error = nullptr);
} // namespace akeno::gfx
