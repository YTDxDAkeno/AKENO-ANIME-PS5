// AKENO STREAM PS5 - Byte streams that FFmpeg reads through custom AVIO.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// FFmpeg never opens files or URLs itself in this app: everything it demuxes
// arrives through one of these sources - a local file, a file on a web server
// (with HTTP Range requests for seeking), or an HLS rendition assembled from
// its segments (hls_source.hpp).
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace akeno::media
{
class ByteSource
{
  public:
    virtual ~ByteSource() = default;
    // Bytes read (> 0), 0 at the end, < 0 on failure or stop (see error()).
    virtual int read(std::uint8_t *buffer, int size) = 0;
    [[nodiscard]] virtual bool seekable() const
    {
        return false;
    }
    // Absolute position; false when the source cannot get there.
    virtual bool seek(std::int64_t offset)
    {
        (void)offset;
        return false;
    }
    [[nodiscard]] virtual std::int64_t size() const
    {
        return -1;
    }
    [[nodiscard]] virtual std::int64_t position() const = 0;
    [[nodiscard]] const std::string &error() const noexcept
    {
        return error_;
    }

  protected:
    std::string error_;
};

std::unique_ptr<ByteSource> open_file_source(const std::string &path, std::string *error);

// Network counters the player shows (called from the downloading thread).
using TransferObserver = std::function<void(long status, std::size_t bytes)>;

// A file on a web server, streamed through a bounded buffer by a background
// download. Seeking restarts the download with a Range request (or, when the
// server ignores ranges, from the start, skipping to the offset); small
// forward jumps are read through.
std::unique_ptr<ByteSource> open_http_source(const std::string &url, const std::atomic<bool> &stop,
                                             TransferObserver observer, std::string *error);
} // namespace akeno::media
