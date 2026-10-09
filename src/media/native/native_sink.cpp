// AKENO STREAM PS5 - Hardware decode sink (Videodec2 + Audiodec/AudioOut).
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mirrors ProsperoTV's stream adapter (iptv_player.cpp AdapterOpen and
// tv_preview.cpp's picture-mode decoder), which run on retail firmware: the
// backend is initialised per session, opened from the demuxer's format with
// a picture callback, fed Annex-B access units and ADTS/MPEG audio frames, and
// closed by the demuxer. request_stop() releases submissions blocked on full
// queues so seeks and stops never hang.
#include "media/native/native_sink.hpp"

#include "iptv_native_backend.h"

#include <atomic>
#include <cstdio>

namespace akeno::media
{
namespace
{
class NativeSink final : public DecodeSink
{
  public:
    explicit NativeSink(std::shared_ptr<FrameStore> frames) : frames_{std::move(frames)}
    {
    }

    ~NativeSink() override
    {
        shutdown();
    }

    bool init(std::string *error) override
    {
        const int32_t result = iptv_native_backend_init(&backend_);
        if (result != 0)
        {
            if (error)
                *error = "native backend init failed (" + std::to_string(result) + ")";
            return false;
        }
        initialized_ = true;
        return true;
    }

    iptv_stream_backend_t callbacks() override
    {
        iptv_stream_backend_t b{};
        b.context = this;
        b.open = [](void *self, const iptv_stream_format_t *format)
        { return static_cast<NativeSink *>(self)->open(format); };
        b.submit_video =
            [](void *self, const std::uint8_t *data, std::size_t bytes, std::uint64_t pts)
        {
            auto &s = *static_cast<NativeSink *>(self);
            return s.opened_ ? static_cast<int>(
                                   iptv_native_backend_submit_video(&s.backend_, data, bytes, pts))
                             : -1;
        };
        b.submit_audio =
            [](void *self, const std::uint8_t *data, std::size_t bytes, std::uint64_t pts)
        {
            auto &s = *static_cast<NativeSink *>(self);
            if (!s.opened_)
                return -1;
            // A failing audio track must not stop the pictures: switch audio off.
            if (iptv_native_backend_submit_audio(&s.backend_, data, bytes, pts) != 0 &&
                !s.audio_failed_)
            {
                s.audio_failed_ = true;
                (void)iptv_native_backend_disable_audio(&s.backend_);
            }
            return 0;
        };
        b.disable_audio = [](void *self)
        {
            auto &s = *static_cast<NativeSink *>(self);
            return s.opened_ ? static_cast<int>(iptv_native_backend_disable_audio(&s.backend_)) : 0;
        };
        b.select_audio = [](void *self, std::uint32_t type)
        {
            auto &s = *static_cast<NativeSink *>(self);
            return s.opened_ ? static_cast<int>(iptv_native_backend_select_audio(&s.backend_, type))
                             : -1;
        };
        b.discontinuity = [](void *self)
        {
            auto &s = *static_cast<NativeSink *>(self);
            return s.opened_ ? static_cast<int>(iptv_native_backend_discontinuity(&s.backend_))
                             : -1;
        };
        b.programme_boundary = [](void *self)
        {
            auto &s = *static_cast<NativeSink *>(self);
            return s.opened_ ? static_cast<int>(iptv_native_backend_programme_boundary(&s.backend_))
                             : 0;
        };
        b.drain = [](void *self)
        {
            auto &s = *static_cast<NativeSink *>(self);
            return s.opened_ ? static_cast<int>(iptv_native_backend_drain(&s.backend_)) : 0;
        };
        b.close = [](void *self) { static_cast<NativeSink *>(self)->close_backend(); };
        b.hardware_validated = 0;
        return b;
    }

    void set_paused(bool paused) override
    {
        if (opened_.load())
            iptv_native_backend_set_paused(&backend_, paused ? 1 : 0);
        paused_.store(paused);
    }

    void request_stop() override
    {
        cancelled_.store(true);
        if (initialized_)
            iptv_native_backend_request_stop(&backend_);
    }

    std::uint64_t presented_pts_us() const override
    {
        return opened_.load() ? iptv_native_backend_presented_pts(&backend_) : UINT64_MAX;
    }

