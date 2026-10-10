// AKENO STREAM PS5 - HLS segment delivery.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/hls_source.hpp"

#include "platform/platform.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <cstring>

namespace akeno::media
{
namespace
{
constexpr std::size_t kPlaylistBytes = 4u << 20;
constexpr std::size_t kSegmentBytes = 48u << 20;
constexpr std::size_t kInitBytes = 4u << 20;

// Sleeps in small steps so a stop is noticed quickly.
void pause_us(std::uint64_t microseconds, const std::atomic<bool> &interrupted)
{
    const std::uint64_t until = platform::monotonic_us() + microseconds;
    while (!interrupted.load() && platform::monotonic_us() < until)
        platform::sleep_us(20000);
}

bool same_map(const std::optional<hls::Map> &a, const std::optional<hls::Map> &b)
{
    if (a.has_value() != b.has_value())
        return false;
    return !a || (a->uri == b->uri && a->offset == b->offset && a->length == b->length);
}
} // namespace

// ---------------------------------------------------------------------------
SegmentCursor::SegmentCursor(net::Client &client, std::string media_url, hls::Playlist playlist,
                             const std::atomic<bool> &interrupted, int live_edge_segments)
    : client_{client}, media_url_{std::move(media_url)}, playlist_{std::move(playlist)},
      interrupted_{interrupted}, live_edge_{live_edge_segments}, live_{playlist_.is_live()}
{
}

double SegmentCursor::start_at(double seconds)
{
    last_reload_us_ = platform::monotonic_us();
    const auto &segments = playlist_.segments;
    if (segments.empty())
        return 0.0;
    std::size_t index = 0;
    if (live_)
        index = segments.size() > static_cast<std::size_t>(live_edge_)
                    ? segments.size() - static_cast<std::size_t>(live_edge_)
                    : 0;
    else
        index = hls::segment_at(playlist_, seconds);
    next_sequence_ = segments[index].sequence;
    return live_ ? 0.0 : segments[index].start;
}

SegmentCursor::Next SegmentCursor::next(SegmentRef *out, std::string *error)
{
    for (;;)
    {
        if (interrupted_.load())
            return Next::stopped;
        const auto &segments = playlist_.segments;
        const auto it = std::find_if(segments.begin(), segments.end(), [&](const hls::Segment &s)
                                     { return s.sequence >= next_sequence_; });
        if (it != segments.end())
        {
            out->segment = *it;
            out->map.reset();
            out->key.reset();
            if (it->map >= 0 && static_cast<std::size_t>(it->map) < playlist_.maps.size())
                out->map = playlist_.maps[static_cast<std::size_t>(it->map)];
            if (it->key >= 0 && static_cast<std::size_t>(it->key) < playlist_.keys.size())
                out->key = playlist_.keys[static_cast<std::size_t>(it->key)];
            out->jumped = live_ && it->sequence > next_sequence_;
            next_sequence_ = it->sequence + 1;
            return Next::segment;
        }
        if (!live_)
            return Next::end;
        // Live: wait about half a target duration, then reload the playlist.
        const auto wait =
            static_cast<std::uint64_t>(std::max(1.0, playlist_.target_duration / 2.0) * 1.0e6);
        const std::uint64_t since = platform::monotonic_us() - last_reload_us_;
        if (since < wait)
            pause_us(wait - since, interrupted_);
        if (interrupted_.load())
            return Next::stopped;
        net::Request r;
        r.url = media_url_;
        r.max_bytes = kPlaylistBytes;
        const net::Response reload = client_.perform(r);
        last_reload_us_ = platform::monotonic_us();
        auto parsed = reload.ok() ? hls::parse(reload.body, media_url_) : hls::ParseResult{};
        if (parsed.ok && parsed.playlist.kind == hls::Kind::media)
        {
            const bool grew = !parsed.playlist.segments.empty() &&
                              parsed.playlist.segments.back().sequence >= next_sequence_;
            stale_reloads_ = grew ? 0 : stale_reloads_ + 1;
            playlist_ = std::move(parsed.playlist);
            if (!playlist_.is_live())
                live_ = false; // the event ended (ENDLIST appeared)
        }
        else
        {
            ++stale_reloads_;
        }
        if (stale_reloads_ > 12)
        {
            if (error)
                *error = "The live stream stopped updating";
            return Next::failed;
        }
    }
}

// ---------------------------------------------------------------------------
SegmentFetcher::SegmentFetcher(net::Client &client, const std::atomic<bool> &interrupted,
                               net::CancelFlag cancel, SegmentObserver observer, int retries)
    : client_{client}, interrupted_{interrupted}, cancel_{std::move(cancel)},
      observer_{std::move(observer)}, retries_{retries}
{
}

bool SegmentFetcher::download(const std::string &uri, std::int64_t offset, std::int64_t length,
                              std::size_t max_bytes, std::string *body, std::string *error,
                              bool segment)
{
    net::Response response;
    for (int attempt = 0; attempt <= retries_; ++attempt)
    {
        if (interrupted_.load())
        {
            *error = "stopped";
            return false;
        }
        net::Request r;
        r.url = uri;
        r.max_bytes = max_bytes;
        r.cancel = cancel_;
        if (offset >= 0 && length > 0)
            r.range = std::to_string(offset) + "-" + std::to_string(offset + length - 1);
        response = client_.perform(r);
        if (observer_ && segment)
            observer_({response.status, response.bytes, response.elapsed_ms, attempt > 0, false});
        if (response.ok())
        {
            *body = std::move(response.body);
            // A server that ignores Range sends the whole resource.
            if (offset >= 0 && length > 0 && response.status == 200 &&
                static_cast<std::int64_t>(body->size()) >= offset + length)
                *body = body->substr(static_cast<std::size_t>(offset),
                                     static_cast<std::size_t>(length));
            return true;
        }
        if (response.outcome == net::Outcome::cancelled)
            break;
        if (response.outcome == net::Outcome::http_error && response.status >= 400 &&
            response.status < 500 && response.status != 408 && response.status != 429)
            break;
        pause_us(500000u * static_cast<unsigned>(attempt + 1), interrupted_);
    }
    *error = interrupted_.load() ? "stopped" : response.describe();
    return false;
}

bool SegmentFetcher::fetch_map(const hls::Map &map, std::string *body, std::string *error)
{
    if (download(map.uri, map.offset, map.length, kInitBytes, body, error, false))
        return true;
    *error = "Initialization segment download failed: " + *error;
    return false;
}

bool SegmentFetcher::fetch(const SegmentRef &ref, std::string *body, std::string *error)
{
    const hls::Segment &s = ref.segment;
    if (!download(s.uri, s.offset, s.length, kSegmentBytes, body, error, true))
    {
        if (*error != "stopped")
            *error = "Segment download failed: " + *error;
        return false;
    }
    if (ref.key && !decrypt(ref, body, error))
        return false;
    if (observer_)
        observer_({0, 0, 0, false, true});
    return true;
}

bool SegmentFetcher::decrypt(const SegmentRef &ref, std::string *body, std::string *error)
{
    const hls::Key &key = *ref.key;
    auto cached = keys_.find(key.uri);
    if (cached == keys_.end())
    {
        std::string bytes;
        if (!download(key.uri, -1, 0, 1024, &bytes, error, false))
        {
            *error = "The decryption key could not be loaded: " + *error;
            return false;
        }
        if (bytes.size() != 16)
        {
            *error = "The decryption key is not 16 bytes long";
            return false;
        }
        cached = keys_.emplace(key.uri, std::move(bytes)).first;
    }
    std::uint8_t iv[16] = {};
    if (key.has_iv)
        std::memcpy(iv, key.iv, sizeof(iv));
    else
        for (int i = 0; i < 8; ++i) // the media sequence number, big-endian
            iv[15 - i] = static_cast<std::uint8_t>(ref.segment.sequence >> (8 * i));
    if (!aes128_cbc_decrypt(reinterpret_cast<const std::uint8_t *>(cached->second.data()), iv,
                            body))
    {
        *error = "A segment could not be decrypted (wrong key or damaged data)";
        return false;
    }
    return true;
}

bool aes128_cbc_decrypt(const std::uint8_t key[16], const std::uint8_t iv[16], std::string *data)
{
    if (data->empty() || data->size() % 16 != 0)
        return false;
    EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
    if (!context)
        return false;
    int written = 0, final_bytes = 0;
    auto *bytes = reinterpret_cast<unsigned char *>(data->data());
    const bool ok =
        EVP_DecryptInit_ex(context, EVP_aes_128_cbc(), nullptr, key, iv) == 1 &&
        EVP_DecryptUpdate(context, bytes, &written, bytes, static_cast<int>(data->size())) == 1 &&
        EVP_DecryptFinal_ex(context, bytes + written, &final_bytes) == 1;
    EVP_CIPHER_CTX_free(context);
    if (!ok)
        return false;
    data->resize(static_cast<std::size_t>(written + final_bytes));
    return true;
}

// ---------------------------------------------------------------------------
namespace
{
class HlsSource final : public ByteSource
{
  public:
    HlsSource(std::string media_url, hls::Playlist playlist, const std::atomic<bool> &stop,
              net::CancelFlag cancel, SegmentObserver observer, int live_edge, int retries)
        : cursor_{client_, std::move(media_url), std::move(playlist), stop, live_edge},
          fetcher_{client_, stop, std::move(cancel), std::move(observer), retries}, stop_{stop}
    {
    }

