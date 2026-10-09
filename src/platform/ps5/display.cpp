// AKENO STREAM PS5 - VideoOut presentation of CPU-composed frames.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The VideoOut setup (WC direct memory, RGBA8 sRGB, tiling mode 0, two
// registered buffers, flip on vblank) and the tile address function are the
// hardware-validated ones from ps5-native-app-boilerplate (BlackBearReloaded,
// GPL-3.0-or-later). The application composes each frame in ordinary cached
// memory; present() converts it to the tiled layout one 4x4 micro-tile at a
// time. In this layout a 4x4 micro-tile is exactly one contiguous 64-byte
// line (x bits 0-1 select bytes 2-3, y bits 0-1 select bytes 4-5), so every
// store fills a whole write-combining line.
#include "platform/display.hpp"

#include "platform/platform.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C"
{
    std::size_t sceKernelGetDirectMemorySize();
    int sceKernelAllocateDirectMemory(std::int64_t search_start, std::int64_t search_end,
                                      std::size_t length, std::size_t alignment, int memory_type,
                                      std::int64_t *physical_address);
    int sceKernelMapDirectMemory(void **address, std::size_t length, int protection, int flags,
                                 std::int64_t physical_address, std::size_t alignment);
    int sceSystemServiceHideSplashScreen();
    int sceVideoOutOpen(std::int32_t user_id, std::int32_t bus_type, std::int32_t index,
                        const void *param);
    int sceVideoOutSetFlipRate(std::int32_t handle, std::int32_t rate);
    int sceVideoOutSubmitFlip(std::int32_t handle, std::int32_t buffer_index,
                              std::uint32_t flip_mode, std::int64_t flip_argument);
    int sceVideoOutWaitVblank(std::int32_t handle);
}

