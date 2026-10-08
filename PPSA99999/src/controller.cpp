// Akeno Anime PS5 controller adapter.
// Based on ps5-homebrew-ui's documented pad ABI; GPL-3.0-or-later.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller.hpp"
#include <cstddef>
#include <cstdint>

namespace {
struct RawPadSample {
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
constexpr std::uint32_t UP = 0x10, DOWN = 0x40, CIRCLE = 0x2000;
constexpr std::uint32_t CROSS = 0x4000, L1 = 0x400, R1 = 0x800;
} // namespace

extern "C" {
int sceUserServiceInitialize(const void *);
int sceUserServiceGetInitialUser(int *);
int scePadInit(void);
int scePadOpen(int, int, int, const void *);
int scePadRead(int, RawPadSample *, int);
int sceKernelUsleep(std::uint32_t);
}

namespace akeno {
bool Controller::open() noexcept {
    (void)sceUserServiceInitialize(nullptr);
    int user = -1;
    if (sceUserServiceGetInitialUser(&user) != 0) return false;
    (void)scePadInit();
    for (int i = 0; i < 20; ++i) {
        handle_ = scePadOpen(user, 0, 0, nullptr);
        if (handle_ >= 0) return true;
        (void)sceKernelUsleep(50000);
    }
    handle_ = -1;
    return false;
}

void Controller::poll(Model &model) noexcept {
    if (handle_ < 0) { model.set_controller(false); return; }
    RawPadSample raw[64]{};
    const int count = scePadRead(handle_, raw, 64);
    if (count <= 0) return;
    for (int i = 0; i < count; ++i) {
        if (raw[i].connected == 0) {
            old_buttons_ = 0;
            model.set_controller(false);
            continue;
        }
        model.set_controller(true);
        const std::uint32_t now = raw[i].buttons;
        const std::uint32_t pressed = now & ~old_buttons_;
        old_buttons_ = now;
        if (pressed & L1) model.navigate(Nav::tab_left);
        if (pressed & R1) model.navigate(Nav::tab_right);
        if (pressed & UP) model.navigate(Nav::up);
        if (pressed & DOWN) model.navigate(Nav::down);
        if (pressed & CROSS) model.navigate(Nav::confirm);
        if (pressed & CIRCLE) model.navigate(Nav::back);
    }
}
} // namespace akeno
