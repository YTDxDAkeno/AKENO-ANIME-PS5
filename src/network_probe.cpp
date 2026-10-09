// Akeno Anime: bounded certificate-verified HTTPS test and free HLS playlist inspection.
// Copyright (C) 2026 Akeno Anime contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "network_probe.hpp"
#include "console_curl.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <pthread.h>
#include <curl/curl.h>

namespace
{
constexpr const char *kHttpsUrl = "https://example.com/";
constexpr const char *kHlsUrl = "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8";
constexpr std::size_t kMaxBody = 128 * 1024;

struct Response
{
    std::array<char, kMaxBody + 1> bytes{};
    std::size_t used = 0;
    bool truncated = false;
};

std::size_t append_body(char *ptr, std::size_t size, std::size_t count, void *user) noexcept
{
    auto &response = *static_cast<Response *>(user);
    if (size && count > static_cast<std::size_t>(-1) / size)
        return 0;
    const std::size_t len = size * count;
    if (len > kMaxBody - response.used)
    {
        response.truncated = true;
        return 0; // Bound memory; curl reports a write error instead of overflowing.
    }
    std::memcpy(response.bytes.data() + response.used, ptr, len);
    response.used += len;
    response.bytes[response.used] = '\0';
    return len;
}

struct GetResult
{
    int http = 0;
    int curl_error = 0;
};

GetResult fetch_https(const char *url, Response &response) noexcept
{
    CURL *easy = curl_easy_init();
    if (!easy)
        return {0, static_cast<int>(CURLE_FAILED_INIT)};

    console_curl_setup(easy); // On-console certificates, non-blocking socket and no signals.
    curl_easy_setopt(easy, CURLOPT_URL, url);
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, 7500L);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, 16000L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(easy, CURLOPT_USERAGENT, "AkenoAnime-PS5/0.2 (homebrew diagnostic)");
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, append_body);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &response);
    const CURLcode result = curl_easy_perform(easy);
    long code = 0;
    (void)curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(easy);
    return {static_cast<int>(code), static_cast<int>(result)};
}

int count_hls_variants(const Response &response) noexcept
{
    if (response.used < 7 || std::memcmp(response.bytes.data(), "#EXTM3U", 7) != 0)
        return -1;
    constexpr const char *needle = "#EXT-X-STREAM-INF:";
    int variants = 0;
    const char *cur = response.bytes.data();
    while (const char *hit = std::strstr(cur, needle))
    {
        ++variants;
        cur = hit + std::strlen(needle);
    }
    return variants; // 0 denotes a media playlist, also a valid HLS manifest.
}
} // namespace

namespace akeno
{
bool NetworkProbe::start() noexcept
{
    ProbeStage expected = stage_.load(std::memory_order_acquire);
    for (;;)
    {
        if (expected == ProbeStage::running)
            return false;
        if (stage_.compare_exchange_weak(expected, ProbeStage::running,
                                         std::memory_order_acq_rel))
            break;
    }

    https_http_code_.store(0, std::memory_order_relaxed);
    hls_http_code_.store(0, std::memory_order_relaxed);
    curl_code_.store(0, std::memory_order_relaxed);
    variants_.store(0, std::memory_order_relaxed);

    pthread_attr_t attributes{};
    pthread_t worker{};
    if (pthread_attr_init(&attributes) != 0)
    {
        stage_.store(ProbeStage::thread_error, std::memory_order_release);
        return false;
    }
    (void)pthread_attr_setstacksize(&attributes, 1024 * 1024);
    const int result = pthread_create(&worker, &attributes, thread_entry, this);
    (void)pthread_attr_destroy(&attributes);
    if (result != 0)
    {
        stage_.store(ProbeStage::thread_error, std::memory_order_release);
        return false;
    }
    (void)pthread_detach(worker);
    return true;
}

void *NetworkProbe::thread_entry(void *argument) noexcept
{
    static_cast<NetworkProbe *>(argument)->perform();
    return nullptr;
}

void NetworkProbe::perform() noexcept
{
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    {
        curl_code_.store(static_cast<int>(CURLE_FAILED_INIT));
        stage_.store(ProbeStage::https_error, std::memory_order_release);
        return;
    }

    // Use a bounded thread-local buffer (the thread has a 1 MiB stack).
    Response response{};
    const GetResult first = fetch_https(kHttpsUrl, response);
    https_http_code_.store(first.http);
    curl_code_.store(first.curl_error);
    if (first.curl_error != 0 || first.http != 200 || response.truncated)
    {
        stage_.store(ProbeStage::https_error, std::memory_order_release);
        curl_global_cleanup();
        return;
    }

    Response playlist{};
    const GetResult second = fetch_https(kHlsUrl, playlist);
    hls_http_code_.store(second.http);
    curl_code_.store(second.curl_error);
    const int variants = second.curl_error == 0 && !playlist.truncated &&
                                 second.http == 200
                             ? count_hls_variants(playlist)
                             : -1;
    variants_.store(variants);
    stage_.store(variants >= 0 ? ProbeStage::ready : ProbeStage::playlist_error,
                 std::memory_order_release);
    curl_global_cleanup();
}

ProbeSnapshot NetworkProbe::snapshot() const noexcept
{
    // Once the stage is terminal (acquire), all preceding worker stores are visible.
    ProbeSnapshot snapshot{};
    snapshot.stage = stage_.load(std::memory_order_acquire);
    snapshot.https_http_code = https_http_code_.load(std::memory_order_relaxed);
    snapshot.hls_http_code = hls_http_code_.load(std::memory_order_relaxed);
    snapshot.curl_code = curl_code_.load(std::memory_order_relaxed);
    snapshot.variants = variants_.load(std::memory_order_relaxed);
    return snapshot;
}
} // namespace akeno
