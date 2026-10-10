// AKENO STREAM PS5 - Host test decode sink (FFmpeg software, no pacing).
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "software_sink.hpp"

extern "C"
{
#include <libavcodec/avcodec.h>
}

#include <cstring>

namespace akeno::test
{
namespace
{
class SoftwareSink final : public media::DecodeSink
{
  public:
    SoftwareSink(std::shared_ptr<media::FrameStore> frames, std::shared_ptr<SinkRecord> record)
        : frames_{std::move(frames)}, record_{std::move(record)}
    {
        ++record_->sinks_created;
    }
    ~SoftwareSink() override
    {
        shutdown();
    }

    bool init(std::string *) override
    {
        return true;
    }

    iptv_stream_backend_t callbacks() override
    {
        iptv_stream_backend_t b{};
        b.context = this;
        b.open = [](void *self, const iptv_stream_format_t *f)
        { return static_cast<SoftwareSink *>(self)->open(f); };
        b.submit_video = [](void *self, const std::uint8_t *d, std::size_t n, std::uint64_t pts)
        { return static_cast<SoftwareSink *>(self)->video(d, n, pts); };
        b.submit_audio = [](void *self, const std::uint8_t *, std::size_t, std::uint64_t)
        {
            ++static_cast<SoftwareSink *>(self)->record_->audio_units;
            return 0;
        };
        b.disable_audio = [](void *) { return 0; };
        b.discontinuity = [](void *) { return 0; };
        b.drain = [](void *self)
        {
            auto &s = *static_cast<SoftwareSink *>(self);
            ++s.record_->drains;
            if (s.codec_)
            {
                avcodec_send_packet(s.codec_, nullptr);
                s.receive();
            }
            return 0;
        };
        b.close = [](void *self)
        {
            auto &s = *static_cast<SoftwareSink *>(self);
            ++s.record_->closes;
            s.release();
        };
        return b;
    }

    void set_paused(bool) override
    {
    }
    void request_stop() override
    {
        stop_.store(true);
    }
    std::uint64_t presented_pts_us() const override
    {
        return presented_.load();
    }
    void fill_status(media::PlayerStatus &status) const override
    {
        status.decoder = "FFmpeg software (host test)";
        status.frames_decoded = record_->frames.load();
        status.frames_presented = record_->frames.load();
    }
    void shutdown() override
    {
        release();
    }

  private:
    int open(const iptv_stream_format_t *f)
    {
        ++record_->opens;
        record_->width.store(f->visible_width);
        record_->height.store(f->visible_height);
        record_->audio_type.store(f->audio_stream_type);
        const AVCodec *codec = avcodec_find_decoder(
            f->video_codec == IPTV_STREAM_VIDEO_HEVC ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);
        codec_ = codec ? avcodec_alloc_context3(codec) : nullptr;
        if (!codec_ || avcodec_open2(codec_, codec, nullptr) < 0)
            return -1;
        frame_ = av_frame_alloc();
        packet_ = av_packet_alloc();
        return frame_ && packet_ ? 0 : -1;
    }

    int video(const std::uint8_t *data, std::size_t bytes, std::uint64_t pts)
    {
        if (stop_.load())
            return -125;
        ++record_->video_units;
        if (!codec_)
            return -1;
        av_new_packet(packet_, static_cast<int>(bytes));
        std::memcpy(packet_->data, data, bytes);
        packet_->pts =
            pts == IPTV_STREAM_PTS_UNKNOWN ? AV_NOPTS_VALUE : static_cast<std::int64_t>(pts);
        if (avcodec_send_packet(codec_, packet_) < 0)
            ++record_->decode_errors;
        av_packet_unref(packet_);
        receive();
        return 0;
    }

    void receive()
    {
        while (avcodec_receive_frame(codec_, frame_) == 0)
        {
            ++record_->frames;
            if (frame_->pts != AV_NOPTS_VALUE)
                presented_.store(static_cast<std::uint64_t>(frame_->pts));
            if (frames_ && frame_->format == AV_PIX_FMT_YUV420P)
            {
                // Repack planar 4:2:0 to NV12 as the console decoder delivers it.
                const int w = frame_->width, h = frame_->height;
                nv12_.resize(static_cast<std::size_t>(w) * (h + (h + 1) / 2));
                for (int y = 0; y < h; ++y)
                    std::memcpy(&nv12_[static_cast<std::size_t>(y) * w],
                                frame_->data[0] + y * frame_->linesize[0],
                                static_cast<std::size_t>(w));
                for (int y = 0; y < (h + 1) / 2; ++y)
                    for (int x = 0; x < w / 2; ++x)
                    {
                        nv12_[static_cast<std::size_t>(h + y) * w + 2 * x] =
                            frame_->data[1][y * frame_->linesize[1] + x];
                        nv12_[static_cast<std::size_t>(h + y) * w + 2 * x + 1] =
                            frame_->data[2][y * frame_->linesize[2] + x];
                    }
                gfx::Nv12Picture p;
                p.data = nv12_.data();
                p.bytes = nv12_.size();
                p.pitch = static_cast<std::uint32_t>(w);
                p.surface_height = static_cast<std::uint32_t>(h);
                p.width = static_cast<std::uint32_t>(w);
                p.height = static_cast<std::uint32_t>(h);
                frames_->publish(p, static_cast<std::uint64_t>(frame_->pts));
            }
            av_frame_unref(frame_);
        }
    }

    void release()
    {
        av_packet_free(&packet_);
        av_frame_free(&frame_);
        avcodec_free_context(&codec_);
    }

    std::shared_ptr<media::FrameStore> frames_;
    std::shared_ptr<SinkRecord> record_;
    AVCodecContext *codec_ = nullptr;
    AVFrame *frame_ = nullptr;
    AVPacket *packet_ = nullptr;
    std::vector<std::uint8_t> nv12_;
    std::atomic<bool> stop_{false};
    std::atomic<std::uint64_t> presented_{UINT64_MAX};
};
} // namespace

std::unique_ptr<media::DecodeSink> make_software_sink(std::shared_ptr<media::FrameStore> frames,
                                                      std::shared_ptr<SinkRecord> record)
{
    return std::make_unique<SoftwareSink>(std::move(frames), std::move(record));
}
} // namespace akeno::test
