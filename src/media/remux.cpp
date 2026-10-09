// AKENO STREAM PS5 - Local files to MPEG-TS for the native player.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/remux.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
}

namespace akeno::media
{
namespace
{
constexpr int kIoBytes = 256 * 1024;

std::string av_error(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof(text));
    return text;
}

bool audio_supported(AVCodecID id)
{
    switch (id)
    {
    // MP3 is not listed: the native demuxer frames MPEG audio Layer II only.
    case AV_CODEC_ID_AAC:
    case AV_CODEC_ID_AAC_LATM:
    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_AC3:
    case AV_CODEC_ID_EAC3:
        return true;
    default:
        return false;
    }
}
} // namespace

struct Remuxer::Impl
{
    int fd = -1;
    std::int64_t file_size = 0;
    AVIOContext *in_io = nullptr;
    AVFormatContext *in = nullptr;
    AVIOContext *out_io = nullptr;
    AVFormatContext *out = nullptr;
    int video_in = -1, audio_in = -1;
    int video_out = -1, audio_out = -1;
    bool header_written = false;
    const Sink *sink = nullptr;
    bool sink_refused = false;
    RemuxInfo info;

    static int read(void *opaque, std::uint8_t *buffer, int size)
    {
        auto *self = static_cast<Impl *>(opaque);
        const ssize_t got = ::read(self->fd, buffer, static_cast<std::size_t>(size));
        if (got == 0)
            return AVERROR_EOF;
        return got < 0 ? AVERROR(errno) : static_cast<int>(got);
    }

    static std::int64_t seek(void *opaque, std::int64_t offset, int whence)
    {
        auto *self = static_cast<Impl *>(opaque);
        if (whence & AVSEEK_SIZE)
            return self->file_size;
        const off_t result = ::lseek(self->fd, static_cast<off_t>(offset), whence & ~AVSEEK_FORCE);
        return result < 0 ? AVERROR(errno) : static_cast<std::int64_t>(result);
    }

    static int write(void *opaque, const std::uint8_t *buffer, int size)
    {
        auto *self = static_cast<Impl *>(opaque);
        if (!self->sink || self->sink_refused)
            return AVERROR_EXIT;
        if (!(*self->sink)(buffer, static_cast<std::size_t>(size)))
        {
            self->sink_refused = true;
            return AVERROR_EXIT;
        }
        return size;
    }

    void release()
    {
        if (out)
        {
            if (out_io)
            {
                av_freep(&out_io->buffer);
                avio_context_free(&out_io);
            }
            avformat_free_context(out);
            out = nullptr;
        }
        if (in)
            avformat_close_input(&in);
        if (in_io)
        {
            av_freep(&in_io->buffer);
            avio_context_free(&in_io);
        }
        if (fd >= 0)
        {
            ::close(fd);
            fd = -1;
        }
        header_written = false;
        video_in = audio_in = video_out = audio_out = -1;
    }
};

Remuxer::Remuxer() : impl_{std::make_unique<Impl>()}
{
}
Remuxer::~Remuxer()
{
    close();
}

const RemuxInfo &Remuxer::info() const noexcept
{
    return impl_->info;
}

void Remuxer::close()
{
    impl_->release();
}

bool Remuxer::open(const std::string &path, std::string *error)
{
    close();
    Impl &m = *impl_;
    m.info = {};
    const auto fail = [&](std::string why)
    {
        if (error)
            *error = std::move(why);
        m.release();
        return false;
    };
    m.fd = ::open(path.c_str(), O_RDONLY);
    if (m.fd < 0)
        return fail(std::string{"cannot open file: "} + std::strerror(errno));
    struct stat facts
    {
    };
    m.file_size = fstat(m.fd, &facts) == 0 ? static_cast<std::int64_t>(facts.st_size) : -1;

    auto *buffer = static_cast<std::uint8_t *>(av_malloc(kIoBytes));
    m.in_io = buffer ? avio_alloc_context(buffer, kIoBytes, 0, &m, Impl::read, nullptr, Impl::seek)
                     : nullptr;
    m.in = avformat_alloc_context();
    if (!m.in_io || !m.in)
    {
        if (!m.in_io)
            av_free(buffer);
        return fail("out of memory");
    }
    m.in->pb = m.in_io;
    m.in->flags |= AVFMT_FLAG_CUSTOM_IO;
    int result = avformat_open_input(&m.in, nullptr, nullptr, nullptr);
    if (result < 0)
        return fail("unrecognised or damaged file (" + av_error(result) + ")");
    result = avformat_find_stream_info(m.in, nullptr);
    if (result < 0 && m.in->nb_streams == 0)
        return fail("no streams found (" + av_error(result) + ")");
    m.info.container = m.in->iformat ? m.in->iformat->name : "";
    if (m.in->duration > 0)
        m.info.duration = static_cast<double>(m.in->duration) / AV_TIME_BASE;

    m.video_in = av_find_best_stream(m.in, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m.video_in < 0)
        return fail("the file has no video stream");
    const AVCodecParameters *video = m.in->streams[m.video_in]->codecpar;
    m.info.video_codec = avcodec_get_name(video->codec_id);
    m.info.width = video->width;
    m.info.height = video->height;
    if (video->codec_id != AV_CODEC_ID_H264 && video->codec_id != AV_CODEC_ID_HEVC)
        return fail("video codec " + m.info.video_codec + " is not supported (H.264 or HEVC only)");
    m.audio_in = av_find_best_stream(m.in, AVMEDIA_TYPE_AUDIO, -1, m.video_in, nullptr, 0);
    if (m.audio_in >= 0)
    {
        const AVCodecParameters *audio = m.in->streams[m.audio_in]->codecpar;
        if (audio_supported(audio->codec_id))
        {
            m.info.audio_codec = avcodec_get_name(audio->codec_id);
            m.info.audio_rate = audio->sample_rate;
            m.info.audio_channels = audio->ch_layout.nb_channels;
        }
        else
        {
            m.info.notice = std::string{"audio codec "} + avcodec_get_name(audio->codec_id) +
                            " is not supported; playing video only";
            m.audio_in = -1;
        }
    }
    else
    {
        m.info.notice = "the file has no audio track";
    }

    result = avformat_alloc_output_context2(&m.out, nullptr, "mpegts", nullptr);
    if (result < 0 || !m.out)
        return fail("MPEG-TS muxer unavailable (" + av_error(result) + ")");
    auto *out_buffer = static_cast<std::uint8_t *>(av_malloc(kIoBytes));
    m.out_io = out_buffer
                   ? avio_alloc_context(out_buffer, kIoBytes, 1, &m, nullptr, Impl::write, nullptr)
                   : nullptr;
    if (!m.out_io)
    {
        av_free(out_buffer);
        return fail("out of memory");
    }
    m.out->pb = m.out_io;
    m.out->flags |= AVFMT_FLAG_CUSTOM_IO;
    for (int index : {m.video_in, m.audio_in})
    {
        if (index < 0)
            continue;
        AVStream *stream = avformat_new_stream(m.out, nullptr);
        if (!stream ||
            avcodec_parameters_copy(stream->codecpar, m.in->streams[index]->codecpar) < 0)
            return fail("cannot configure the MPEG-TS muxer");
        stream->codecpar->codec_tag = 0;
        stream->time_base = m.in->streams[index]->time_base;
        (index == m.video_in ? m.video_out : m.audio_out) = stream->index;
    }
    return true;
}

