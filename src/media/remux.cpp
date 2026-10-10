// AKENO STREAM PS5 - Containers to MPEG-TS for the native player.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/remux.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>

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

// FFmpeg lists every name a demuxer answers to ("mov,mp4,m4a,...").
std::string container_name(const std::string &demuxer)
{
    if (demuxer.rfind("mov,", 0) == 0)
        return "MP4";
    if (demuxer.rfind("matroska", 0) == 0)
        return "Matroska";
    if (demuxer == "mpegts")
        return "MPEG-TS";
    return demuxer;
}

// One demuxed input on a ByteSource.
struct Input
{
    std::unique_ptr<ByteSource> source;
    AVIOContext *io = nullptr;
    AVFormatContext *format = nullptr;
    AVPacket *pending = nullptr; // next packet of a selected stream, not yet written
    bool ended = false;

    static int read(void *opaque, std::uint8_t *buffer, int size)
    {
        auto *self = static_cast<Input *>(opaque);
        const int got = self->source->read(buffer, size);
        if (got > 0)
            return got;
        return got == 0 ? AVERROR_EOF : AVERROR_EXIT;
    }

    static std::int64_t seek(void *opaque, std::int64_t offset, int whence)
    {
        auto *self = static_cast<Input *>(opaque);
        ByteSource &s = *self->source;
        if (whence & AVSEEK_SIZE)
            return s.size() >= 0 ? s.size() : AVERROR(ENOSYS);
        whence &= ~AVSEEK_FORCE;
        std::int64_t target = offset;
        if (whence == SEEK_CUR)
            target = s.position() + offset;
        else if (whence == SEEK_END)
        {
            if (s.size() < 0)
                return AVERROR(ENOSYS);
            target = s.size() + offset;
        }
        return s.seek(target) ? target : AVERROR(EIO);
    }

    bool open(std::unique_ptr<ByteSource> from, std::string *error)
    {
        source = std::move(from);
        auto *buffer = static_cast<std::uint8_t *>(av_malloc(kIoBytes));
        io = buffer ? avio_alloc_context(buffer, kIoBytes, 0, this, read, nullptr,
                                         source->seekable() ? seek : nullptr)
                    : nullptr;
        format = avformat_alloc_context();
        if (!io || !format)
        {
            if (!io)
                av_free(buffer);
            *error = "out of memory";
            return false;
        }
        if (!source->seekable())
            io->seekable = 0;
        format->pb = io;
        format->flags |= AVFMT_FLAG_CUSTOM_IO;
        int result = avformat_open_input(&format, nullptr, nullptr, nullptr);
        if (result < 0)
        {
            *error = source->error().empty()
                         ? "unrecognised or damaged media (" + av_error(result) + ")"
                         : source->error();
            return false;
        }
        result = avformat_find_stream_info(format, nullptr);
        if (result < 0 && format->nb_streams == 0)
        {
            *error = "no streams found (" + av_error(result) + ")";
            return false;
        }
        pending = av_packet_alloc();
        if (!pending)
        {
            *error = "out of memory";
            return false;
        }
        return true;
    }

    void release()
    {
        av_packet_free(&pending);
        if (format)
            avformat_close_input(&format);
        if (io)
        {
            av_freep(&io->buffer);
            avio_context_free(&io);
        }
        source.reset();
        ended = false;
    }
};
} // namespace

