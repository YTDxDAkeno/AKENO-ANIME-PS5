// AKENO STREAM PS5 - Local files to MPEG-TS for the native player.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// MP4, Matroska and MPEG-TS files are demuxed with FFmpeg and remuxed, without
// transcoding, into an MPEG-TS byte stream for the same native pipeline that
// plays HLS (ProsperoTV feeds MP4/Matroska to its native player the same way).
// FFmpeg reads the file through custom AVIO on a POSIX descriptor; it never
// opens paths itself.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace akeno::media
{
struct RemuxInfo
{
    double duration = 0.0;
    std::string container;
    std::string video_codec;
    std::string audio_codec; // empty when there is no usable audio
    int width = 0, height = 0;
    int audio_rate = 0, audio_channels = 0;
    std::string notice; // e.g. "audio codec opus is not supported"
};

class Remuxer final
{
  public:
    Remuxer();
    ~Remuxer();
    Remuxer(const Remuxer &) = delete;
    Remuxer &operator=(const Remuxer &) = delete;

    bool open(const std::string &path, std::string *error);
    [[nodiscard]] const RemuxInfo &info() const noexcept;
    // Positions the input at or before seconds (keyframe). Call before run().
    bool seek(double seconds);
    // Writes TS bytes to sink until the end of the file (returns 1), sink
    // refusal or stop (0), or an error (-1, message in *error).
    using Sink = std::function<bool(const std::uint8_t *, std::size_t)>;
    // first_video_seconds receives the file time of the first video packet
    // written (after a seek this is the keyframe actually used).
    int run(const Sink &sink, const std::atomic<bool> &stop, std::string *error,
            std::atomic<double> *first_video_seconds = nullptr);
    void close();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Containers the library browser offers for playback.
bool is_playable_extension(const std::string &path);
} // namespace akeno::media
