// AKENO STREAM PS5 - HLS segment delivery.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SegmentCursor walks a media playlist (reloading it while a live stream
// runs), SegmentFetcher downloads one segment with retries, byte ranges and
// AES-128 decryption, and HlsSource joins the initialization section and the
// segments of one rendition into a byte stream FFmpeg can demux (fragmented
// MP4, or a separate audio rendition).
#pragma once

#include "media/byte_source.hpp"
#include "media/hls.hpp"
#include "net/http.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace akeno::media
{
struct SegmentStats
{
    long status = 0;
    std::size_t bytes = 0;
    std::uint32_t elapsed_ms = 0;
    bool retry = false;
    bool segment_done = false;
};
using SegmentObserver = std::function<void(const SegmentStats &)>;

// One segment as it was listed when it was handed out (indices into a
// playlist would not survive a live reload).
struct SegmentRef
{
    hls::Segment segment;
    std::optional<hls::Map> map;
    std::optional<hls::Key> key;
    bool jumped = false; // the live window moved past the expected segment
};

class SegmentCursor
{
  public:
    SegmentCursor(net::Client &client, std::string media_url, hls::Playlist playlist,
                  const std::atomic<bool> &interrupted, int live_edge_segments);

    // VOD: starts at the segment containing seconds; live: near the live edge.
    // Returns the start time of that segment (0 for live).
    double start_at(double seconds);

    enum class Next
    {
        segment,
        end,
        stopped,
        failed,
    };
    Next next(SegmentRef *out, std::string *error);
    [[nodiscard]] bool live() const noexcept
    {
        return live_;
    }

  private:
    net::Client &client_;
    std::string media_url_;
    hls::Playlist playlist_;
    const std::atomic<bool> &interrupted_;
    int live_edge_;
    bool live_;
    std::uint64_t next_sequence_ = 0;
    std::uint64_t last_reload_us_ = 0;
    int stale_reloads_ = 0;
};

class SegmentFetcher
{
  public:
    SegmentFetcher(net::Client &client, const std::atomic<bool> &interrupted,
                   net::CancelFlag cancel, SegmentObserver observer, int retries);
    // Downloads (and decrypts) the segment into *body.
    bool fetch(const SegmentRef &ref, std::string *body, std::string *error);
    bool fetch_map(const hls::Map &map, std::string *body, std::string *error);

  private:
    bool download(const std::string &uri, std::int64_t offset, std::int64_t length,
                  std::size_t max_bytes, std::string *body, std::string *error, bool segment);
    bool decrypt(const SegmentRef &ref, std::string *body, std::string *error);

    net::Client &client_;
    const std::atomic<bool> &interrupted_;
    net::CancelFlag cancel_;
    SegmentObserver observer_;
    int retries_;
    std::map<std::string, std::string> keys_;
};

// AES-128-CBC with PKCS#7 padding, as HLS uses it. Exposed for tests.
bool aes128_cbc_decrypt(const std::uint8_t key[16], const std::uint8_t iv[16], std::string *data);

// A rendition as one byte stream: initialization section, then segments.
std::unique_ptr<ByteSource> open_hls_source(std::string media_url, hls::Playlist playlist,
                                            double start_seconds, const std::atomic<bool> &stop,
                                            net::CancelFlag cancel, SegmentObserver observer,
                                            int live_edge_segments, int retries,
                                            double *segment_start);
} // namespace akeno::media