struct Remuxer::Impl
{
    Input inputs[2];
    int input_count = 0;
    // Selected streams: input index and stream index for video and audio.
    int video_input = 0, video_stream = -1;
    int audio_input = -1, audio_stream = -1;
    AVIOContext *out_io = nullptr;
    AVFormatContext *out = nullptr;
    int video_out = -1, audio_out = -1;
    bool header_written = false;
    const Sink *sink = nullptr;
    bool sink_refused = false;
    RemuxInfo info;

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
        for (Input &input : inputs)
            input.release();
        input_count = 0;
        header_written = false;
        video_stream = audio_stream = audio_input = video_out = audio_out = -1;
    }

    AVStream *stream(int input, int index) const
    {
        return inputs[input].format->streams[index];
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

bool Remuxer::seekable() const
{
    return impl_->input_count > 0 && impl_->inputs[0].source->seekable();
}

void Remuxer::close()
{
    impl_->release();
}

bool Remuxer::open(const std::string &path, std::string *error)
{
    auto source = open_file_source(path, error);
    if (!source)
        return false;
    return open(std::move(source), nullptr, error);
}

bool Remuxer::open(std::unique_ptr<ByteSource> main, std::unique_ptr<ByteSource> audio,
                   std::string *error)
{
    close();
    Impl &m = *impl_;
    m.info = {};
    std::string why;
    const auto fail = [&](std::string message)
    {
        if (error)
            *error = std::move(message);
        m.release();
        return false;
    };
    if (!main || !m.inputs[0].open(std::move(main), &why))
        return fail(why.empty() ? "no input" : why);
    m.input_count = 1;
    AVFormatContext *in = m.inputs[0].format;
    m.info.container = container_name(in->iformat ? in->iformat->name : "");
    if (in->duration > 0)
        m.info.duration = static_cast<double>(in->duration) / AV_TIME_BASE;

    m.video_stream = av_find_best_stream(in, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m.video_stream < 0)
        return fail("no video stream");
    const AVCodecParameters *video = in->streams[m.video_stream]->codecpar;
    m.info.video_codec = avcodec_get_name(video->codec_id);
    m.info.width = video->width;
    m.info.height = video->height;
    if (video->codec_id != AV_CODEC_ID_H264 && video->codec_id != AV_CODEC_ID_HEVC)
        return fail("video codec " + m.info.video_codec + " is not supported (H.264 or HEVC only)");

    if (audio)
    {
        if (!m.inputs[1].open(std::move(audio), &why))
            m.info.notice = "the audio track could not be opened (" + why + "); playing video only";
        else
        {
            m.input_count = 2;
            const int index =
                av_find_best_stream(m.inputs[1].format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
            if (index >= 0)
            {
                m.audio_input = 1;
                m.audio_stream = index;
            }
        }
    }
    if (m.audio_input < 0)
    {
        const int index =
            av_find_best_stream(in, AVMEDIA_TYPE_AUDIO, -1, m.video_stream, nullptr, 0);
        if (index >= 0)
        {
            m.audio_input = 0;
            m.audio_stream = index;
        }
    }
    if (m.audio_input >= 0)
    {
        const AVCodecParameters *a = m.stream(m.audio_input, m.audio_stream)->codecpar;
        if (audio_supported(a->codec_id))
        {
            m.info.audio_codec = avcodec_get_name(a->codec_id);
            m.info.audio_rate = a->sample_rate;
            m.info.audio_channels = a->ch_layout.nb_channels;
        }
        else
        {
            m.info.notice = std::string{"audio codec "} + avcodec_get_name(a->codec_id) +
                            " is not supported; playing video only";
            m.audio_input = m.audio_stream = -1;
        }
    }
    else if (m.info.notice.empty())
    {
        m.info.notice = "no audio track";
    }

    int result = avformat_alloc_output_context2(&m.out, nullptr, "mpegts", nullptr);
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
    const auto add = [&](int input, int index, int *out_index)
    {
        AVStream *source = m.stream(input, index);
        AVStream *stream = avformat_new_stream(m.out, nullptr);
        if (!stream || avcodec_parameters_copy(stream->codecpar, source->codecpar) < 0)
            return false;
        stream->codecpar->codec_tag = 0;
        stream->time_base = source->time_base;
        *out_index = stream->index;
        return true;
    };
    if (!add(0, m.video_stream, &m.video_out) ||
        (m.audio_input >= 0 && !add(m.audio_input, m.audio_stream, &m.audio_out)))
        return fail("cannot configure the MPEG-TS muxer");
    return true;
}

bool Remuxer::seek(double seconds)
{
    Impl &m = *impl_;
    if (!m.input_count || seconds <= 0.0)
        return true;
    const std::int64_t target = static_cast<std::int64_t>(seconds * AV_TIME_BASE);
    bool ok = av_seek_frame(m.inputs[0].format, -1, target, AVSEEK_FLAG_BACKWARD) >= 0;
    if (m.input_count == 2)
        ok = av_seek_frame(m.inputs[1].format, -1, target, AVSEEK_FLAG_BACKWARD) >= 0 && ok;
    return ok;
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
    if (!m.input_count || !m.out)
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
    // Fills an input's pending packet with its next video or audio packet.
    const auto refill = [&](int i) -> int
    {
        Input &input = m.inputs[i];
        while (!input.ended && input.pending->size == 0)
        {
            const int result = av_read_frame(input.format, input.pending);
            if (result == AVERROR_EOF)
            {
                input.ended = true;
                break;
            }
            if (result < 0)
                return result;
            const int s = input.pending->stream_index;
            const bool wanted =
                (i == 0 && s == m.video_stream) || (i == m.audio_input && s == m.audio_stream);
            if (!wanted)
                av_packet_unref(input.pending);
        }
        return 0;
    };
    const auto time_of = [&](int i)
    {
        const AVPacket *p = m.inputs[i].pending;
        const std::int64_t t = p->dts != AV_NOPTS_VALUE ? p->dts : p->pts;
        if (t == AV_NOPTS_VALUE)
            return -1.0e18;
        return t * av_q2d(m.inputs[i].format->streams[p->stream_index]->time_base);
    };

    int outcome = 1;
    bool first_video = true;
    while (!stop.load(std::memory_order_relaxed))
    {
        int result = 0;
        int failed = 0;
        for (int i = 0; i < m.input_count && result >= 0; ++i)
        {
            result = refill(i);
            failed = i;
        }
        if (result < 0)
        {
            const std::string &source_error = m.inputs[failed].source->error();
            outcome = result == AVERROR_EXIT && stop.load()
                          ? 0
                          : fail(source_error.empty() ? "read error (" + av_error(result) + ")"
                                                      : source_error);
            break;
        }
        // The earliest pending packet goes next.
        int next = -1;
        for (int i = 0; i < m.input_count; ++i)
            if (m.inputs[i].pending->size > 0 && (next < 0 || time_of(i) < time_of(next)))
                next = i;
        if (next < 0)
            break; // every input has ended
        AVPacket *packet = m.inputs[next].pending;
        const AVStream *in_stream = m.stream(next, packet->stream_index);
        const bool is_video = next == 0 && packet->stream_index == m.video_stream;
        const int target = is_video ? m.video_out : m.audio_out;
        if (is_video && first_video && first_video_seconds && packet->pts != AV_NOPTS_VALUE)
        {
            const std::int64_t origin =
                in_stream->start_time != AV_NOPTS_VALUE ? in_stream->start_time : 0;
            first_video_seconds->store(
                std::max(0.0, (packet->pts - origin) * av_q2d(in_stream->time_base)));
            first_video = false;
        }
        av_packet_rescale_ts(packet, in_stream->time_base, m.out->streams[target]->time_base);
        packet->stream_index = target;
        packet->pos = -1;
        result = av_interleaved_write_frame(m.out, packet); // takes the packet's data
        av_packet_unref(packet);
        if (result < 0)
        {
            outcome = m.sink_refused ? 0 : fail("mux error (" + av_error(result) + ")");
            break;
        }
    }
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
