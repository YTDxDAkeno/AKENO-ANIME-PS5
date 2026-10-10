// AKENO STREAM PS5 - Host implementations of the platform services (tests).
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/platform.hpp"

#include <curl/curl.h>

#include <atomic>
#include <cstdlib>
#include <ctime>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

namespace akeno::platform
{
std::uint64_t monotonic_us() noexcept
{
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::uint64_t>(now.tv_sec) * 1000000u +
           static_cast<std::uint64_t>(now.tv_nsec) / 1000u;
}

std::uint64_t wall_clock_seconds() noexcept
{
    return static_cast<std::uint64_t>(std::time(nullptr));
}

void sleep_us(std::uint32_t microseconds) noexcept
{
    usleep(microseconds);
}

std::string app_dir()
{
    const char *root = std::getenv("AKENO_SOURCE_ROOT");
    return root ? root : ".";
}

std::string data_dir()
{
    const char *dir = std::getenv("AKENO_DATA_DIR");
    std::string path = dir ? dir : "/tmp/akeno-test-data";
    mkdir(path.c_str(), 0700);
    return path;
}

std::string data_dir_from_pc()
{
    return data_dir();
}

SystemInfo system_info()
{
    SystemInfo info;
    info.platform = "host test build";
    info.firmware = "n/a";
    info.model = "build machine";
    info.data_dir = data_dir();
    info.app_dir = app_dir();
    return info;
}

void notify(std::string_view) noexcept
{
}

bool launch_app(const std::vector<std::string> &, std::string *error)
{
    if (error)
        *error = "starting other apps works only on the console";
    return false;
}

bool open_web_browser(const std::string &, std::string *error)
{
    if (error)
        *error = "the system web browser is available only on the console";
    return false;
}

namespace
{
std::atomic<const char *> g_stage{"startup"};
}

void install_crash_reporter() noexcept
{
}

void set_stage(const char *stage) noexcept
{
    g_stage.store(stage);
}

const char *stage() noexcept
{
    return g_stage.load();
}

std::size_t heap_bytes() noexcept
{
    return 0;
}

void configure_curl(void *curl_easy) noexcept
{
    // The sandboxed CI proxies may provide their own CA bundle.
    if (const char *bundle = std::getenv("SSL_CERT_FILE"))
        curl_easy_setopt(static_cast<CURL *>(curl_easy), CURLOPT_CAINFO, bundle);
}

bool start_thread(Thread &thread, void *(*entry)(void *), void *argument, std::size_t stack_bytes,
                  const char *) noexcept
{
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, stack_bytes < 262144 ? 262144 : stack_bytes);
    pthread_t handle{};
    const int result = pthread_create(&handle, &attributes, entry, argument);
    pthread_attr_destroy(&attributes);
    if (result != 0)
        return false;
    thread.handle = static_cast<std::uintptr_t>(handle);
    thread.started = true;
    return true;
}

void join_thread(Thread &thread) noexcept
{
    if (!thread.started)
        return;
    pthread_join(static_cast<pthread_t>(thread.handle), nullptr);
    thread.started = false;
}
} // namespace akeno::platform
