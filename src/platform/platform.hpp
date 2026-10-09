// AKENO STREAM PS5 - Platform services used by the portable application.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Implemented in src/platform/ps5 for the console and tests/host for the
// build machine, so everything above this layer is testable off-console.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace akeno::platform
{
std::uint64_t monotonic_us() noexcept;
std::uint64_t wall_clock_seconds() noexcept; // Unix time, 0 when unknown
void sleep_us(std::uint32_t microseconds) noexcept;

// Read-only application files (/app0 on the console).
std::string app_dir();
// Writable title data (/download0/akeno on the console). Created on demand.
std::string data_dir();

struct SystemInfo
{
    std::string platform;     // "PS5 native title" / "host"
    std::string firmware;     // e.g. "12.20" or "unknown"
    std::string firmware_raw; // the system's version string
    std::string model;
    std::string data_dir;
    std::string app_dir;
};
SystemInfo system_info();

// System notification (toast); best effort.
void notify(std::string_view message) noexcept;

// Reports a crash (signal, code address, current stage) as a system
// notification and in <data>/crash.txt before the system ends the app. Call
// first in main. No-op on the host.
void install_crash_reporter() noexcept;
// Names what the app is doing for the crash report; the string must stay valid.
void set_stage(const char *stage) noexcept;
const char *stage() noexcept;
// Bytes the allocator has mapped (0 when unknown).
std::size_t heap_bytes() noexcept;

// Applies console-specific options to a new curl easy handle (CA list,
// non-blocking sockets, no signals). No-op on the host.
void configure_curl(void *curl_easy) noexcept;

// Starts a joinable thread with an explicit stack size. Returns false on failure.
struct Thread
{
    std::uintptr_t handle = 0;
    bool started = false;
};
bool start_thread(Thread &thread, void *(*entry)(void *), void *argument, std::size_t stack_bytes,
                  const char *name) noexcept;
void join_thread(Thread &thread) noexcept;
} // namespace akeno::platform
