/*
 * ps5-native-app-boilerplate - Page-backed allocator for the executable.
 * Copyright (C) 2026 BlackBearReloaded
 * Copyright (C) 2026 AKENO STREAM contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The clean-room libc.prx supplies the loader-facing heap contract, not a
 * general-purpose heap: on hardware the system malloc behind it returns null
 * once an application asks for real amounts of memory (ProsperoTV recorded
 * this on firmware 6.02; AKENO STREAM 0.4.0 crashed at launch). The executable
 * therefore owns its whole allocation family, backed by anonymous pages.
 *
 * Every block starts 32 bytes after a 32-byte header, so plain allocations are
 * 32-byte aligned: the PS5 target's default new alignment, which the compiler
 * relies on for 256-bit stores. Blocks up to 1 MiB come from 32 MiB chunks in
 * 56 size classes with free lists; larger blocks get their own mapping.
 * Pointers the allocator did not hand out (for example from a system function)
 * are recognised by address and left alone instead of being dereferenced.
 *
 * Header-only and free of static constructors so it works before any
 * initializer runs, and so the host tests can exercise the same code.
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ps5::alloc
{
struct Backend
{
    void *(*map)(std::size_t bytes) noexcept; // zero-filled pages or nullptr
    void (*unmap)(void *address, std::size_t bytes) noexcept;
};

class PageAllocator
{
  public:
    static constexpr std::size_t kAlignment = 32;
    static constexpr std::size_t kHeader = 32;
    static constexpr std::size_t kPage = 0x4000;
    static constexpr std::size_t kChunkBytes = std::size_t{32} << 20;
    static constexpr std::size_t kLargest = std::size_t{1} << 20; // largest size-class capacity
    static constexpr std::size_t kClasses = 56;
    static constexpr std::size_t kMaxChunks = 512; // 16 GiB of small blocks
    static constexpr std::size_t kMaxLarge = 4096; // live blocks above 1 MiB

    explicit constexpr PageAllocator(Backend backend) noexcept : backend_{backend}
    {
    }
    PageAllocator(const PageAllocator &) = delete;
    PageAllocator &operator=(const PageAllocator &) = delete;

    // alignment: a power of two; anything up to 32 gives the default.
    void *allocate(std::size_t size, std::size_t alignment = kAlignment) noexcept
    {
        if (alignment < kAlignment)
            alignment = kAlignment;
        if ((alignment & (alignment - 1)) != 0 || alignment > kPage)
            return nullptr;
        if (size == 0)
            size = 1;
        const std::size_t slack = alignment - kAlignment;
        if (size > ~std::size_t{0} / 2 - slack)
            return nullptr;
        auto *payload = static_cast<unsigned char *>(allocate_block(size + slack));
        if (!payload || slack == 0)
            return payload;
        const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(payload);
        const std::uintptr_t aligned = (start + alignment - 1) & ~(std::uintptr_t{alignment} - 1);
        if (aligned == start)
            return payload;
        // The shift is a multiple of 32, so a forwarding header fits before it.
        auto *forward = reinterpret_cast<Header *>(aligned - kHeader);
        forward->magic = kOffsetMagic;
        forward->size_class = 0;
        forward->capacity = 0;
        forward->extent = aligned - start;
        forward->mapping = nullptr;
        return reinterpret_cast<void *>(aligned);
    }

    void *allocate_zeroed(std::size_t count, std::size_t size) noexcept
    {
        if (size != 0 && count > ~std::size_t{0} / size)
            return nullptr;
        const std::size_t bytes = count * size;
        void *address = allocate(bytes);
        if (address && !fresh_large(address))
            std::memset(address, 0, bytes);
        return address;
    }

    void release(void *address) noexcept
    {
        if (!address)
            return;
        lock();
        Block block = find(address);
        if (block.kind == Kind::small)
        {
            block.header->magic = kFreeMagic;
            auto *payload = reinterpret_cast<unsigned char *>(block.header) + kHeader;
            std::memcpy(payload, &free_lists_[block.header->size_class], sizeof(void *));
            free_lists_[block.header->size_class] = payload;
            unlock();
            return;
        }
        if (block.kind == Kind::large)
        {
            void *mapping = block.header->mapping;
            const std::size_t bytes = block.header->extent;
            block.header->magic = kFreeMagic;
            forget_large(mapping);
            unlock();
            mapped_.fetch_sub(bytes, std::memory_order_relaxed);
            backend_.unmap(mapping, bytes);
            return;
        }
        unlock(); // not ours, or already free: leave it alone
    }

    void *reallocate(void *address, std::size_t size) noexcept
    {
        if (!address)
            return allocate(size);
        if (size == 0)
        {
            release(address);
            return nullptr;
        }
        const std::size_t available = usable_size(address);
        if (available == 0)
            return nullptr; // not ours: its size is unknown
        if (size <= available)
            return address;
        void *replacement = allocate(size);
        if (!replacement)
            return nullptr;
        std::memcpy(replacement, address, available);
        release(address);
        return replacement;
    }

    // Bytes usable from address; 0 for pointers the allocator does not own.
    std::size_t usable_size(const void *address) noexcept
    {
        if (!address)
            return 0;
        lock();
        const Block block = find(const_cast<void *>(address));
        unlock();
        if (block.kind == Kind::none)
            return 0;
        const auto *payload = reinterpret_cast<const unsigned char *>(block.header) + kHeader;
        const std::size_t shift =
            static_cast<std::size_t>(static_cast<const unsigned char *>(address) - payload);
        return block.header->capacity - shift;
    }

    bool owns(const void *address) noexcept
    {
        return usable_size(address) != 0;
    }

    std::size_t mapped_bytes() const noexcept
    {
        return mapped_.load(std::memory_order_relaxed);
    }

    // Capacity of size class index (exposed for tests).
    static constexpr std::size_t class_capacity(std::size_t index) noexcept
    {
        if (index < 8)
            return (index + 1) * 32;
        const std::size_t group = (index - 8) / 4, step = (index - 8) % 4 + 1;
        const std::size_t base = std::size_t{256} << group;
        return base + step * (base / 4);
    }

    static constexpr std::size_t class_index(std::size_t size) noexcept
    {
        if (size <= 256)
            return size == 0 ? 0 : (size + 31) / 32 - 1;
        const unsigned k =
            63u - static_cast<unsigned>(__builtin_clzll(size - 1)); // 2^k < size <= 2^(k+1)
        const std::size_t base = std::size_t{1} << k, step = base / 4;
        const std::size_t sub = (size - base + step - 1) / step; // 1..4
        return 8 + (k - 8) * 4 + (sub - 1);
    }

  private:
    static constexpr std::uint32_t kSmallMagic = 0x41534d4cu;  // "LMSA"
    static constexpr std::uint32_t kLargeMagic = 0x4147524cu;  // "LRGA"
    static constexpr std::uint32_t kFreeMagic = 0x41455246u;   // "FREA"
    static constexpr std::uint32_t kOffsetMagic = 0x4146464fu; // "OFFA"

    struct Header
    {
        std::uint32_t magic;
        std::uint32_t size_class;
        std::uint64_t capacity; // usable bytes from the payload start
        std::uint64_t extent;   // large: mapping bytes; forwarding header: shift
        void *mapping;          // large: mapping base
    };
    static_assert(sizeof(Header) == kHeader);

    enum class Kind
    {
        none,
        small,
        large
    };
    struct Block
    {
        Kind kind;
        Header *header;
    };

    void lock() noexcept
    {
        unsigned spins = 0;
        while (lock_.test_and_set(std::memory_order_acquire))
        {
            if (++spins < 64)
                __builtin_ia32_pause();
            else
                spins = 0;
        }
    }
    void unlock() noexcept
    {
        lock_.clear(std::memory_order_release);
    }

    void *allocate_block(std::size_t size) noexcept
    {
        if (size > kLargest)
            return allocate_large(size);
        const std::size_t index = class_index(size);
        const std::size_t capacity = class_capacity(index);
        lock();
        if (unsigned char *payload = free_lists_[index])
        {
            std::memcpy(&free_lists_[index], payload, sizeof(void *));
            auto *header = reinterpret_cast<Header *>(payload - kHeader);
            header->magic = kSmallMagic;
            unlock();
            return payload;
        }
        const std::size_t block_bytes = kHeader + capacity;
        if (!cursor_ || static_cast<std::size_t>(limit_ - cursor_) < block_bytes)
        {
            if (chunk_count_ == kMaxChunks)
            {
                unlock();
                return nullptr;
            }
            void *chunk = backend_.map(kChunkBytes);
            if (!chunk)
            {
                unlock();
                return nullptr;
            }
            mapped_.fetch_add(kChunkBytes, std::memory_order_relaxed);
            chunks_[chunk_count_] = static_cast<unsigned char *>(chunk);
            ++chunk_count_;
            cursor_ = static_cast<unsigned char *>(chunk);
            limit_ = cursor_ + kChunkBytes;
        }
        auto *header = reinterpret_cast<Header *>(cursor_);
        cursor_ += block_bytes;
        header->magic = kSmallMagic;
        header->size_class = static_cast<std::uint32_t>(index);
        header->capacity = capacity;
        header->extent = 0;
        header->mapping = nullptr;
        unlock();
        return reinterpret_cast<unsigned char *>(header) + kHeader;
    }

    void *allocate_large(std::size_t size) noexcept
    {
        if (size > ~std::size_t{0} - kHeader - kPage)
            return nullptr;
        const std::size_t bytes = (kHeader + size + kPage - 1) & ~(kPage - 1);
        void *mapping = backend_.map(bytes);
        if (!mapping)
            return nullptr;
        lock();
        if (large_count_ == kMaxLarge)
        {
            unlock();
            backend_.unmap(mapping, bytes);
            return nullptr;
        }
        large_[large_count_] = static_cast<unsigned char *>(mapping);
        large_bytes_[large_count_] = bytes;
        ++large_count_;
        auto *header = static_cast<Header *>(mapping);
        header->magic = kLargeMagic;
        header->size_class = 0;
        header->capacity = bytes - kHeader;
        header->extent = bytes;
        header->mapping = mapping;
        unlock();
        mapped_.fetch_add(bytes, std::memory_order_relaxed);
        return static_cast<unsigned char *>(mapping) + kHeader;
    }

    bool fresh_large(void *address) noexcept
    {
        // A new mapping is already zero-filled; only the header page was written.
        lock();
        const Block block = find(address);
        unlock();
        return block.kind == Kind::large;
    }

    void forget_large(void *mapping) noexcept
    {
        for (std::size_t i = 0; i < large_count_; ++i)
            if (large_[i] == mapping)
            {
                --large_count_;
                large_[i] = large_[large_count_];
                large_bytes_[i] = large_bytes_[large_count_];
                return;
            }
    }

    // Caller holds the lock. Resolves forwarding headers and checks ownership
    // by address range before reading anything.
    Block find(void *address) noexcept
    {
        auto *p = static_cast<unsigned char *>(address);
        for (std::size_t i = 0; i < chunk_count_; ++i)
        {
            unsigned char *base = chunks_[i];
            if (p < base + kHeader || p >= base + kChunkBytes)
                continue;
            if (reinterpret_cast<std::uintptr_t>(p) % kAlignment != 0)
                return {Kind::none, nullptr};
            auto *header = reinterpret_cast<Header *>(p - kHeader);
            if (header->magic == kOffsetMagic)
            {
                if (header->extent > static_cast<std::size_t>(p - base) - kHeader)
                    return {Kind::none, nullptr};
                p -= header->extent;
                header = reinterpret_cast<Header *>(p - kHeader);
            }
            if (header->magic != kSmallMagic)
                return {Kind::none, nullptr};
            return {Kind::small, header};
        }
        for (std::size_t i = 0; i < large_count_; ++i)
        {
            unsigned char *base = large_[i];
            if (p < base + kHeader || p >= base + large_bytes_[i])
                continue;
            auto *header = reinterpret_cast<Header *>(base);
            return {header->magic == kLargeMagic ? Kind::large : Kind::none, header};
        }
        return {Kind::none, nullptr};
    }

    Backend backend_;
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::atomic<std::size_t> mapped_{0};
    unsigned char *cursor_ = nullptr;
    unsigned char *limit_ = nullptr;
    unsigned char *free_lists_[kClasses] = {};
    unsigned char *chunks_[kMaxChunks] = {};
    std::size_t chunk_count_ = 0;
    unsigned char *large_[kMaxLarge] = {};
    std::size_t large_bytes_[kMaxLarge] = {};
    std::size_t large_count_ = 0;
};

static_assert(PageAllocator::class_index(PageAllocator::kLargest) == PageAllocator::kClasses - 1);
static_assert(PageAllocator::class_capacity(PageAllocator::kClasses - 1) ==
              PageAllocator::kLargest);
} // namespace ps5::alloc