    double start_at(double seconds)
    {
        return cursor_.start_at(seconds);
    }

    int read(std::uint8_t *buffer, int size) override
    {
        while (offset_ == data_.size())
        {
            if (ended_)
                return 0;
            SegmentRef ref;
            std::string why;
            switch (cursor_.next(&ref, &why))
            {
            case SegmentCursor::Next::stopped:
                error_ = "stopped";
                return -1;
            case SegmentCursor::Next::failed:
                error_ = why;
                return -1;
            case SegmentCursor::Next::end:
                ended_ = true;
                return 0;
            case SegmentCursor::Next::segment:
                break;
            }
            std::string bytes;
            if (ref.map && !same_map(ref.map, map_))
            {
                if (!fetcher_.fetch_map(*ref.map, &bytes, &why))
                {
                    error_ = stop_.load() ? "stopped" : why;
                    return -1;
                }
                map_ = ref.map;
            }
            std::string segment;
            if (!fetcher_.fetch(ref, &segment, &why))
            {
                error_ = stop_.load() ? "stopped" : why;
                return -1;
            }
            bytes += segment;
            data_ = std::move(bytes);
            offset_ = 0;
        }
        const std::size_t n = std::min(data_.size() - offset_, static_cast<std::size_t>(size));
        std::memcpy(buffer, data_.data() + offset_, n);
        offset_ += n;
        position_ += static_cast<std::int64_t>(n);
        return static_cast<int>(n);
    }

    [[nodiscard]] std::int64_t position() const override
    {
        return position_;
    }

  private:
    net::Client client_;
    SegmentCursor cursor_;
    SegmentFetcher fetcher_;
    const std::atomic<bool> &stop_;
    std::optional<hls::Map> map_;
    std::string data_;
    std::size_t offset_ = 0;
    std::int64_t position_ = 0;
    bool ended_ = false;
};
} // namespace

std::unique_ptr<ByteSource> open_hls_source(std::string media_url, hls::Playlist playlist,
                                            double start_seconds, const std::atomic<bool> &stop,
                                            net::CancelFlag cancel, SegmentObserver observer,
                                            int live_edge_segments, int retries,
                                            double *segment_start)
{
    auto source = std::make_unique<HlsSource>(std::move(media_url), std::move(playlist), stop,
                                              std::move(cancel), std::move(observer),
                                              live_edge_segments, retries);
    const double start = source->start_at(start_seconds);
    if (segment_start)
        *segment_start = start;
    return source;
}
} // namespace akeno::media
