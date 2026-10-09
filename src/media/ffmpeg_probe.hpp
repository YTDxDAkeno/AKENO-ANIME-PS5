// AKENO STREAM PS5 - Step-by-step FFmpeg demux/decode self-test.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// v0.3 failed on a PS5 with "ERROR -51": avformat_open_input or
// avformat_find_stream_info rejected a downloaded MPEG-TS segment and no
// frame was ever decoded. This probe repeats exactly that path on bytes in
// memory and records which stage failed, FFmpeg's error text and FFmpeg's own
// log lines, so a hardware run reports the cause instead of a single number.
// It runs on the host in the regression tests and on the console from the
// Diagnostics screen with a clip packaged in /app0/assets/selftest.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace akeno::media
{
enum class ProbeStage : std::uint8_t
{
    not_run,
    input,        // bytes present and recognisable
    open_input,   // avformat_open_input
    stream_info,  // avformat_find_stream_info
    find_stream,  // a video stream exists
    open_decoder, // avcodec_find_decoder + avcodec_open2
    decode,       // at least one frame decoded
    complete,
};

const char *stage_name(ProbeStage stage) noexcept;

struct ProbeReport
{
    ProbeStage reached = ProbeStage::not_run; // last stage that succeeded
    bool success = false;
    int error = 0;          // FFmpeg error of the failing stage (negative)
    std::string error_text; // av_strerror text
    std::string container;  // demuxer name
    std::string video_codec;
    std::string audio_codec;
    int width = 0, height = 0;
    std::string pixel_format;
    int audio_sample_rate = 0;
    int audio_channels = 0;
    unsigned frames_decoded = 0;
    unsigned audio_frames_decoded = 0;
    unsigned packets_read = 0;
    int stream_info_result = 0;
    std::string ffmpeg_version;
    std::vector<std::string> log; // FFmpeg's own warnings and errors

    [[nodiscard]] std::string summary() const;
};

struct ProbeOptions
{
    const char *format = nullptr; // force a demuxer (e.g. "mpegts"); null = probe
    unsigned max_video_frames = 8;
    bool call_find_stream_info = true;
};

ProbeReport probe_media(const std::uint8_t *bytes, std::size_t size,
                        const ProbeOptions &options = {});

// FFmpeg identity for diagnostics ("8.0.1, no x86 asm ...").
std::string ffmpeg_build_info();
} // namespace akeno::media
