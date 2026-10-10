// AKENO STREAM PS5 - AKENO STREAM's own pages for the embedded browser.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// While the browser shows one of AKENO STREAM's own pages (the official
// YouTube player, the browser capability test), the app serves them from
// http://127.0.0.1:<port>/s/<token>/... The page gets a real web origin - which
// YouTube's embedded player requires, since it identifies the embedding site
// by the HTTP Referer - and can tell the app what happened (player ready,
// playing, error 150, codec and DRM results) and ask it to close the browser.
//
// What a page can do is limited to exactly that. The server:
//  - listens on the loopback interface only, never on the network;
//  - runs only while one of these pages is open, with a fresh random token
//    in every path, so other websites in the browser cannot reach it;
//  - checks the Host header (no DNS-rebinding), size-limits every request,
//    and accepts only the events it knows, as plain values it never executes;
//  - gives no access to files (one bundled test clip excepted), settings,
//    the network or any console function.
#pragma once

#include "platform/platform.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace akeno::web
{
struct PageEvent
{
    std::string type;   // "ready", "state", "error", ...
    std::string value;  // short value: "1", "150"
    std::string detail; // one line of text
};

class LocalPages final
{
  public:
    static constexpr std::uint16_t kPreferredPort = 8095; // stable: keeps the pages' origin
    static constexpr std::size_t kMaxEvents = 256;
    static constexpr int kMaxRejected = 400;

    LocalPages() = default;
    ~LocalPages();
    LocalPages(const LocalPages &) = delete;
    LocalPages &operator=(const LocalPages &) = delete;

    // Starts serving with a new token. media_file is the bundled MP4 the
    // capability test plays (may be empty).
    bool start(const std::string &media_file, std::string *error);
    void stop();
    [[nodiscard]] bool running() const noexcept
    {
        return running_.load();
    }
    [[nodiscard]] std::uint16_t port() const noexcept
    {
        return port_;
    }
    // http://127.0.0.1:<port>/s/<token>/<page>[?query]
    [[nodiscard]] std::string page_url(std::string_view page, std::string_view query = {}) const;

    // UI thread: what the pages sent since the last call.
    std::vector<PageEvent> take_events();
    std::optional<std::string> take_report();
    bool take_close_request();
    // monotonic ms of the last valid request (0: none yet) and whether a page
    // script was fetched (the page loaded).
    [[nodiscard]] std::uint64_t last_contact_ms() const noexcept
    {
        return last_contact_.load();
    }
    [[nodiscard]] bool page_loaded() const noexcept
    {
        return page_loaded_.load();
    }
    [[nodiscard]] int rejected() const noexcept
    {
        return rejected_.load();
    }

    // Builds the response to one complete request (exposed for tests).
    struct Reply
    {
        int status = 404;
        std::string content_type = "text/plain; charset=utf-8";
        std::string body;
        std::vector<std::string> headers;
    };
    Reply respond_to(std::string_view request);

  private:
    static void *thread_entry(void *self);
    void serve();

    int listener_ = -1;
    std::uint16_t port_ = 0;
    std::string token_;
    std::string media_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> last_contact_{0};
    std::atomic<bool> page_loaded_{false};
    std::atomic<int> rejected_{0};
    platform::Thread thread_;
    std::mutex lock_;
    std::vector<PageEvent> events_;
    std::optional<std::string> report_;
    bool close_requested_ = false;
};

// The pages themselves (src/web/pages.cpp).
std::string_view youtube_page_html();
std::string_view youtube_page_js();
std::string_view capability_page_html();
std::string_view capability_page_js();
std::string_view pages_css();
} // namespace akeno::web
