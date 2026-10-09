// AKENO STREAM PS5 - Host test decode sink (FFmpeg software, no pacing).
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stands in for the console's hardware decoder in host tests: decodes every
// access unit the demuxer submits and publishes pictures to the FrameStore,
// proving that the bytes the player feeds the decoder are decodable.
#pragma once

#include "media/player.hpp"

#include <atomic>
#include <memory>
#include <vector>

namespace akeno::test
{
struct SinkRecord
{
    std::atomic<int> opens{0};
    std::atomic<int> closes{0};
    std::atomic<int> drains{0};
    std::atomic<std::uint64_t> video_units{0};
    std::atomic<std::uint64_t> audio_units{0};
    std::atomic<std::uint64_t> frames{0};
    std::atomic<std::uint64_t> decode_errors{0};
    std::atomic<std::uint32_t> width{0};
    std::atomic<std::uint32_t> height{0};
    std::atomic<std::uint32_t> audio_type{0};
    std::atomic<int> sinks_created{0};
};

std::unique_ptr<media::DecodeSink> make_software_sink(std::shared_ptr<media::FrameStore> frames,
                                                      std::shared_ptr<SinkRecord> record);
} // namespace akeno::test
