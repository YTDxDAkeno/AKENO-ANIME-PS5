// AKENO STREAM PS5 - Bounded JPEG/PNG/GIF/BMP decoding for artwork.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gfx/image_decode.hpp"

#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
// Thread-local error state would need emulated TLS in the native title.
#define STBI_NO_THREAD_LOCALS
#define STBI_MAX_DIMENSIONS 4096
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#include "stb/stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace akeno::gfx
{
Image decode_image(const std::uint8_t *bytes, std::size_t size, std::string *error)
{
    Image image;
    auto fail = [&](const char *why)
    {
        if (error)
            *error = why;
        return Image{};
    };
    if (!bytes || size < 8)
        return fail("empty image");
    if (size > kMaxImageBytes)
        return fail("image file too large");
    int w = 0, h = 0, channels = 0;
    if (!stbi_info_from_memory(bytes, static_cast<int>(size), &w, &h, &channels))
        return fail("unsupported image format");
    if (w <= 0 || h <= 0 || w > kMaxImageDimension || h > kMaxImageDimension)
        return fail("image dimensions out of range");
    unsigned char *decoded =
        stbi_load_from_memory(bytes, static_cast<int>(size), &w, &h, &channels, 4);
    if (!decoded)
        return fail("image decode failed");
    image.resize(w, h);
    std::memcpy(image.pixels.data(), decoded, static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(decoded);
    return image;
}
} // namespace akeno::gfx
