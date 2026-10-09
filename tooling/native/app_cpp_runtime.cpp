/*
 * ps5-native-app-boilerplate - Target allocation runtime.
 * Copyright (C) 2026 BlackBearReloaded
 * Copyright (C) 2026 AKENO STREAM contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Gives the executable one coherent allocator family - malloc, calloc,
 * realloc, free, posix_memalign, aligned_alloc, memalign, strdup, strndup and
 * every C++ new/delete - backed by anonymous pages (page_allocator.hpp). The
 * system malloc behind the clean-room libc.prx is not a general-purpose heap
 * and returns null for real workloads, so none of these may be imported:
 * tools/check-imports.py fails the build if one is.
 */

#include "page_allocator.hpp"

#include <cstddef>
#include <cstring>
#include <new>

extern "C"
{
    void *mmap(void *address, std::size_t length, int protection, int flags, int descriptor,
               long offset);
    int munmap(void *address, std::size_t length);

    std::size_t ps5AllocatorMappedBytes() noexcept;

    // The application may define its own ps5AllocationFailed to report an
    // exhausted heap (for example as a system notification) before the trap.
    __attribute__((weak)) void ps5AllocationFailed(std::size_t bytes)
    {
        (void)bytes;
    }
}

namespace
{
constexpr int kProtectionReadWrite = 3;
constexpr int kMapPrivateAnonymous = 0x1002; // MAP_PRIVATE | MAP_ANON

void *map_pages(std::size_t bytes) noexcept
{
    void *mapping = mmap(nullptr, bytes, kProtectionReadWrite, kMapPrivateAnonymous, -1, 0);
    return mapping == reinterpret_cast<void *>(-1) ? nullptr : mapping;
}

void unmap_pages(void *address, std::size_t bytes) noexcept
{
    (void)munmap(address, bytes);
}

constinit ps5::alloc::PageAllocator g_heap{{map_pages, unmap_pages}};

[[noreturn]] void allocation_failure(std::size_t bytes) noexcept
{
    ps5AllocationFailed(bytes);
    __builtin_trap();
}

void *allocate_or_fail(std::size_t size, std::size_t alignment)
{
    if (void *address = g_heap.allocate(size, alignment))
        return address;
    allocation_failure(size);
}
} // namespace

extern "C"
{
    void *malloc(std::size_t size)
    {
        return g_heap.allocate(size);
    }

    void free(void *address)
    {
        g_heap.release(address);
    }

    void *calloc(std::size_t count, std::size_t size)
    {
        return g_heap.allocate_zeroed(count, size);
    }

    void *realloc(void *address, std::size_t size)
    {
        return g_heap.reallocate(address, size);
    }

    int posix_memalign(void **address, std::size_t alignment, std::size_t size)
    {
        if (!address || alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0)
            return 22; // EINVAL
        void *allocation = g_heap.allocate(size, alignment);
        if (!allocation)
            return 12; // ENOMEM
        *address = allocation;
        return 0;
    }

    void *aligned_alloc(std::size_t alignment, std::size_t size)
    {
        return g_heap.allocate(size, alignment);
    }

    void *memalign(std::size_t alignment, std::size_t size)
    {
        return g_heap.allocate(size, alignment);
    }

    std::size_t malloc_usable_size(const void *address)
    {
        return g_heap.usable_size(address);
    }

    // libcurl, OpenSSL and libpsl free what strdup returns: it must come from
    // this heap, not the system's.
    char *strdup(const char *text)
    {
        const std::size_t length = std::strlen(text);
        auto *copy = static_cast<char *>(g_heap.allocate(length + 1));
        if (copy)
            std::memcpy(copy, text, length + 1);
        return copy;
    }

    char *strndup(const char *text, std::size_t limit)
    {
        std::size_t length = 0;
        while (length < limit && text[length] != '\0')
            ++length;
        auto *copy = static_cast<char *>(g_heap.allocate(length + 1));
        if (copy)
        {
            std::memcpy(copy, text, length);
            copy[length] = '\0';
        }
        return copy;
    }

    std::size_t ps5AllocatorMappedBytes() noexcept
    {
        return g_heap.mapped_bytes();
    }

    __attribute__((noinline, visibility("hidden"))) bool
    ps5ObserveOwnedAllocation(const void *address) noexcept
    {
        __asm__ volatile("" : : "r"(address) : "memory");
        return address != nullptr;
    }
}

void *operator new(std::size_t size)
{
    return allocate_or_fail(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__);
}

void *operator new[](std::size_t size)
{
    return allocate_or_fail(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__);
}

void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
    return g_heap.allocate(size);
}

void *operator new[](std::size_t size, const std::nothrow_t &) noexcept
{
    return g_heap.allocate(size);
}

void *operator new(std::size_t size, std::align_val_t alignment)
{
    return allocate_or_fail(size, static_cast<std::size_t>(alignment));
}

void *operator new[](std::size_t size, std::align_val_t alignment)
{
    return allocate_or_fail(size, static_cast<std::size_t>(alignment));
}

void *operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept
{
    return g_heap.allocate(size, static_cast<std::size_t>(alignment));
}

void *operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept
{
    return g_heap.allocate(size, static_cast<std::size_t>(alignment));
}

void operator delete(void *address) noexcept
{
    g_heap.release(address);
}

void operator delete[](void *address) noexcept
{
    g_heap.release(address);
}

void operator delete(void *address, std::size_t) noexcept
{
    g_heap.release(address);
}

void operator delete[](void *address, std::size_t) noexcept
{
    g_heap.release(address);
}

void operator delete(void *address, std::align_val_t) noexcept
{
    g_heap.release(address);
}

void operator delete[](void *address, std::align_val_t) noexcept
{
    g_heap.release(address);
}

void operator delete(void *address, std::size_t, std::align_val_t) noexcept
{
    g_heap.release(address);
}

void operator delete[](void *address, std::size_t, std::align_val_t) noexcept
{
    g_heap.release(address);
}

void operator delete(void *address, const std::nothrow_t &) noexcept
{
    g_heap.release(address);
}

void operator delete[](void *address, const std::nothrow_t &) noexcept
{
    g_heap.release(address);
}

void operator delete(void *address, std::align_val_t, const std::nothrow_t &) noexcept
{
    g_heap.release(address);
}

void operator delete[](void *address, std::align_val_t, const std::nothrow_t &) noexcept
{
    g_heap.release(address);
}
