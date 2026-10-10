// AKENO STREAM PS5 - Bounded JPEG/PNG/GIF/BMP decoding for artwork.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gfx/image_decode.hpp"

#include <cstring>
#include <vector>

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
namespace
{
std::uint32_t le16(const std::uint8_t *p)
{
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8;
}
std::uint32_t le32(const std::uint8_t *p)
{
    return le16(p) | le16(p + 2) << 16;
}

// Windows icons (favicon.ico): the largest image, either embedded PNG or a
// bitmap without its file header (rebuilt here so stb_image can read it).
// Returns false when the bytes are not an icon.
bool icon_image(const std::uint8_t *bytes, std::size_t size, std::vector<std::uint8_t> &out)
{
    if (size < 6 || le16(bytes) != 0 || le16(bytes + 2) != 1)
        return false;
    const std::size_t count = le16(bytes + 4);
    if (count == 0 || count > 64 || 6 + count * 16 > size)
        return false;
    std::size_t best = count;
    std::uint32_t best_area = 0, best_bits = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::uint8_t *e = bytes + 6 + i * 16;
        const std::uint32_t w = e[0] ? e[0] : 256, h = e[1] ? e[1] : 256, bits = le16(e + 6);
        const std::uint32_t length = le32(e + 8), offset = le32(e + 12);
        if (offset >= size || length > size - offset || length < 40)
            continue;
        if (w * h > best_area || (w * h == best_area && bits > best_bits))
        {
            best = i;
            best_area = w * h;
            best_bits = bits;
        }
    }
    if (best == count)
        return false;
    const std::uint8_t *e = bytes + 6 + best * 16;
    const std::uint8_t *data = bytes + le32(e + 12);
    const std::uint32_t length = le32(e + 8);
    static constexpr std::uint8_t kPng[] = {0x89, 'P', 'N', 'G'};
    if (std::memcmp(data, kPng, sizeof(kPng)) == 0)
    {
        out.assign(data, data + length);
        return true;
    }
    const std::uint32_t header = le32(data);
    if (header < 40 || header > length)
        return false;
    const std::uint32_t bits = le16(data + 14);
    std::uint32_t colours = le32(data + 32);
    if (colours == 0 && bits <= 8)
        colours = 1u << bits;
    const std::uint32_t pixels = 14 + header + colours * 4;
    out.assign(14, 0);
    out[0] = 'B';
    out[1] = 'M';
    const std::uint32_t file_size = 14 + length;
    for (int i = 0; i < 4; ++i)
    {
        out[2 + i] = static_cast<std::uint8_t>(file_size >> (8 * i));
        out[10 + i] = static_cast<std::uint8_t>(pixels >> (8 * i));
    }
    out.insert(out.end(), data, data + length);
    // The icon's height counts the colour image and its transparency mask.
    const std::int32_t height = static_cast<std::int32_t>(le32(out.data() + 14 + 8)) / 2;
    for (int i = 0; i < 4; ++i)
        out[14 + 8 + i] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(height) >> (8 * i));
    return true;
}
} // namespace

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
    std::vector<std::uint8_t> icon;
    if (icon_image(bytes, size, icon))
    {
        if (icon.size() < 8 || (icon[0] == 'B' && icon.size() < 54))
            return fail("damaged icon");
        return decode_image(icon.data(), icon.size(), error);
    }
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
