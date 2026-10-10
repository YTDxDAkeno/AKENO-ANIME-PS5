// AKENO STREAM PS5 - Minimal local HTTP server for integration tests.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace akeno::test
{
class TestServer final
{
  public:
    explicit TestServer(std::string root);
    ~TestServer();
    TestServer(const TestServer &) = delete;
    TestServer &operator=(const TestServer &) = delete;

    [[nodiscard]] int port() const noexcept
    {
        return port_;
    }
    [[nodiscard]] std::string url(const std::string &path) const;
    // Responds to path with this status (and a small HTML body) instead of the file.
    void fail(const std::string &path, int status);
    // Responds to path with this body.
    void override_body(const std::string &path, std::string body);
    [[nodiscard]] int requests() const noexcept
    {
        return requests_.load();
    }
    // Requests that carried a Range header.
    [[nodiscard]] int range_requests() const noexcept
    {
        return range_requests_.load();
    }
    // Answers Range requests with the whole file (a server without range support).
    void ignore_ranges(bool ignore)
    {
        ignore_ranges_.store(ignore);
    }

  private:
    void serve();
    void handle(int client);

    std::string root_;
    int listener_ = -1;
    int port_ = 0;
    std::atomic<bool> running_{true};
    std::atomic<int> requests_{0};
    std::atomic<int> range_requests_{0};
    std::atomic<bool> ignore_ranges_{false};
    std::atomic<int> active_{0};
    std::mutex lock_;
    std::map<std::string, int> failures_;
    std::map<std::string, std::string> bodies_;
    std::thread thread_;
};
} // namespace akeno::test
