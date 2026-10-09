// Akeno Anime - experimental DRM-free HLS video sample for native PS5.
// Copyright (C) 2026 Akeno Anime contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "video_preview.hpp"
#include "console_curl.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <pthread.h>
#include <curl/curl.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>

int sceKernelUsleep(std::uint32_t microseconds);
}

namespace
{
constexpr char kMaster[] = "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8";
constexpr char kAllowedOrigin[] = "https://test-streams.mux.dev/";
constexpr std::size_t kPlaylistCap = 128 * 1024;
constexpr std::size_t kSegmentCap = 8 * 1024 * 1024;

struct Buffer
{
    unsigned char *data = nullptr;
    std::size_t used = 0;
    std::size_t capacity = 0;
    bool overflow = false;

    explicit Buffer(std::size_t max) noexcept
        : data{static_cast<unsigned char *>(std::malloc(max + 1))}, capacity{max}
    {
        if (data)
            data[0] = 0;
    }
    ~Buffer()
    {
        std::free(data);
    }
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
};

std::size_t write_body(char *ptr, std::size_t sz, std::size_t nm, void *context) noexcept
{
    Buffer &buffer = *static_cast<Buffer *>(context);
    if (sz != 0 && nm > static_cast<std::size_t>(-1) / sz)
        return 0;
    const std::size_t size = sz * nm;
    if (!buffer.data || size > buffer.capacity - buffer.used)
    {
        buffer.overflow = true;
        return 0;
    }
    std::memcpy(buffer.data + buffer.used, ptr, size);
    buffer.used += size;
    buffer.data[buffer.used] = 0;
    return size;
}

int https_get(const char *url, Buffer &buf, int *http) noexcept
{
    if (std::strncmp(url, kAllowedOrigin, sizeof(kAllowedOrigin) - 1) != 0)
        return -12; // Only the public, known test-stream host is permitted.
    CURL *handle = curl_easy_init();
    if (!handle)
        return -1;
    console_curl_setup(handle);
    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, 7000L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, 20000L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "AkenoAnimePS5/0.3 test only");
    const CURLcode result = curl_easy_perform(handle);
    long code = 0;
    (void)curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(handle);
    if (http)
        *http = static_cast<int>(code);
    if (result != CURLE_OK)
        return 1000 + static_cast<int>(result);
    if (code != 200 || buf.overflow)
        return static_cast<int>(code) + 2000;
    return 0;
}

// Strict same-origin relative HLS URL expansion, rejecting path traversal.
bool resolve_url(const char *base, const char *path, char *out, std::size_t capacity) noexcept
{
    if (!path || !path[0] || std::strstr(path, "..") ||
        std::strchr(path, '#') || std::strchr(path, '\\'))
        return false;
    if (std::strncmp(path, "https://", 8) == 0)
    {
        if (std::strncmp(path, kAllowedOrigin, sizeof(kAllowedOrigin) - 1) != 0)
            return false;
        return std::snprintf(out, capacity, "%s", path) > 0 &&
               std::strlen(path) < capacity;
    }
    if (std::strstr(path, "://") || path[0] == '/')
        return false;
    const char *last_slash = std::strrchr(base, '/');
    if (!last_slash)
        return false;
    const int base_length = static_cast<int>(last_slash - base + 1);
    const int written = std::snprintf(out, capacity, "%.*s%s", base_length, base, path);
    return written > 0 && static_cast<std::size_t>(written) < capacity &&
           std::strncmp(out, kAllowedOrigin, sizeof(kAllowedOrigin) - 1) == 0;
}

