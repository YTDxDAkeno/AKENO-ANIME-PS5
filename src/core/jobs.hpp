// AKENO STREAM PS5 - Background work with results delivered to the UI thread.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Network requests, JSON parsing and image decoding run on a small pool of
// worker threads. Their completions are queued and executed by the UI thread
// in drain(), so screens only ever touch their state from one thread.
#pragma once

#include "platform/platform.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

namespace akeno
{
class Jobs final
{
  public:
    using Task = std::function<void()>;

    explicit Jobs(int workers = 3, std::size_t stack_bytes = 1024 * 1024);
    ~Jobs();
    Jobs(const Jobs &) = delete;
    Jobs &operator=(const Jobs &) = delete;

    // Runs work on a worker thread.
    void run(Task work);
    // Queues a completion for the UI thread (callable from any thread).
    void post(Task completion);
    // UI thread: executes queued completions. Returns how many ran.
    std::size_t drain();
    [[nodiscard]] std::size_t pending() const;
    // Stops workers after the queued work finishes (destructor does this too).
    void shutdown();

  private:
    static void *entry(void *self);
    void loop();

    mutable std::mutex lock_;
    std::condition_variable wake_;
    std::deque<Task> work_;
    std::vector<Task> done_;
    std::vector<platform::Thread> threads_;
    bool stopping_ = false;
    std::size_t busy_ = 0;
};
} // namespace akeno
