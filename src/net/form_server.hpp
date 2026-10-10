// AKENO STREAM PS5 - A one-page web form for adding sources from a phone.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// While the "Add from phone" screen is open, the app listens on the local
// network and serves one small form at http://<console>:<port>/<pin>. The pin
// is random for every opening and is only shown on the TV (as a QR code), so
// other devices on the network cannot add anything; the server stops when
// the screen closes, after too many wrong requests, or after 20 additions.
// Requests are size- and time-limited and nothing the phone sends is executed:
// a submission is a name and an http(s) address, validated before use.
#pragma once

#include "platform/platform.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace akeno::net
{
class FormServer final
{
  public:
    struct Submission
    {
        std::string name;
        std::string url;
    };

    FormServer() = default;
    ~FormServer();
    FormServer(const FormServer &) = delete;
    FormServer &operator=(const FormServer &) = delete;

    // Listens on port (0: any free port). False + reason on failure.
    bool start(std::uint16_t port, std::string *error);
    void stop();
    [[nodiscard]] bool running() const noexcept
    {
        return running_.load();
    }
    [[nodiscard]] std::uint16_t port() const noexcept
    {
        return port_;
    }
    [[nodiscard]] const std::string &pin() const noexcept
    {
        return pin_;
    }
    // Submissions received since the last call.
    std::vector<Submission> take();
    [[nodiscard]] int accepted() const noexcept
    {
        return accepted_.load();
    }

    static constexpr int kMaxSubmissions = 20;
    static constexpr int kMaxBadRequests = 30;

  private:
    static void *thread_entry(void *self);
    void serve();
    void handle(int client);

    int listener_ = -1;
    std::uint16_t port_ = 0;
    std::string pin_;
    std::atomic<bool> running_{false};
    std::atomic<int> accepted_{0};
    int bad_requests_ = 0;
    platform::Thread thread_;
    std::mutex lock_;
    std::vector<Submission> pending_;
};

// This device's IPv4 address on the local network ("" when unknown).
std::string local_ipv4();
// Decodes application/x-www-form-urlencoded fields (exposed for tests).
std::vector<std::pair<std::string, std::string>> parse_form(const std::string &body);
} // namespace akeno::net