// Returns a non-comment URI line. Playlist input is always NUL-terminated and bounded.
// The public sample master contains five variants, each with a relative playlist.
bool first_playlist_uri(const Buffer &manifest, char *line, std::size_t cap) noexcept
{
    if (manifest.used < 7 || std::memcmp(manifest.data, "#EXTM3U", 7) != 0 ||
        std::strstr(reinterpret_cast<const char *>(manifest.data), "#EXT-X-KEY:") != nullptr)
        return false;
    const char *p = reinterpret_cast<const char *>(manifest.data);
    const char *end = p + manifest.used;
    while (p < end)
    {
        const char *next = static_cast<const char *>(std::memchr(p, '\n', end - p));
        if (!next)
            next = end;
        const char *line_end = next;
        while (line_end > p && (line_end[-1] == '\r' || line_end[-1] == ' '))
            --line_end;
        std::size_t size = static_cast<std::size_t>(line_end - p);
        if (size > 0 && p[0] != '#' && size < cap)
        {
            std::memcpy(line, p, size);
            line[size] = 0;
            return true;
        }
        p = next < end ? next + 1 : end;
    }
    return false;
}

struct MemoryReader
{
    const unsigned char *bytes;
    int bytes_used;
    int pos;
};

int read_memory(void *context, std::uint8_t *dst, int max) noexcept
{
    auto &stream = *static_cast<MemoryReader *>(context);
    const int remaining = stream.bytes_used - stream.pos;
    if (remaining <= 0)
        return AVERROR_EOF;
    const int count = remaining < max ? remaining : max;
    std::memcpy(dst, stream.bytes + stream.pos, static_cast<std::size_t>(count));
    stream.pos += count;
    return count;
}

std::int64_t seek_memory(void *context, std::int64_t offset, int whence) noexcept
{
    auto &stream = *static_cast<MemoryReader *>(context);
    if (whence == AVSEEK_SIZE)
        return stream.bytes_used;
    const int mode = whence & ~AVSEEK_FORCE;
    std::int64_t next = offset;
    if (mode == SEEK_CUR)
        next += stream.pos;
    else if (mode == SEEK_END)
        next += stream.bytes_used;
    else if (mode != SEEK_SET)
        return -1;
    if (next < 0 || next > stream.bytes_used)
        return -1;
    stream.pos = static_cast<int>(next);
    return next;
}
} // namespace

