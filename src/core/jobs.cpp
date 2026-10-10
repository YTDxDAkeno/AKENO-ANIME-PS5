// AKENO STREAM PS5 - Background work with results delivered to the UI thread.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/jobs.hpp"

namespace akeno
{
Jobs::Jobs(int workers, std::size_t stack_bytes)
{
    threads_.resize(static_cast<std::size_t>(workers > 0 ? workers : 1));
    for (auto &thread : threads_)
        (void)platform::start_thread(thread, entry, this, stack_bytes, "akeno-jobs");
}

Jobs::~Jobs()
{
    shutdown();
}

void Jobs::shutdown()
{
    {
        std::lock_guard<std::mutex> guard(lock_);
        if (stopping_)
            return;
        stopping_ = true;
    }
    wake_.notify_all();
    for (auto &thread : threads_)
        platform::join_thread(thread);
}

void Jobs::run(Task work)
{
    {
        std::lock_guard<std::mutex> guard(lock_);
        if (stopping_)
            return;
        work_.push_back(std::move(work));
    }
    wake_.notify_one();
}

void Jobs::post(Task completion)
{
    std::lock_guard<std::mutex> guard(lock_);
    done_.push_back(std::move(completion));
}

std::size_t Jobs::drain()
{
    std::vector<Task> ready;
    {
        std::lock_guard<std::mutex> guard(lock_);
        ready.swap(done_);
    }
    for (Task &task : ready)
        task();
    return ready.size();
}

std::size_t Jobs::pending() const
{
    std::lock_guard<std::mutex> guard(lock_);
    return work_.size() + busy_ + done_.size();
}

void *Jobs::entry(void *self)
{
    static_cast<Jobs *>(self)->loop();
    return nullptr;
}

void Jobs::loop()
{
    for (;;)
    {
        Task task;
        {
            std::unique_lock<std::mutex> guard(lock_);
            wake_.wait(guard, [&] { return stopping_ || !work_.empty(); });
            if (work_.empty())
                return; // stopping and nothing left
            task = std::move(work_.front());
            work_.pop_front();
            ++busy_;
        }
        task();
        std::lock_guard<std::mutex> guard(lock_);
        --busy_;
    }
}
} // namespace akeno
