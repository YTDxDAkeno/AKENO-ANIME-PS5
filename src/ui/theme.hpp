// AKENO STREAM PS5 - Visual design tokens.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gfx/font.hpp"
#include "gfx/surface.hpp"

namespace akeno::ui::theme
{
using gfx::hex;
using gfx::Pixel;

// Canvas and safe area (TV overscan-safe margins).
inline constexpr int kWidth = 1920;
inline constexpr int kHeight = 1080;
inline constexpr int kMarginX = 96;
inline constexpr int kTopBarHeight = 132;
inline constexpr int kHintBarY = 1012;

// Surfaces
inline constexpr Pixel kBackgroundTop = hex(0x0a0e18);
inline constexpr Pixel kBackgroundBottom = hex(0x131a29);
inline constexpr Pixel kSurface = hex(0x182033);
inline constexpr Pixel kSurfaceRaised = hex(0x212b42);
inline constexpr Pixel kSurfaceHighlight = hex(0x2b3754);
inline constexpr Pixel kDivider = hex(0x2a3348);
inline constexpr Pixel kPlaceholder = hex(0x1d2538);

// Text
inline constexpr Pixel kText = hex(0xf4f6fb);
inline constexpr Pixel kTextSecondary = hex(0xb1bbcf);
inline constexpr Pixel kTextMuted = hex(0x76819a);
inline constexpr Pixel kTextOnAccent = hex(0x0b0f19);

// Accents per mode
inline constexpr Pixel kAccentHome = hex(0x4f8cff);
inline constexpr Pixel kAccentAnime = hex(0xff7a3d);
inline constexpr Pixel kAccentYouTube = hex(0xff3d5a);
inline constexpr Pixel kAccentDiscover = hex(0xe56bd0);
inline constexpr Pixel kAccentLibrary = hex(0x35c79a);
inline constexpr Pixel kAccentSources = hex(0x2ec4d6);
inline constexpr Pixel kAccentSettings = hex(0x9b7bff);
inline constexpr Pixel kFocus = hex(0xffffff);

// Status
inline constexpr Pixel kSuccess = hex(0x35c79a);
inline constexpr Pixel kWarning = hex(0xffc247);
inline constexpr Pixel kError = hex(0xff5c6c);
inline constexpr Pixel kInfo = hex(0x5aa9ff);

// DualSense face-button colours (as printed on the PS5 controller: monochrome
// glyphs; tinted subtly for recognisability).
inline constexpr Pixel kCross = hex(0x8fb4ff);
inline constexpr Pixel kCircle = hex(0xff8a8a);
inline constexpr Pixel kSquare = hex(0xe99fe0);
inline constexpr Pixel kTriangle = hex(0x6fe0c0);

// Geometry
inline constexpr int kCardRadius = 16;
inline constexpr int kPanelRadius = 24;
inline constexpr int kFocusThickness = 5;
inline constexpr int kLandscapeCardW = 400;
inline constexpr int kLandscapeCardH = 225;
inline constexpr int kPortraitCardW = 200;
inline constexpr int kPortraitCardH = 300;
inline constexpr int kCardGap = 28;

// Typography
struct Type
{
    int size;
    gfx::Weight weight;
};
inline constexpr Type kDisplay{68, gfx::Weight::bold};
inline constexpr Type kTitle{46, gfx::Weight::bold};
inline constexpr Type kHeading{34, gfx::Weight::semibold};
inline constexpr Type kSubheading{28, gfx::Weight::semibold};
inline constexpr Type kBody{26, gfx::Weight::regular};
inline constexpr Type kBodyStrong{26, gfx::Weight::semibold};
inline constexpr Type kCaption{22, gfx::Weight::regular};
inline constexpr Type kCaptionStrong{22, gfx::Weight::semibold};
inline constexpr Type kSmall{19, gfx::Weight::semibold};

inline gfx::TextStyle style(const Type &type, Pixel color = kText)
{
    return {type.size, type.weight, color};
}
} // namespace akeno::ui::theme
