// AKENO STREAM PS5 - QR codes for handing a link to a phone.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gfx/surface.hpp"

#include <string_view>

namespace akeno::gfx
{
// Renders text as a QR code (medium error correction) with a four-module
// quiet zone, scaled to at most max_size pixels square. Invalid on failure.
Image make_qr(std::string_view text, int max_size);
} // namespace akeno::gfx
