// AKENO STREAM PS5 - Tests for the executable's page-backed allocator.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The console build replaces malloc/new with this allocator; these tests run
// the same header against the build machine's mmap.
#include "../../tooling/native/page_allocator.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <random>
#include <sys/mman.h>
#include <thread>
#include <vector>

namespace
{
using ps5::alloc::PageAllocator;

void *host_map(std::size_t bytes) noexcept
{
    void *p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
}

void host_unmap(void *address, std::size_t bytes) noexcept
{
    munmap(address, bytes);
}

std::unique_ptr<PageAllocator> make()
{
    return std::make_unique<PageAllocator>(ps5::alloc::Backend{host_map, host_unmap});
}

bool aligned(const void *p, std::size_t alignment)
{
    return reinterpret_cast<std::uintptr_t>(p) % alignment == 0;
}
} // namespace

TEST(Allocator, SizeClassesCoverEverySize)
{
    std::size_t previous = 0;
    for (std::size_t size = 1; size <= PageAllocator::kLargest; size += (size < 4096 ? 1 : 997))
    {
        const std::size_t index = PageAllocator::class_index(size);
        ASSERT_LT(index, PageAllocator::kClasses) << size;
        const std::size_t capacity = PageAllocator::class_capacity(index);
        ASSERT_GE(capacity, size) << size;
        ASSERT_EQ(capacity % 32, 0u) << size;
        ASSERT_GE(index, previous) << size;
        if (index > 0)
            ASSERT_LT(PageAllocator::class_capacity(index - 1), size) << size;
        previous = index;
    }
    // Waste is bounded: at most a quarter above 256 bytes.
    EXPECT_LE(PageAllocator::class_capacity(PageAllocator::class_index(1000)), 1280u);
}

TEST(Allocator, BlocksAre32ByteAlignedAndUsable)
{
    auto heap = make();
    std::vector<void *> blocks;
    for (std::size_t size : std::initializer_list<std::size_t>{
             1, 7, 32, 33, 100, 255, 256, 257, 4000, 65536, 1u << 20, (1u << 20) + 1, 8u << 20})
    {
        auto *p = static_cast<unsigned char *>(heap->allocate(size));
        ASSERT_NE(p, nullptr) << size;
        EXPECT_TRUE(aligned(p, 32)) << size;
        EXPECT_GE(heap->usable_size(p), size);
        std::memset(p, 0xab, size);
        blocks.push_back(p);
    }
    for (void *p : blocks)
        heap->release(p);
}

TEST(Allocator, HonoursLargerAlignments)
{
    auto heap = make();
    for (std::size_t alignment : std::initializer_list<std::size_t>{64, 128, 256, 4096})
        for (std::size_t size : std::initializer_list<std::size_t>{1, 100, 5000, 2u << 20})
        {
            auto *p = static_cast<unsigned char *>(heap->allocate(size, alignment));
            ASSERT_NE(p, nullptr);
            EXPECT_TRUE(aligned(p, alignment)) << alignment << " " << size;
            EXPECT_GE(heap->usable_size(p), size);
            std::memset(p, 0x5a, size);
            heap->release(p);
        }
    EXPECT_EQ(heap->allocate(16, 48), nullptr); // not a power of two
}

TEST(Allocator, ReusesFreedBlocksOfTheSameClass)
{
    auto heap = make();
    void *a = heap->allocate(100);
    heap->release(a);
    void *b = heap->allocate(120); // same 128-byte class
    EXPECT_EQ(a, b);
    heap->release(b);
}

TEST(Allocator, LargeBlocksReturnTheirPages)
{
    auto heap = make();
    const std::size_t before = heap->mapped_bytes();
    void *p = heap->allocate(24u << 20);
    ASSERT_NE(p, nullptr);
    EXPECT_GE(heap->mapped_bytes(), before + (24u << 20));
    heap->release(p);
    EXPECT_EQ(heap->mapped_bytes(), before);
}

TEST(Allocator, ReallocatePreservesContents)
{
    auto heap = make();
    auto *p = static_cast<unsigned char *>(heap->reallocate(nullptr, 10));
    ASSERT_NE(p, nullptr);
    for (int i = 0; i < 10; ++i)
        p[i] = static_cast<unsigned char>(i);
    // Within the block's capacity the pointer stays.
    EXPECT_EQ(heap->reallocate(p, 30), p);
    auto *q = static_cast<unsigned char *>(heap->reallocate(p, 3u << 20)); // into a large block
    ASSERT_NE(q, nullptr);
    for (int i = 0; i < 10; ++i)
        EXPECT_EQ(q[i], i);
    auto *r = static_cast<unsigned char *>(heap->reallocate(q, 64));
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r, q); // shrinking keeps the block
    EXPECT_EQ(heap->reallocate(r, 0), nullptr);
}

TEST(Allocator, ZeroedAllocationsAreZeroEvenWhenReused)
{
    auto heap = make();
    auto *p = static_cast<unsigned char *>(heap->allocate(500));
    std::memset(p, 0xff, 500);
    heap->release(p);
    auto *q = static_cast<unsigned char *>(heap->allocate_zeroed(50, 10));
    ASSERT_EQ(q, p);
    for (int i = 0; i < 500; ++i)
        ASSERT_EQ(q[i], 0) << i;
    EXPECT_EQ(heap->allocate_zeroed(~std::size_t{0} / 2, 4), nullptr); // overflow
    heap->release(q);
}

TEST(Allocator, LeavesForeignAndRepeatedPointersAlone)
{
    auto heap = make();
    int on_stack = 0;
    void *from_system = std::malloc(64);
    heap->release(&on_stack);
    heap->release(from_system);
    EXPECT_EQ(heap->usable_size(&on_stack), 0u);
    EXPECT_EQ(heap->usable_size(from_system), 0u);
    EXPECT_EQ(heap->reallocate(from_system, 128), nullptr);
    std::free(from_system);

    void *p = heap->allocate(64);
    heap->release(p);
    heap->release(p); // double free is ignored
    void *a = heap->allocate(64);
    void *b = heap->allocate(64);
    EXPECT_NE(a, b);
    heap->release(a);
    heap->release(b);
}

TEST(Allocator, IsSafeAcrossThreads)
{
    auto heap = make();
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back(
            [&, t]
            {
                std::mt19937 random(static_cast<unsigned>(t + 1));
                std::vector<std::pair<unsigned char *, std::size_t>> live;
                for (int i = 0; i < 20000; ++i)
                {
                    if (live.size() < 64 && (live.empty() || random() % 3 != 0))
                    {
                        const std::size_t size =
                            random() % 3 == 0 ? random() % 200000 : random() % 512;
                        auto *p = static_cast<unsigned char *>(heap->allocate(size + 1));
                        if (!p || !aligned(p, 32))
                        {
                            ++failures;
                            continue;
                        }
                        std::memset(p, t + 1, size + 1);
                        live.emplace_back(p, size + 1);
                    }
                    else
                    {
                        const std::size_t pick = random() % live.size();
                        auto [p, size] = live[pick];
                        for (std::size_t k = 0; k < size; k += 97)
                            if (p[k] != t + 1)
                                ++failures;
                        heap->release(p);
                        live[pick] = live.back();
                        live.pop_back();
                    }
                }
                for (auto [p, size] : live)
                    heap->release(p);
            });
    for (auto &thread : threads)
        thread.join();
    EXPECT_EQ(failures.load(), 0);
}
