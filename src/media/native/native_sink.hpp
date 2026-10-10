// AKENO STREAM PS5 - Hardware decode sink (Videodec2 + Audiodec/AudioOut).
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "media/player.hpp"

#include <memory>

namespace akeno::media
{
// Builds sinks bound to the vendored ProsperoTV native backend in picture
// mode: the backend decodes and paces, AKENO's renderer presents.
std::unique_ptr<DecodeSink> make_native_sink(std::shared_ptr<FrameStore> frames);

// Global audio volume (0..100) applied by the native audio path.
void set_native_volume(unsigned percent);
unsigned native_volume();
} // namespace akeno::media