namespace akeno::platform
{
namespace
{
constexpr unsigned kW = Display::kWidth;
constexpr unsigned kH = Display::kHeight;
constexpr std::size_t kFrameBytes = 0x1000000;
constexpr std::size_t kMemoryBytes = kFrameBytes * 2;
constexpr std::size_t kAlignment = 0x200000;
constexpr int kMemoryTypeWcGarlic = 3;
constexpr int kMapProtection = 0x33;
constexpr std::uint64_t kPixelFormatRgba8Srgb = UINT64_C(0x8000000022000000);
constexpr unsigned kBlocksPerRow = (kW + 127u) >> 7;
constexpr unsigned kBlockRows = (kH + 127u) >> 7;
constexpr std::size_t kUsedBytes = static_cast<std::size_t>(kBlocksPerRow) * kBlockRows << 16;

struct VideoBuffer
{
    void *data;
    void *metadata;
    void *reserved0;
    void *reserved1;
};

struct VideoAttribute
{
    std::uint8_t reserved[80];
};

extern "C" void sceVideoOutSetBufferAttribute2(VideoAttribute *attribute,
                                               std::uint64_t pixel_format,
                                               std::uint32_t tiling_mode, std::uint32_t width,
                                               std::uint32_t height, std::uint64_t option,
                                               std::uint32_t dcc_control,
                                               std::uint64_t dcc_clear_color);
extern "C" int sceVideoOutRegisterBuffers2(std::int32_t handle, std::int32_t set_index,
                                           std::int32_t buffer_index_start, VideoBuffer *buffers,
                                           std::int32_t buffer_count, VideoAttribute *attribute,
                                           std::int32_t category, void *option);

// Separable parts of the boilerplate's tiled_byte_offset(x, y).
constexpr std::uint32_t x_bits(std::uint32_t x) noexcept
{
    return ((x << 2) & 0xcU) ^ ((x << 5) & 0x380U) ^ ((x << 4) & 0x400U) ^ ((x << 6) & 0x800U) ^
           ((x << 9) & 0xa000U);
}

constexpr std::uint32_t y_bits(std::uint32_t y) noexcept
{
    return ((y << 4) & 0x70U) ^ ((y << 5) & 0xf00U) ^ ((y << 9) & 0x1000U) ^ ((y << 8) & 0x4000U);
}

constexpr std::size_t tiled_byte_offset(unsigned x, unsigned y) noexcept
{
    const std::uint32_t block = (y >> 7) * kBlocksPerRow + (x >> 7);
    return (static_cast<std::size_t>(block) << 16) + (y_bits(y) ^ x_bits(x));
}

static_assert(tiled_byte_offset(1, 0) == 4 && tiled_byte_offset(3, 0) == 12);
static_assert(tiled_byte_offset(0, 1) == 16 && tiled_byte_offset(0, 3) == 48);
static_assert(tiled_byte_offset(5, 6) - tiled_byte_offset(4, 4) == 2 * 16 + 1 * 4);

void flush_range(void *address, std::size_t length) noexcept
{
    auto *at = static_cast<std::uint8_t *>(address);
    const auto *end = at + length;
    for (; at < end; at += 64)
        __asm__ volatile("clflush (%0)" : : "r"(at) : "memory");
    __asm__ volatile("mfence" ::: "memory");
}
} // namespace

struct Display::Impl
{
    int handle = -1;
    std::array<std::uint8_t *, 2> buffers{};
    int shown = 0;
    std::int64_t flip_count = 0;
    std::uint64_t presented = 0;
    std::uint32_t last_us = 0;
    std::array<std::uint32_t, kW / 4> x_offsets{};
};

Display::Display() : impl_{std::make_unique<Impl>()}
{
}
Display::~Display() = default;

bool Display::open(std::string *error)
{
    const auto fail = [&](const char *why)
    {
        if (error)
            *error = why;
        return false;
    };
    for (unsigned i = 0; i < kW / 4; ++i)
        impl_->x_offsets[i] = x_bits(i * 4);

    (void)sceSystemServiceHideSplashScreen();
    impl_->handle = sceVideoOutOpen(0xff, 0, 0, nullptr);
    if (impl_->handle < 0)
        return fail("sceVideoOutOpen failed");
    const std::size_t pool = sceKernelGetDirectMemorySize();
    if (pool < kMemoryBytes)
        return fail("not enough direct memory for the framebuffers");
    std::int64_t physical = 0;
    if (sceKernelAllocateDirectMemory(0, static_cast<std::int64_t>(pool), kMemoryBytes, kAlignment,
                                      kMemoryTypeWcGarlic, &physical) < 0)
        return fail("framebuffer direct-memory allocation failed");
    void *mapped = nullptr;
    if (sceKernelMapDirectMemory(&mapped, kMemoryBytes, kMapProtection, 0, physical, kAlignment) <
            0 ||
        !mapped)
        return fail("framebuffer mapping failed");
    impl_->buffers[0] = static_cast<std::uint8_t *>(mapped);
    impl_->buffers[1] = impl_->buffers[0] + kFrameBytes;
    std::memset(mapped, 0, kUsedBytes);
    std::memset(impl_->buffers[1], 0, kUsedBytes);
    flush_range(impl_->buffers[0], kUsedBytes);
    flush_range(impl_->buffers[1], kUsedBytes);

    std::array<VideoBuffer, 2> buffers{{
        {impl_->buffers[0], nullptr, nullptr, nullptr},
        {impl_->buffers[1], nullptr, nullptr, nullptr},
    }};
    VideoAttribute attribute{};
    (void)sceVideoOutSetFlipRate(impl_->handle, 0);
    sceVideoOutSetBufferAttribute2(&attribute, kPixelFormatRgba8Srgb, 0, kW, kH, 0, 0, 0);
    if (sceVideoOutRegisterBuffers2(impl_->handle, 0, 0, buffers.data(), 2, &attribute, 0,
                                    nullptr) < 0)
        return fail("framebuffer registration failed");
    if (sceVideoOutSubmitFlip(impl_->handle, 0, 1, ++impl_->flip_count) < 0)
        return fail("initial flip failed");
    (void)sceVideoOutWaitVblank(impl_->handle);
    impl_->shown = 0;
    return true;
}

void Display::present(const gfx::Surface &frame)
{
    if (impl_->handle < 0 || frame.width() < static_cast<int>(kW) ||
        frame.height() < static_cast<int>(kH))
        return;
    const std::uint64_t started = monotonic_us();
    const int back = impl_->shown ^ 1;
    std::uint8_t *fb = impl_->buffers[static_cast<std::size_t>(back)];
    for (unsigned y = 0; y < kH; y += 4)
    {
        const std::uint32_t yb = y_bits(y);
        const std::size_t block_row = static_cast<std::size_t>(y >> 7) * kBlocksPerRow;
        const auto *r0 = frame.row(static_cast<int>(y));
        const auto *r1 = frame.row(static_cast<int>(y + 1));
        const auto *r2 = frame.row(static_cast<int>(y + 2));
        const auto *r3 = frame.row(static_cast<int>(y + 3));
        for (unsigned i = 0; i < kW / 4; ++i)
        {
            const unsigned x = i * 4;
            std::uint8_t *d = fb + ((block_row + (x >> 7)) << 16) + (yb ^ impl_->x_offsets[i]);
            std::memcpy(d, r0 + x, 16);
            std::memcpy(d + 16, r1 + x, 16);
            std::memcpy(d + 32, r2 + x, 16);
            std::memcpy(d + 48, r3 + x, 16);
        }
    }
    flush_range(fb, kUsedBytes);
    if (sceVideoOutSubmitFlip(impl_->handle, back, 1, ++impl_->flip_count) < 0)
    {
        // The previous flip is still queued; give it one vblank and retry.
        (void)sceVideoOutWaitVblank(impl_->handle);
        if (sceVideoOutSubmitFlip(impl_->handle, back, 1, impl_->flip_count) < 0)
            return;
    }
    impl_->shown = back;
    ++impl_->presented;
    impl_->last_us = static_cast<std::uint32_t>(monotonic_us() - started);
}

void Display::wait_vblank()
{
    if (impl_->handle >= 0)
        (void)sceVideoOutWaitVblank(impl_->handle);
    else
        sleep_us(16000);
}

std::uint64_t Display::presented_frames() const noexcept
{
    return impl_->presented;
}

std::uint32_t Display::last_present_us() const noexcept
{
    return impl_->last_us;
}
} // namespace akeno::platform
