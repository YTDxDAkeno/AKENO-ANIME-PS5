// AKENO STREAM PS5 - Asynchronous artwork loading and caching.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/image_cache.hpp"

#include "core/fs.hpp"
#include "gfx/image_decode.hpp"
#include "net/http.hpp"

#include <algorithm>
#include <memory>

namespace akeno::ui
{
ImageCache::ImageCache(Jobs &jobs, std::size_t budget_bytes) : jobs_{jobs}, budget_{budget_bytes}
{
}

const gfx::Image *ImageCache::get(const std::string &url, int w, int h)
{
    if (url.empty() || w <= 0 || h <= 0)
        return nullptr;
    const std::string key = url + "@" + std::to_string(w) + "x" + std::to_string(h);
    auto it = entries_.find(key);
    if (it == entries_.end())
    {
        Entry entry;
        entry.url = url;
        entry.w = w;
        entry.h = h;
        entry.last_used = ++clock_;
        entries_.emplace(key, std::move(entry));
        queue_.push_front(key);
        if (queue_.size() > kMaxQueued)
        {
            // Forget the oldest unstarted requests; they will be asked for again if still visible.
            const std::string dropped = queue_.back();
            queue_.pop_back();
            const auto d = entries_.find(dropped);
            if (d != entries_.end() && d->second.state == State::queued)
                entries_.erase(d);
        }
        return nullptr;
    }
    Entry &entry = it->second;
    entry.last_used = ++clock_;
    if (entry.state == State::queued)
    {
        // Re-prioritise: visible again.
        const auto q = std::find(queue_.begin(), queue_.end(), key);
        if (q != queue_.end() && q != queue_.begin())
        {
            queue_.erase(q);
            queue_.push_front(key);
        }
    }
    return entry.state == State::ready ? &entry.image : nullptr;
}

void ImageCache::tick()
{
    while (in_flight_ < kMaxInFlight && !queue_.empty())
    {
        const std::string key = queue_.front();
        queue_.pop_front();
        const auto it = entries_.find(key);
        if (it == entries_.end() || it->second.state != State::queued)
            continue;
        it->second.state = State::loading;
        ++in_flight_;
        const std::string url = it->second.url;
        const int w = it->second.w, h = it->second.h;
        jobs_.run(
            [this, key, url, w, h]
            {
                auto image = std::make_shared<gfx::Image>();
                std::vector<std::uint8_t> bytes;
                if (url.starts_with("/"))
                {
                    if (auto data = fs::read_bytes(url, gfx::kMaxImageBytes))
                        bytes = std::move(*data);
                }
                else
                {
                    const net::Response r = net::get(url, {}, gfx::kMaxImageBytes);
                    if (r.ok())
                        bytes.assign(r.body.begin(), r.body.end());
                }
                if (!bytes.empty())
                {
                    const gfx::Image decoded = gfx::decode_image(bytes.data(), bytes.size());
                    if (decoded.valid())
                        *image = gfx::cover_image(decoded, w, h);
                }
                jobs_.post(
                    [this, key, image]
                    {
                        --in_flight_;
                        const auto it = entries_.find(key);
                        if (it == entries_.end())
                            return;
                        if (image->valid())
                        {
                            it->second.image = std::move(*image);
                            it->second.state = State::ready;
                            bytes_ += it->second.image.pixels.size() * sizeof(gfx::Pixel);
                            evict();
                        }
                        else
                        {
                            it->second.state = State::failed;
                        }
                        changed_ = true;
                    });
            });
    }
}

void ImageCache::evict()
{
    while (bytes_ > budget_)
    {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it)
            if (it->second.state == State::ready &&
                (oldest == entries_.end() || it->second.last_used < oldest->second.last_used))
                oldest = it;
        if (oldest == entries_.end())
            return;
        bytes_ -= oldest->second.image.pixels.size() * sizeof(gfx::Pixel);
        entries_.erase(oldest);
    }
}

bool ImageCache::take_changed()
{
    const bool changed = changed_;
    changed_ = false;
    return changed;
}

ImageCache::Stats ImageCache::stats() const
{
    Stats s;
    s.bytes = bytes_;
    for (const auto &[key, entry] : entries_)
    {
        (void)key;
        if (entry.state == State::ready)
            ++s.images;
        else if (entry.state == State::loading)
            ++s.loading;
        else if (entry.state == State::failed)
            ++s.failed;
    }
    return s;
}
} // namespace akeno::ui
