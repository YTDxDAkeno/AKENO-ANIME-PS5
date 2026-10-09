// Akeno Anime HTTPS and HLS manifest diagnostic for PS5.
// Copyright (C) 2026 Akeno Anime contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>

namespace akeno
{
enum class ProbeStage : std::uint8_t
{
    idle,
    running,
    https_error,
    playlist_error,
    ready,
    thread_error
};

struct ProbeSnapshot
{
    ProbeStage stage;
    int https_http_code;
    int hls_http_code;
    int curl_code;
    int variants;
};

class NetworkProbe final
{
  public:
    NetworkProbe() = default;
    NetworkProbe(const NetworkProbe &) = delete;
    NetworkProbe &operator=(const NetworkProbe &) = delete;

    // Safe to call on the UI thread. Does not block. A new run is allowed after completion.
    bool start() noexcept;
    ProbeSnapshot snapshot() const noexcept;

  private:
    static void *thread_entry(void *arg) noexcept;
    void perform() noexcept;

    // Published by the worker before stage_ is stored (release).
    std::atomic<int> https_http_code_{0};
    std::atomic<int> hls_http_code_{0};
    std::atomic<int> curl_code_{0};
    std::atomic<int> variants_{0};
    std::atomic<ProbeStage> stage_{ProbeStage::idle};
};
} // namespace akeno
