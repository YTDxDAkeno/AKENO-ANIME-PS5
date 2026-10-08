// Akeno Anime PS5 DualSense adapter. ABI fields based on open-source ps5-homebrew-ui.
// Original project: Copyright (C) 2026 BlackBearReloaded, GPL-3.0-or-later.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app_model.hpp"
#include <cstdint>

namespace akeno {
class Controller final {
public:
    bool open() noexcept;
    void poll(Model &model) noexcept;
private:
    int handle_ = -1;
    std::uint32_t old_buttons_ = 0;
};
} // namespace akeno