bool Remuxer::seek(double seconds)
{
    Impl &m = *impl_;
    if (!m.in || seconds <= 0.0)
        return true;
    const std::int64_t target = static_cast<std::int64_t>(seconds * AV_TIME_BASE);
    return av_seek_frame(m.in, -1, target, AVSEEK_FLAG_BACKWARD) >= 0;
}

int Remuxer::run(const Sink &sink, const std::atomic<bool> &stop, std::string *error,
                 std::atomic<double> *first_video_seconds)
{
    Impl &m = *impl_;
    const auto fail = [&](std::string why)
    {
        if (error)
            *error = std::move(why);
        return -1;
    };
    if (!m.in || !m.out)
        return fail("not open");
    m.sink = &sink;
    m.sink_refused = false;
    if (!m.header_written)
    {
        const int result = avformat_write_header(m.out, nullptr);
        if (result < 0)
            return m.sink_refused ? 0 : fail("MPEG-TS header failed (" + av_error(result) + ")");
        m.header_written = true;
    }
    AVPacket *packet = av_packet_alloc();
    if (!packet)
        return fail("out of memory");
    int outcome = 1;
    bool first_video = true;
    while (!stop.load(std::memory_order_relaxed))
    {
        int result = av_read_frame(m.in, packet);
        if (result == AVERROR_EOF)
            break;
        if (result < 0)
        {
            outcome = fail("read error (" + av_error(result) + ")");
            break;
        }
        int target = -1;
        if (packet->stream_index == m.video_in)
        {
            target = m.video_out;
            const AVStream *in_stream = m.in->streams[m.video_in];
            if (first_video && first_video_seconds && packet->pts != AV_NOPTS_VALUE)
            {
                const std::int64_t origin =
                    in_stream->start_time != AV_NOPTS_VALUE ? in_stream->start_time : 0;
                first_video_seconds->store(
                    std::max(0.0, (packet->pts - origin) * av_q2d(in_stream->time_base)));
                first_video = false;
            }
        }
        else if (packet->stream_index == m.audio_in)
            target = m.audio_out;
        if (target < 0)
        {
            av_packet_unref(packet);
            continue;
        }
        av_packet_rescale_ts(packet, m.in->streams[packet->stream_index]->time_base,
                             m.out->streams[target]->time_base);
        packet->stream_index = target;
        packet->pos = -1;
        result = av_interleaved_write_frame(m.out, packet);
        if (result < 0)
        {
            outcome = m.sink_refused ? 0 : fail("mux error (" + av_error(result) + ")");
            break;
        }
    }
    av_packet_free(&packet);
    if (stop.load())
        outcome = 0;
    if (outcome == 1)
    {
        av_write_trailer(m.out);
        avio_flush(m.out_io);
        if (m.sink_refused)
            outcome = 0;
    }
    m.sink = nullptr;
    return outcome;
}

bool is_playable_extension(const std::string &path)
{
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return false;
    std::string ext = path.substr(dot + 1);
    for (char &c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    static constexpr const char *kExtensions[] = {"ts", "m2ts", "mts", "mp4", "m4v", "mkv", "mov"};
    return std::any_of(std::begin(kExtensions), std::end(kExtensions),
                       [&](const char *e) { return ext == e; });
}
} // namespace akeno::media
