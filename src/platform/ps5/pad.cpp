// AKENO STREAM PS5 - DualSense access through scePad.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sample layout from the open-source ps5-homebrew-ui pad ABI (GPL-3.0-or-later),
// validated on firmware 12.20 by AKENO v0.1.
#include "platform/pad.hpp"

#include <cstddef>
#include <cstdint>

namespace
{
struct RawPadSample
{
    std::uint32_t buttons;
    std::uint8_t left_x, left_y, right_x, right_y, l2, r2;
    std::uint8_t reserved0[66];
    std::int32_t connected;
    std::uint64_t timestamp_us;
    std::uint8_t extension[16];
    std::uint8_t connected_count;
    std::uint8_t reserved1[15];
};
static_assert(sizeof(RawPadSample) == 120);
static_assert(offsetof(RawPadSample, connected) == 0x4c);
static_assert(offsetof(RawPadSample, timestamp_us) == 0x50);
} // namespace

extern "C"
{
    int sceUserServiceInitialize(const void *);
    int sceUserServiceGetInitialUser(int *);
    int scePadInit(void);
    int scePadOpen(int, int, int, const void *);
    int scePadRead(int, RawPadSample *, int);
}

namespace akeno::platform
{
bool Pad::open() noexcept
{
    static bool initialised = false;
    if (!initialised)
    {
        (void)sceUserServiceInitialize(nullptr);
        (void)scePadInit();
        initialised = true;
    }
    if (user_ < 0 && sceUserServiceGetInitialUser(&user_) != 0)
    {
        user_ = -1;
        return false;
    }
    const int handle = scePadOpen(user_, 0, 0, nullptr);
    if (handle < 0)
        return false;
    handle_ = handle;
    return true;
}

void Pad::poll(std::uint64_t now_ms, std::vector<input::Event> &events)
{
    if (handle_ < 0)
    {
        // Retry about once a second; the user may connect a controller later.
        if (now_ms >= next_reopen_ms_)
        {
            next_reopen_ms_ = now_ms + 1000;
            (void)open();
        }
        mapper_.feed({0, 128, 128, false}, now_ms, events);
        return;
    }
    static RawPadSample samples[64];
    const int count = scePadRead(handle_, samples, 64);
    if (count <= 0)
    {
        mapper_.tick(now_ms, events);
        return;
    }
    for (int i = 0; i < count; ++i)
    {
        input::Sample sample;
        sample.buttons = samples[i].buttons;
        sample.left_x = samples[i].left_x;
        sample.left_y = samples[i].left_y;
        sample.connected = samples[i].connected != 0;
        mapper_.feed(sample, now_ms, events);
    }
}
} // namespace akeno::platform
