// AKENO STREAM PS5 - Byte streams that FFmpeg reads through custom AVIO.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/byte_source.hpp"

#include "net/http.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace akeno::media
{
namespace
{
class FileSource final : public ByteSource
{
  public:
    FileSource(int fd, std::int64_t size) : fd_{fd}, size_{size}
    {
    }
    ~FileSource() override
    {
        ::close(fd_);
    }

    int read(std::uint8_t *buffer, int size) override
    {
        const ssize_t got = ::read(fd_, buffer, static_cast<std::size_t>(size));
        if (got < 0)
        {
            error_ = std::strerror(errno);
            return -1;
        }
        position_ += got;
        return static_cast<int>(got);
    }
    [[nodiscard]] bool seekable() const override
    {
        return true;
    }
    bool seek(std::int64_t offset) override
    {
        if (::lseek(fd_, static_cast<off_t>(offset), SEEK_SET) < 0)
            return false;
        position_ = offset;
        return true;
    }
    [[nodiscard]] std::int64_t size() const override
    {
        return size_;
    }
    [[nodiscard]] std::int64_t position() const override
    {
        return position_;
    }

  private:
    int fd_;
    std::int64_t size_;
    std::int64_t position_ = 0;
};

constexpr std::size_t kRingBytes = 4u << 20;
constexpr std::int64_t kReadThroughBytes = 2 << 20; // forward jumps read instead of reconnecting
constexpr std::size_t kThreadStack = 512u * 1024u;

class HttpSource final : public ByteSource
{
  public:
    HttpSource(std::string url, const std::atomic<bool> &stop, TransferObserver observer)
        : url_{std::move(url)}, stop_{stop}, observer_{std::move(observer)}, ring_(kRingBytes)
    {
    }
    ~HttpSource() override
    {
        stop_download();
    }

    bool start(std::int64_t offset, std::string *error)
    {
        launch(offset);
        std::unique_lock<std::mutex> lock(lock_);
        while (!head_known_ && !finished_ && !stop_.load())
            changed_.wait_for(lock, std::chrono::milliseconds(50));
        if (failed_ || (finished_ && !head_known_))
        {
            if (error)
                *error = failure_.empty() ? "the server did not answer" : failure_;
            return false;
        }
        return !stop_.load();
    }

    int read(std::uint8_t *buffer, int size) override
    {
        std::unique_lock<std::mutex> lock(lock_);
        while (count_ == 0 && !finished_ && !stop_.load())
            changed_.wait_for(lock, std::chrono::milliseconds(50));
        if (stop_.load())
        {
            error_ = "stopped";
            return -1;
        }
        if (count_ == 0)
        {
            if (failed_)
            {
                error_ = failure_;
                return -1;
            }
            return 0;
        }
        const std::size_t n = std::min(count_, static_cast<std::size_t>(size));
        for (std::size_t copied = 0; copied < n;)
        {
            const std::size_t run = std::min(n - copied, ring_.size() - head_);
            std::memcpy(buffer + copied, ring_.data() + head_, run);
            head_ = (head_ + run) % ring_.size();
            copied += run;
        }
        count_ -= n;
        position_ += static_cast<std::int64_t>(n);
        changed_.notify_all();
        return static_cast<int>(n);
    }

    // With Range support a seek reconnects at the offset; without it the
    // download restarts and skips to the offset (slow, but correct).
    [[nodiscard]] bool seekable() const override
    {
        std::lock_guard<std::mutex> guard(lock_);
        return size_ > 0;
    }

    bool seek(std::int64_t offset) override
    {
        std::int64_t gap = 0;
        {
            std::unique_lock<std::mutex> lock(lock_);
            if (offset == position_)
                return true;
            gap = offset - position_;
            if (gap > 0 && gap <= static_cast<std::int64_t>(count_))
            {
                head_ = (head_ + static_cast<std::size_t>(gap)) % ring_.size();
                count_ -= static_cast<std::size_t>(gap);
                position_ = offset;
                changed_.notify_all();
                return true;
            }
        }
        if (gap > 0 && gap <= kReadThroughBytes)
        {
            // Read through a short forward jump on the open connection.
            std::uint8_t scratch[16384];
            while (position_ < offset)
            {
                const int got = read(scratch, static_cast<int>(std::min<std::int64_t>(
                                                  sizeof(scratch), offset - position_)));
                if (got <= 0)
                    return false;
            }
            return true;
        }
        if (!seekable() || offset < 0)
            return false;
        stop_download();
        if (offset >= size())
        {
            // At or past the end: nothing left to download.
            std::lock_guard<std::mutex> guard(lock_);
            head_ = count_ = 0;
            finished_ = true;
            failed_ = false;
            position_ = offset;
            return true;
        }
        return start(offset, nullptr);
    }

    [[nodiscard]] std::int64_t size() const override
    {
        std::lock_guard<std::mutex> guard(lock_);
        return size_;
    }
    [[nodiscard]] std::int64_t position() const override
    {
        return position_;
    }

  private:
    static void *thread_entry(void *self)
    {
        static_cast<HttpSource *>(self)->download();
        return nullptr;
    }

    void launch(std::int64_t offset)
    {
        {
            std::lock_guard<std::mutex> guard(lock_);
            head_ = count_ = 0;
            head_known_ = finished_ = failed_ = false;
            failure_.clear();
            start_ = position_ = offset;
            skip_ = 0;
            cancel_ = net::make_cancel_flag();
        }
        if (!platform::start_thread(thread_, thread_entry, this, kThreadStack, "akeno-http"))
        {
            std::lock_guard<std::mutex> guard(lock_);
            finished_ = failed_ = true;
            failure_ = "could not start the download thread";
        }
    }

    void stop_download()
    {
        if (cancel_)
            cancel_->store(true);
        changed_.notify_all();
        platform::join_thread(thread_);
    }

    bool cancelled() const
    {
        return stop_.load() || (cancel_ && cancel_->load());
    }

    void download()
    {
        net::Client client;
        net::Request request;
        request.url = url_;
        request.range = std::to_string(start_) + "-";
        request.max_bytes = ~std::size_t{0} / 2;
        request.total_timeout_ms = 0;
        request.cancel = cancel_;
        request.on_head = [this](const net::Head &head)
        {
            std::lock_guard<std::mutex> guard(lock_);
            if (head.status < 200 || head.status > 299)
            {
                failure_ = "HTTP " + std::to_string(head.status);
                failed_ = true;
                changed_.notify_all();
                return false;
            }
            ranges_ = head.partial;
            status_ = head.status;
            if (head.total_length > 0)
                size_ = head.total_length;
            if (!head.partial && start_ > 0)
                skip_ = start_; // the server ignored the range: discard up to the offset
            head_known_ = true;
            changed_.notify_all();
            return true;
        };
        request.on_data = [this](const std::uint8_t *data, std::size_t bytes)
        {
            std::unique_lock<std::mutex> lock(lock_);
            if (observer_)
                observer_(status_, bytes);
            if (skip_ > 0)
            {
                const auto dropped =
                    std::min<std::int64_t>(skip_, static_cast<std::int64_t>(bytes));
                skip_ -= dropped;
                data += dropped;
                bytes -= static_cast<std::size_t>(dropped);
            }
            while (bytes)
            {
                while (count_ == ring_.size() && !cancelled())
                    changed_.wait_for(lock, std::chrono::milliseconds(50));
                if (cancelled())
                    return false;
                const std::size_t tail = (head_ + count_) % ring_.size();
                const std::size_t run =
                    std::min({bytes, ring_.size() - count_, ring_.size() - tail});
                std::memcpy(ring_.data() + tail, data, run);
                count_ += run;
                data += run;
                bytes -= run;
                changed_.notify_all();
            }
            return true;
        };
        const net::Response response = client.perform(request);
        std::lock_guard<std::mutex> guard(lock_);
        finished_ = true;
        if (!response.ok() && !cancelled() && !failed_)
        {
            failed_ = true;
            failure_ = response.describe();
        }
        changed_.notify_all();
    }

    std::string url_;
    const std::atomic<bool> &stop_;
    TransferObserver observer_;
    platform::Thread thread_;
    mutable std::mutex lock_;
    std::condition_variable changed_;
    std::vector<std::uint8_t> ring_;
    long status_ = 0;
    std::size_t head_ = 0, count_ = 0;
    bool head_known_ = false, finished_ = false, failed_ = false;
    std::string failure_;
    bool ranges_ = false;
    std::int64_t size_ = -1;
    std::int64_t start_ = 0, position_ = 0, skip_ = 0;
    net::CancelFlag cancel_;
};
} // namespace

std::unique_ptr<ByteSource> open_file_source(const std::string &path, std::string *error)
{
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0)
    {
        if (error)
            *error = std::string{"cannot open file: "} + std::strerror(errno);
        return nullptr;
    }
    struct stat facts
    {
    };
    const std::int64_t size = ::fstat(fd, &facts) == 0 ? facts.st_size : -1;
    return std::make_unique<FileSource>(fd, size);
}

std::unique_ptr<ByteSource> open_http_source(const std::string &url, const std::atomic<bool> &stop,
                                             TransferObserver observer, std::string *error)
{
    auto source = std::make_unique<HttpSource>(url, stop, std::move(observer));
    if (!source->start(0, error))
        return nullptr;
    return source;
}
} // namespace akeno::media
