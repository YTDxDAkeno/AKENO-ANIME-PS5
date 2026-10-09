// AKENO STREAM PS5 - QR codes for handing a link to a phone.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gfx/qr.hpp"

#include <string>
#include <vector>

extern "C"
{
#include "qrcodegen/qrcodegen.h"
}

namespace akeno::gfx
{
Image make_qr(std::string_view text, int max_size)
{
    if (text.empty() || text.size() > 1200 || max_size < 64)
        return {};
    const std::string input{text};
    std::vector<std::uint8_t> code(qrcodegen_BUFFER_LEN_MAX), temp(qrcodegen_BUFFER_LEN_MAX);
    if (!qrcodegen_encodeText(input.c_str(), temp.data(), code.data(), qrcodegen_Ecc_MEDIUM,
                              qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO,
                              true))
        return {};
    const int modules = qrcodegen_getSize(code.data());
    const int total = modules + 8; // quiet zone of four modules on each side
    const int scale = max_size / total;
    if (scale < 1)
        return {};
    Image image;
    image.resize(total * scale, total * scale, rgba(255, 255, 255));
    for (int y = 0; y < modules; ++y)
        for (int x = 0; x < modules; ++x)
            if (qrcodegen_getModule(code.data(), x, y))
                for (int dy = 0; dy < scale; ++dy)
                {
                    Pixel *row =
                        &image.pixels[static_cast<std::size_t>((y + 4) * scale + dy) * image.width];
                    for (int dx = 0; dx < scale; ++dx)
                        row[(x + 4) * scale + dx] = rgba(0, 0, 0);
                }
    return image;
}
} // namespace akeno::gfx
