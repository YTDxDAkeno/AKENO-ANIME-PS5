// AKENO STREAM PS5 - HTTP(S) client on libcurl.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TLS peer and host verification are always on (the console's certificate
// list is configured by the platform layer). A redirect may never downgrade
// HTTPS to HTTP. Every request is bounded in time and size and can be
// cancelled from another thread.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace akeno::net
{
using CancelFlag = std::shared_ptr<std::atomic<bool>>;
inline CancelFlag make_cancel_flag()
{
    return std::make_shared<std::atomic<bool>>(false);
}

// What a response announced before its body (redirects already followed).
struct Head
{
    long status = 0;
    std::int64_t content_length = -1; // bytes in this response, -1 when unknown
    std::int64_t total_length = -1;   // whole resource (Content-Range or Content-Length)
    bool partial = false;             // 206: the server honoured the Range header
};

struct Request
{
    std::string url;
    std::string method = "GET";       // GET or POST
    std::vector<std::string> headers; // "Name: value"
    std::string body;
    std::size_t max_bytes = 8u * 1024u * 1024u;
    long connect_timeout_ms = 8000;
    long total_timeout_ms = 30000;
    long low_speed_bytes = 1024; // abort if slower than this ...
    long low_speed_seconds = 15; // ... for this long
    bool follow_redirects = true;
    CancelFlag cancel;
    // Optional streaming sink. Returning false aborts the transfer. When set,
    // the body is not accumulated in Response::body.
    std::function<bool(const std::uint8_t *, std::size_t)> on_data;
    // Byte range to request, e.g. "1000-" or "0-4095" (HTTP Range header).
    std::string range;
    // Called once before the first body byte (or at the end for an empty
    // body). Returning false aborts the transfer.
    std::function<bool(const Head &)> on_head;
};

enum class Outcome : std::uint8_t
{
    ok,            // completed with HTTP 2xx
    http_error,    // completed with a non-2xx status
    network_error, // DNS, connect, TLS, timeout ...
    too_large,
    cancelled,
    invalid_url,
};

struct Response
{
    Outcome outcome = Outcome::network_error;
    long status = 0;
    std::string body;
    std::string content_type;
    std::string final_url;
    int curl_code = 0;
    std::string error; // human-readable, never contains credentials
    std::size_t bytes = 0;
    std::uint32_t elapsed_ms = 0;
    Head head;

    [[nodiscard]] bool ok() const noexcept
    {
        return outcome == Outcome::ok;
    }
    [[nodiscard]] std::string describe() const;
};

class Client final
{
  public:
    Client();
    ~Client();
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

    // Reuses the connection between calls (HTTP keep-alive). One thread at a time.
    Response perform(const Request &request);

    static void global_init(); // call once on the main thread at start-up
    static std::string user_agent();
    static std::string library_version();

  private:
    void *easy_ = nullptr;
};

// Convenience for one-off requests from worker threads.
Response get(const std::string &url, const CancelFlag &cancel = {},
             std::size_t max_bytes = 8u * 1024u * 1024u, std::vector<std::string> headers = {});

const char *outcome_name(Outcome outcome) noexcept;

// Test seam: when installed, every Client::perform() call is answered by this
// function instead of the network. Used only by host tests and the host
// screenshot tool; the console build never installs one.
using Transport = std::function<Response(const Request &)>;
void set_test_transport(Transport transport);
} // namespace akeno::net
