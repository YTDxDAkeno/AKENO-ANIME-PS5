// AKENO STREAM PS5 - Console implementations of the platform services.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/platform.hpp"

#include "platform/ps5/console_curl.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <sys/stat.h>

extern "C"
{
    int sceKernelUsleep(std::uint32_t microseconds);
    int sceKernelSendNotificationRequest(std::uint32_t device, void *request, std::size_t size,
                                         int blocking);
    int sceKernelGetSystemSwVersion(void *version);
}

namespace akeno::platform
{
namespace
{
struct NotificationRequest
{
    std::uint8_t reserved[45];
    char message[3075];
};

// Official libkernel layout: the call fails (it does not write) when size
// does not match, so a different firmware layout only yields "unknown".
struct SwVersion
{
    std::size_t size;
    char text[0x1c];
    std::uint32_t number;
};
static_assert(sizeof(SwVersion) == 0x28);

std::string format_firmware(std::uint32_t number, const char *text)
{
    // number is BCD-like 0xMMmmppbb (e.g. 0x12200001 for 12.20).
    if (number != 0)
    {
        char out[16];
        std::snprintf(out, sizeof(out), "%x.%02x", (number >> 24) & 0xffu, (number >> 16) & 0xffu);
        return out;
    }
    return text && text[0] ? text : "unknown";
}
} // namespace

std::uint64_t monotonic_us() noexcept
{
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return static_cast<std::uint64_t>(now.tv_sec) * 1000000u +
           static_cast<std::uint64_t>(now.tv_nsec) / 1000u;
}

std::uint64_t wall_clock_seconds() noexcept
{
    const time_t now = std::time(nullptr);
    return now > 0 ? static_cast<std::uint64_t>(now) : 0;
}

void sleep_us(std::uint32_t microseconds) noexcept
{
    (void)sceKernelUsleep(microseconds);
}

std::string app_dir()
{
    return "/app0";
}

std::string data_dir()
{
    static const std::string path = []
    {
        const std::string dir = "/download0/akeno";
        (void)mkdir(dir.c_str(), 0777);
        return dir;
    }();
    return path;
}

std::string data_dir_from_pc()
{
    // The title's download data is mounted into its sandbox only while it runs.
    return "/mnt/sandbox/PPSA99276_000/download0/akeno";
}

SystemInfo system_info()
{
    SystemInfo info;
    info.platform = "PS5 native title";
    SwVersion version{};
    version.size = sizeof(version);
    if (sceKernelGetSystemSwVersion(&version) == 0)
    {
        version.text[sizeof(version.text) - 1] = '\0';
        info.firmware_raw = version.text;
        info.firmware = format_firmware(version.number, version.text);
    }
    else
    {
        info.firmware = "unknown";
    }
    info.model = "PlayStation 5";
    info.data_dir = data_dir();
    info.app_dir = app_dir();
    return info;
}

void notify(std::string_view message) noexcept
{
    static NotificationRequest request{};
    const std::size_t count =
        message.size() < sizeof(request.message) - 1 ? message.size() : sizeof(request.message) - 1;
    std::memcpy(request.message, message.data(), count);
    request.message[count] = '\0';
    (void)sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
}

void configure_curl(void *curl_easy) noexcept
{
    if (curl_easy)
        console_curl_setup(static_cast<CURL *>(curl_easy));
}

bool start_thread(Thread &thread, void *(*entry)(void *), void *argument, std::size_t stack_bytes,
                  const char *name) noexcept
{
    (void)name;
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0)
        return false;
    (void)pthread_attr_setstacksize(&attributes, stack_bytes);
    pthread_t handle{};
    const int result = pthread_create(&handle, &attributes, entry, argument);
    (void)pthread_attr_destroy(&attributes);
    if (result != 0)
        return false;
    thread.handle = reinterpret_cast<std::uintptr_t>(handle);
    thread.started = true;
    return true;
}

void join_thread(Thread &thread) noexcept
{
    if (!thread.started)
        return;
    (void)pthread_join(reinterpret_cast<pthread_t>(thread.handle), nullptr);
    thread.started = false;
}
} // namespace akeno::platform
