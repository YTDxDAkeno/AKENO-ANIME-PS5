/* AKENO STREAM PS5 - The ProsperoTV full-screen AGC presenter, switched off.
 * Copyright (C) 2026 AKENO STREAM contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The vendored native backend (third_party/prosperotv) can either present
 * decoded pictures itself through the GPU (AGC + its own VideoOut handle) or
 * hand each paced NV12 picture to a callback. AKENO STREAM always uses the
 * callback: the application's renderer keeps the one VideoOut handle for the
 * whole session and composites video with its own interface. With a picture
 * callback configured the backend guards every presenter call, so these
 * definitions only satisfy the linker. Should one be reached anyway, it
 * reports failure instead of touching the display. */

#include "iptv_native_agc_present.h"

#define AKENO_AGC_PRESENTER_DISABLED (-1100)

void iptv_native_agc_set_osd(iptv_native_osd_t draw, void *context)
{
    (void)draw;
    (void)context;
}

int32_t iptv_native_agc_present_nv12(const void *source, size_t source_bytes, uint32_t pitch,
                                     uint32_t surface_height, uint32_t visible_width,
                                     uint32_t visible_height,
                                     const iptv_native_video_overlay_t *overlay)
{
    (void)source;
    (void)source_bytes;
    (void)pitch;
    (void)surface_height;
    (void)visible_width;
    (void)visible_height;
    (void)overlay;
    return AKENO_AGC_PRESENTER_DISABLED;
}

int32_t iptv_native_agc_present_nv12_deferred(const void *source, size_t source_bytes,
                                              uint32_t pitch, uint32_t surface_height,
                                              uint32_t visible_width, uint32_t visible_height,
                                              const iptv_native_video_overlay_t *overlay)
{
    return iptv_native_agc_present_nv12(source, source_bytes, pitch, surface_height, visible_width,
                                        visible_height, overlay);
}

int32_t iptv_native_agc_present_yuv_deferred(const void *source, size_t source_bytes,
                                             uint32_t pitch, uint32_t surface_height,
                                             uint32_t visible_width, uint32_t visible_height,
                                             uint32_t bit_depth,
                                             const iptv_native_video_overlay_t *overlay)
{
    (void)bit_depth;
    return iptv_native_agc_present_nv12(source, source_bytes, pitch, surface_height, visible_width,
                                        visible_height, overlay);
}

int32_t iptv_native_agc_present_finish_frame(void)
{
    return AKENO_AGC_PRESENTER_DISABLED;
}

int32_t iptv_native_agc_loading_start(void)
{
    return AKENO_AGC_PRESENTER_DISABLED;
}

void iptv_native_agc_loading_stop(void)
{
}

void iptv_native_agc_set_overlay_enabled(int enabled)
{
    (void)enabled;
}

int iptv_native_agc_overlay_enabled(void)
{
    return 0;
}

int32_t iptv_native_agc_present_drain(void)
{
    return 0;
}

void iptv_native_agc_present_set_cancelled(int cancelled)
{
    (void)cancelled;
}

int32_t iptv_native_agc_present_shutdown(void)
{
    return 0;
}

int iptv_native_agc_hdr_active(void)
{
    return 0;
}

void iptv_native_agc_set_color_test(int force_sdr, int sample_output)
{
    (void)force_sdr;
    (void)sample_output;
}