namespace akeno
{
VideoPreview::VideoPreview() noexcept
{
    (void)pthread_mutex_init(&mutex_, nullptr);
}

bool VideoPreview::start() noexcept
{
    const VideoStage old = stage_.load(std::memory_order_acquire);
    if (old == VideoStage::fetching || old == VideoStage::demuxing ||
        old == VideoStage::playing || old == VideoStage::paused)
        return false;
    stop_.store(false);
    paused_.store(false);
    frames_.store(0);
    error_.store(0);
    http_.store(0);
    segment_bytes_.store(0);
    ts_sync_.store(0);
    demuxer_.store(0);
    av_error_.store(0);
    stream_info_error_.store(0);
    pthread_mutex_lock(&mutex_);
    frame_ready_ = false;
    pthread_mutex_unlock(&mutex_);
    stage_.store(VideoStage::fetching, std::memory_order_release);

    pthread_attr_t attr{};
    pthread_t thread{};
    if (pthread_attr_init(&attr) != 0)
    {
        stage_.store(VideoStage::error);
        error_.store(-21);
        return false;
    }
    (void)pthread_attr_setstacksize(&attr, 1024 * 1024);
    const int result = pthread_create(&thread, &attr, worker_entry, this);
    (void)pthread_attr_destroy(&attr);
    if (result != 0)
    {
        error_.store(-22);
        stage_.store(VideoStage::error);
        return false;
    }
    (void)pthread_detach(thread);
    return true;
}

void VideoPreview::stop() noexcept
{
    stop_.store(true, std::memory_order_release);
}

bool VideoPreview::should_stop() const noexcept
{
    return stop_.load(std::memory_order_acquire);
}

void VideoPreview::toggle_pause() noexcept
{
    const VideoStage stage = stage_.load(std::memory_order_acquire);
    if (stage != VideoStage::playing && stage != VideoStage::paused)
        return;
    const bool pause = !paused_.load();
    paused_.store(pause);
    stage_.store(pause ? VideoStage::paused : VideoStage::playing);
}

VideoSnapshot VideoPreview::snapshot() const noexcept
{
    return {stage_.load(std::memory_order_acquire), frames_.load(), error_.load(),
            http_.load(), segment_bytes_.load(), ts_sync_.load(),
            demuxer_.load(), av_error_.load(), stream_info_error_.load()};
}

void VideoPreview::draw_frame(ps5::demo::Canvas &canvas) noexcept
{
    pthread_mutex_lock(&mutex_);
    if (frame_ready_)
        canvas.blit_rgba(415, 265, 960, 540, pixels_, kWidth, kHeight);
    pthread_mutex_unlock(&mutex_);
}

void *VideoPreview::worker_entry(void *ptr) noexcept
{
    static_cast<VideoPreview *>(ptr)->run();
    return nullptr;
}

void VideoPreview::run() noexcept
{
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    {
        error_.store(-30);
        stage_.store(VideoStage::error);
        return;
    }
    int failure = 0;
    int http = 0;
    char variant_url[512]{};
    char segment_url[512]{};
    char relative[512]{};
    {
        Buffer master{kPlaylistCap};
        if (!master.data || (failure = https_get(kMaster, master, &http)) != 0 ||
            !first_playlist_uri(master, relative, sizeof(relative)) ||
            !resolve_url(kMaster, relative, variant_url, sizeof(variant_url)))
        {
            if (!failure)
                failure = -40;
        }
    }
    http_.store(http);
    if (failure || should_stop())
    {
        error_.store(failure);
        stage_.store(should_stop() ? VideoStage::stopped : VideoStage::error);
        curl_global_cleanup();
        return;
    }
    {
        Buffer media{kPlaylistCap};
        if (!media.data || (failure = https_get(variant_url, media, &http)) != 0 ||
            !first_playlist_uri(media, relative, sizeof(relative)) ||
            !resolve_url(variant_url, relative, segment_url, sizeof(segment_url)))
        {
            if (!failure)
                failure = -41;
        }
    }
    http_.store(http);
    if (failure || should_stop())
    {
        error_.store(failure);
        stage_.store(should_stop() ? VideoStage::stopped : VideoStage::error);
        curl_global_cleanup();
        return;
    }
    // The first unencrypted MPEG-TS segment is downloaded into a capped buffer.
    Buffer segment{kSegmentCap};
    if (!segment.data || (failure = https_get(segment_url, segment, &http)) != 0)
    {
        http_.store(http);
        error_.store(failure ? failure : -42);
        stage_.store(VideoStage::error);
        curl_global_cleanup();
        return;
    }
    http_.store(http);
    segment_bytes_.store(static_cast<unsigned>(segment.used));
    if (should_stop())
    {
        stage_.store(VideoStage::stopped);
        curl_global_cleanup();
        return;
    }

    // A valid HLS URL can return HTTP 200 with HTML, a media-playlist,
    // or a different transport format. Do not hand that to the MPEG-TS demuxer.
    const bool looks_like_ts =
        segment.used >= 3 * 188 &&
        segment.data[0] == 0x47 && segment.data[188] == 0x47 &&
        segment.data[376] == 0x47;
    ts_sync_.store(looks_like_ts ? 1 : -1);
    if (!looks_like_ts)
    {
        error_.store(-43);
        stage_.store(VideoStage::error, std::memory_order_release);
        curl_global_cleanup();
        return;
    }

    stage_.store(VideoStage::demuxing);
    MemoryReader reader{segment.data, static_cast<int>(segment.used), 0};
    AVFormatContext *format = avformat_alloc_context();
    std::uint8_t *io_buffer = static_cast<std::uint8_t *>(av_malloc(32768));
    AVIOContext *io = io_buffer
                          ? avio_alloc_context(io_buffer, 32768, 0, &reader, read_memory,
                                               nullptr, seek_memory)
                          : nullptr;
    if (!format || !io)
    {
        if (io)
            avio_context_free(&io);
        else
            av_free(io_buffer);
        avformat_free_context(format);
        error_.store(-50);
        stage_.store(VideoStage::error);
        curl_global_cleanup();
        return;
    }

    format->pb = io;
    format->flags |= AVFMT_FLAG_CUSTOM_IO;
    const AVInputFormat *ts_format = av_find_input_format("mpegts");
    demuxer_.store(ts_format ? 1 : -1);
    // Keep AVIO and the format context cleanup independent: avformat_open_input
    // can free the format context on failure and set the pointer to null.
    if (!ts_format)
    {
        failure = -50;
    }
    else
    {
        const int opened = avformat_open_input(&format, nullptr, ts_format, nullptr);
        if (opened < 0)
        {
            av_error_.store(opened);
            failure = -511; // Failed to open the MPEG-TS container.
        }
        else
        {
            const int inspected = avformat_find_stream_info(format, nullptr);
            stream_info_error_.store(inspected);
            // A single HLS segment can contain a partial elementary stream.
            // If FFmpeg already discovered streams, try its parsed codecpar.
            // The codec open step below still rejects incomplete parameters.
            if (inspected < 0 && format->nb_streams == 0)
            {
                av_error_.store(inspected);
                failure = -512;
            }
        }
    }

    int stream_index = -1;
    AVCodecContext *decoder = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *packet = nullptr;
    SwsContext *sws = nullptr;
    if (!failure)
    {
        for (unsigned i = 0; i < format->nb_streams; ++i)
        {
            if (format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                stream_index = static_cast<int>(i);
                break;
            }
        }
        if (stream_index < 0)
            failure = -52;
    }

    if (!failure)
    {
        const AVCodecParameters *parameters = format->streams[stream_index]->codecpar;
        const AVCodec *codec = avcodec_find_decoder(parameters->codec_id);
        if (!codec || !(decoder = avcodec_alloc_context3(codec)) ||
            avcodec_parameters_to_context(decoder, parameters) < 0 ||
            avcodec_open2(decoder, codec, nullptr) < 0)
            failure = -53;
    }
    if (!failure)
    {
        frame = av_frame_alloc();
        packet = av_packet_alloc();
        if (!frame || !packet)
            failure = -54;
    }

    std::array<std::uint8_t, kFrameBytes> resized{};
    std::uint8_t *destination[4] = {resized.data(), nullptr, nullptr, nullptr};
    int pitch[4] = {static_cast<int>(kWidth * 4), 0, 0, 0};
    if (!failure)
    {
        while (!should_stop() && av_read_frame(format, packet) >= 0)
        {
            if (packet->stream_index == stream_index &&
                avcodec_send_packet(decoder, packet) >= 0)
            {
                while (!should_stop() && avcodec_receive_frame(decoder, frame) == 0)
                {
                    if (!sws)
                        sws = sws_getContext(frame->width, frame->height,
                                             static_cast<AVPixelFormat>(frame->format),
                                             kWidth, kHeight, AV_PIX_FMT_RGBA, SWS_BILINEAR,
                                             nullptr, nullptr, nullptr);
                    if (!sws)
                    {
                        failure = -55;
                        break;
                    }
                    (void)sws_scale(sws, frame->data, frame->linesize, 0, frame->height,
                                    destination, pitch);
                    pthread_mutex_lock(&mutex_);
                    std::memcpy(pixels_, resized.data(), kFrameBytes);
                    frame_ready_ = true;
                    pthread_mutex_unlock(&mutex_);
                    frames_.fetch_add(1);
                    stage_.store(paused_.load() ? VideoStage::paused : VideoStage::playing);
                    while (!should_stop() && paused_.load())
                        (void)sceKernelUsleep(20000);
                    // First hardware proof: fixed 25 fps (not yet A/V synchronized).
                    (void)sceKernelUsleep(40000);
                }
            }
            av_packet_unref(packet);
            if (failure)
                break;
        }
    }
    if (sws)
        sws_freeContext(sws);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&decoder);
    if (format)
        avformat_close_input(&format);
    avio_context_free(&io);

    error_.store(failure);
    stage_.store(should_stop() ? VideoStage::stopped
                               : (failure || frames_.load() == 0 ? VideoStage::error
                                                                 : VideoStage::finished));
    curl_global_cleanup();
}
} // namespace akeno
