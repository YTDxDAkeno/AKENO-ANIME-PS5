// AKENO STREAM PS5 - Step-by-step FFmpeg demux/decode self-test.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/ffmpeg_probe.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
}

namespace akeno::media
{
namespace
{
std::mutex g_probe_lock; // one probe at a time owns the log callback
std::mutex g_log_lock;
std::vector<std::string> *g_log_sink = nullptr;

void capture_log(void *avcl, int level, const char *format, va_list args)
{
    if (level > AV_LOG_WARNING)
        return;
    char line[512];
    int print_prefix = 1;
    va_list copy;
    va_copy(copy, args);
    av_log_format_line2(avcl, level, format, copy, line, sizeof(line), &print_prefix);
    va_end(copy);
    std::size_t length = std::strlen(line);
    while (length && (line[length - 1] == '\n' || line[length - 1] == '\r'))
        line[--length] = '\0';
    std::lock_guard<std::mutex> lock(g_log_lock);
    if (g_log_sink && g_log_sink->size() < 40 && length)
        g_log_sink->emplace_back(line, length);
}

struct MemoryInput
{
    const std::uint8_t *bytes;
    std::int64_t size;
    std::int64_t pos;
};

int read_packet(void *opaque, std::uint8_t *dst, int capacity)
{
    auto &in = *static_cast<MemoryInput *>(opaque);
    const std::int64_t remaining = in.size - in.pos;
    if (remaining <= 0)
        return AVERROR_EOF;
    const int count = remaining < capacity ? static_cast<int>(remaining) : capacity;
    std::memcpy(dst, in.bytes + in.pos, static_cast<std::size_t>(count));
    in.pos += count;
    return count;
}

std::int64_t seek(void *opaque, std::int64_t offset, int whence)
{
    auto &in = *static_cast<MemoryInput *>(opaque);
    if (whence & AVSEEK_SIZE)
        return in.size;
    std::int64_t next;
    switch (whence & ~AVSEEK_FORCE)
    {
    case SEEK_SET:
        next = offset;
        break;
    case SEEK_CUR:
        next = in.pos + offset;
        break;
    case SEEK_END:
        next = in.size + offset;
        break;
    default:
        return AVERROR(EINVAL);
    }
    if (next < 0 || next > in.size)
        return AVERROR(EINVAL);
    in.pos = next;
    return next;
}

std::string error_string(int error)
{
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    if (av_strerror(error, text, sizeof(text)) < 0)
        std::snprintf(text, sizeof(text), "FFmpeg error %d", error);
    return text;
}
} // namespace

const char *stage_name(ProbeStage stage) noexcept
{
    switch (stage)
    {
    case ProbeStage::not_run:
        return "not run";
    case ProbeStage::input:
        return "input";
    case ProbeStage::open_input:
        return "open input (avformat_open_input)";
    case ProbeStage::stream_info:
        return "stream info (avformat_find_stream_info)";
    case ProbeStage::find_stream:
        return "find video stream";
    case ProbeStage::open_decoder:
        return "open decoder (avcodec_open2)";
    case ProbeStage::decode:
        return "decode frames";
    case ProbeStage::complete:
        return "complete";
    }
    return "unknown";
}

std::string ProbeReport::summary() const
{
    char text[256];
    if (success)
        std::snprintf(text, sizeof(text), "OK: %s %s %dx%d %s, %u frames decoded",
                      container.c_str(), video_codec.c_str(), width, height, pixel_format.c_str(),
                      frames_decoded);
    else
    {
        // The failing stage is the one after the last stage reached.
        const auto failed = static_cast<ProbeStage>(static_cast<int>(reached) + 1);
        std::snprintf(text, sizeof(text), "FAILED at %s: %s (%d)", stage_name(failed),
                      error_text.c_str(), error);
    }
    return text;
}

std::string ffmpeg_build_info()
{
    std::string info = "FFmpeg ";
    info += av_version_info();
    info += " (lavf ";
    info +=
        std::to_string(LIBAVFORMAT_VERSION_MAJOR) + "." + std::to_string(LIBAVFORMAT_VERSION_MINOR);
    info += ", lavc ";
    info += std::to_string(LIBAVCODEC_VERSION_MAJOR) + "." +
            std::to_string(LIBAVCODEC_VERSION_MINOR) + ")";
    return info;
}

ProbeReport probe_media(const std::uint8_t *bytes, std::size_t size, const ProbeOptions &options)
{
    std::lock_guard<std::mutex> probe_lock(g_probe_lock);
    ProbeReport report;
    report.ffmpeg_version = ffmpeg_build_info();
    {
        std::lock_guard<std::mutex> lock(g_log_lock);
        g_log_sink = &report.log;
    }
    av_log_set_callback(capture_log);

    AVFormatContext *format = nullptr;
    AVIOContext *io = nullptr;
    AVCodecContext *video = nullptr;
    AVCodecContext *audio = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    MemoryInput input{bytes, static_cast<std::int64_t>(size), 0};
    int video_index = -1, audio_index = -1;

    const auto fail = [&](int error)
    {
        report.error = error;
        report.error_text = error_string(error);
    };

    do
    {
        if (!bytes || size < 188)
        {
            fail(AVERROR_INVALIDDATA);
            break;
        }
        report.reached = ProbeStage::input;

        constexpr int kIoBuffer = 64 * 1024;
        auto *io_buffer = static_cast<std::uint8_t *>(av_malloc(kIoBuffer));
        io = io_buffer
                 ? avio_alloc_context(io_buffer, kIoBuffer, 0, &input, read_packet, nullptr, seek)
                 : nullptr;
        format = avformat_alloc_context();
        if (!io || !format)
        {
            if (!io)
                av_free(io_buffer);
            fail(AVERROR(ENOMEM));
            break;
        }
        format->pb = io;
        format->flags |= AVFMT_FLAG_CUSTOM_IO;
        const AVInputFormat *forced =
            options.format ? av_find_input_format(options.format) : nullptr;
        if (options.format && !forced)
        {
            fail(AVERROR_DEMUXER_NOT_FOUND);
            break;
        }
        int result = avformat_open_input(&format, nullptr, forced, nullptr);
        if (result < 0)
        {
            // avformat_open_input freed the context on failure.
            fail(result);
            break;
        }
        report.reached = ProbeStage::open_input;
        report.container = format->iformat ? format->iformat->name : "";

        if (options.call_find_stream_info)
        {
            result = avformat_find_stream_info(format, nullptr);
            report.stream_info_result = result;
            if (result < 0 && format->nb_streams == 0)
            {
                fail(result);
                break;
            }
        }
        report.reached = ProbeStage::stream_info;

        video_index = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        audio_index = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, video_index, nullptr, 0);
        if (video_index < 0)
        {
            fail(video_index);
            break;
        }
        report.reached = ProbeStage::find_stream;
        const AVCodecParameters *vpar = format->streams[video_index]->codecpar;
        report.video_codec = avcodec_get_name(vpar->codec_id);
        if (audio_index >= 0)
        {
            const AVCodecParameters *apar = format->streams[audio_index]->codecpar;
            report.audio_codec = avcodec_get_name(apar->codec_id);
            report.audio_sample_rate = apar->sample_rate;
            report.audio_channels = apar->ch_layout.nb_channels;
        }

        const AVCodec *codec = avcodec_find_decoder(vpar->codec_id);
        if (!codec)
        {
            fail(AVERROR_DECODER_NOT_FOUND);
            break;
        }
        video = avcodec_alloc_context3(codec);
        if (!video)
        {
            fail(AVERROR(ENOMEM));
            break;
        }
        result = avcodec_parameters_to_context(video, vpar);
        if (result >= 0)
            result = avcodec_open2(video, codec, nullptr);
        if (result < 0)
        {
            fail(result);
            break;
        }
        if (audio_index >= 0)
        {
            const AVCodecParameters *apar = format->streams[audio_index]->codecpar;
            if (const AVCodec *acodec = avcodec_find_decoder(apar->codec_id))
            {
                audio = avcodec_alloc_context3(acodec);
                if (audio && (avcodec_parameters_to_context(audio, apar) < 0 ||
                              avcodec_open2(audio, acodec, nullptr) < 0))
                    avcodec_free_context(&audio);
            }
        }
        report.reached = ProbeStage::open_decoder;

        packet = av_packet_alloc();
        frame = av_frame_alloc();
        if (!packet || !frame)
        {
            fail(AVERROR(ENOMEM));
            break;
        }
        int last_decode_error = 0;
        bool eof = false;
        while (report.frames_decoded < options.max_video_frames)
        {
            if (!eof)
            {
                result = av_read_frame(format, packet);
                if (result < 0)
                {
                    eof = true;
                    avcodec_send_packet(video, nullptr); // drain
                }
                else
                {
                    ++report.packets_read;
                    AVCodecContext *target = packet->stream_index == video_index ? video
                                             : (audio && packet->stream_index == audio_index)
                                                 ? audio
                                                 : nullptr;
                    if (target)
                    {
                        const int sent = avcodec_send_packet(target, packet);
                        if (sent < 0 && sent != AVERROR(EAGAIN))
                            last_decode_error = sent;
                        while (target == audio && avcodec_receive_frame(audio, frame) == 0)
                            ++report.audio_frames_decoded;
                    }
                    av_packet_unref(packet);
                }
            }
            bool progressed = false;
            while (report.frames_decoded < options.max_video_frames)
            {
                const int got = avcodec_receive_frame(video, frame);
                if (got == AVERROR(EAGAIN) || got == AVERROR_EOF)
                    break;
                if (got < 0)
                {
                    last_decode_error = got;
                    break;
                }
                progressed = true;
                ++report.frames_decoded;
                report.width = frame->width;
                report.height = frame->height;
                const char *name = av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame->format));
                report.pixel_format = name ? name : "?";
                av_frame_unref(frame);
            }
            if (eof && !progressed)
                break;
        }
        if (report.frames_decoded == 0)
        {
            fail(last_decode_error ? last_decode_error : AVERROR_INVALIDDATA);
            break;
        }
        report.reached = ProbeStage::complete;
        report.success = true;
    } while (false);

    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&audio);
    avcodec_free_context(&video);
    if (format)
        avformat_close_input(&format);
    if (io)
    {
        av_freep(&io->buffer);
        avio_context_free(&io);
    }
    av_log_set_callback(av_log_default_callback);
    {
        std::lock_guard<std::mutex> lock(g_log_lock);
        g_log_sink = nullptr;
    }
    return report;
}
} // namespace akeno::media