    void fill_status(PlayerStatus &status) const override
    {
        status.decoder = "PS5 hardware decoder (Videodec2)";
        if (!initialized_)
            return;
        iptv_native_telemetry_t t{};
        if (iptv_native_backend_get_telemetry(&backend_, &t) != 0)
            return;
        status.frames_decoded = t.decoded_frames;
        status.frames_presented = t.presented_frames;
        status.frames_dropped = t.dropped_late_video_frames + t.dropped_delayed_frames;
        status.fps_x100 = t.actual_frame_rate_x100;
        status.decoder_errors = t.decoder_errors;
        status.last_native_result = t.last_native_result ? t.last_native_result : t.last_result;
        status.audio_underruns = t.audio_queue_underruns;
        status.audio_errors = t.audio_output_errors;
        if (t.software_video)
            status.decoder = "Software H.264 fallback (interlaced source)";
        if (t.audio_disabled && status.notice.find("audio") == std::string::npos)
            status.notice = status.notice.empty() ? "audio output unavailable for this stream"
                                                  : status.notice + "; audio output unavailable";
    }

    void shutdown() override
    {
        close_backend();
        initialized_ = false;
    }

  private:
    int open(const iptv_stream_format_t *f)
    {
        if (!initialized_ || !f)
            return -1;
        iptv_native_open_config_t config{};
        if (f->video_codec == IPTV_STREAM_VIDEO_H264)
            config.codec = IPTV_NATIVE_CODEC_H264;
        else if (f->video_codec == IPTV_STREAM_VIDEO_HEVC)
            config.codec = IPTV_NATIVE_CODEC_HEVC;
        else
            return -1;
        config.profile = f->video_profile;
        config.level = f->video_level;
        config.coded_width = f->coded_width;
        config.coded_height = f->coded_height;
        config.visible_width = f->visible_width;
        config.visible_height = f->visible_height;
        config.bit_depth = f->video_bit_depth;
        config.chroma_format = IPTV_NATIVE_CHROMA_420;
        config.hdr = 0;
        config.enable_audio = f->audio_pid != 0;
        config.audio_stream_type = f->audio_stream_type;
        config.picture = [](void *self, const iptv_native_picture_t *p)
        { static_cast<NativeSink *>(self)->picture(p); };
        config.picture_context = this;
        config.picture_cancelled = [](void *self)
        { return static_cast<NativeSink *>(self)->cancelled_.load() ? 1 : 0; };
        config.poll_controls = nullptr;
        config.controls_context = nullptr;
        const int32_t result = iptv_native_backend_open(&backend_, &config);
        opened_.store(result == 0);
        if (result == 0 && paused_.load())
            iptv_native_backend_set_paused(&backend_, 1);
        return static_cast<int>(result);
    }

    void picture(const iptv_native_picture_t *p)
    {
        if (!p || !frames_ || cancelled_.load())
            return;
        gfx::Nv12Picture picture;
        picture.data = static_cast<const std::uint8_t *>(p->data);
        picture.bytes = p->bytes;
        picture.pitch = p->pitch;
        picture.surface_height = p->surface_height;
        picture.width = p->width;
        picture.height = p->height;
        picture.bit_depth = p->bit_depth;
        picture.matrix = gfx::choose_matrix(p->color.matrix, p->height);
        picture.full_range = p->color.range == 2;
        (void)frames_->publish(picture, p->pts_us);
    }

    void close_backend()
    {
        if (initialized_ && (opened_.load() || !closed_))
        {
            (void)iptv_native_backend_close(&backend_);
            closed_ = true;
        }
        opened_.store(false);
    }

    std::shared_ptr<FrameStore> frames_;
    mutable iptv_native_backend_t backend_{};
    bool initialized_ = false;
    bool closed_ = false;
    bool audio_failed_ = false;
    std::atomic<bool> opened_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> cancelled_{false};
};
} // namespace

std::unique_ptr<DecodeSink> make_native_sink(std::shared_ptr<FrameStore> frames)
{
    return std::make_unique<NativeSink>(std::move(frames));
}

void set_native_volume(unsigned percent)
{
    iptv_native_set_volume(percent > 100 ? 100 : percent);
}

unsigned native_volume()
{
    return iptv_native_get_volume();
}
} // namespace akeno::media
