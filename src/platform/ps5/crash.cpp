// AKENO STREAM PS5 - Crash and out-of-memory reporting.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A crash on the console ends in the system's "CE-108255-1" dialog with no
// detail. The handler below first sends a system notification naming the
// signal, the code address relative to eboot.bin and the current stage, then
// writes the same text plus a frame-pointer backtrace to <data>/crash.txt,
// and returns: SA_RESETHAND restores the default action, so the faulting
// instruction runs again and the system ends the app as before.
//
// Only fixed buffers and system calls are used once a signal arrives; the
// heap or the C library may be what failed.
#include "app/version.hpp"
#include "platform/platform.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>

extern "C"
{
    int sceKernelSendNotificationRequest(std::uint32_t device, void *request, std::size_t size,
                                         int blocking);
    std::size_t ps5AllocatorMappedBytes() noexcept;
    // Defined by the linker at the first byte of the executable image.
    extern char __dso_handle __attribute__((visibility("hidden")));
}

namespace akeno::platform
{
namespace
{
struct NotificationRequest
{
    std::uint8_t reserved[45];
    char message[3075];
};

std::atomic<const char *> g_stage{"startup"};
NotificationRequest g_request{};
char g_report[2048];
alignas(64) unsigned char g_alternate_stack[64 * 1024];

// Minimal formatting into a fixed buffer.
struct Writer
{
    char *out;
    std::size_t capacity;
    std::size_t used = 0;

    void text(const char *s)
    {
        while (s && *s && used + 1 < capacity)
            out[used++] = *s++;
        out[used] = '\0';
    }
    void hex(std::uintptr_t value)
    {
        char digits[19] = "0x";
        int n = 0;
        char reversed[16];
        do
        {
            reversed[n++] = "0123456789abcdef"[value & 0xf];
            value >>= 4;
        } while (value && n < 16);
        for (int i = 0; i < n; ++i)
            digits[2 + i] = reversed[n - 1 - i];
        digits[2 + n] = '\0';
        text(digits);
    }
    void decimal(std::size_t value)
    {
        char reversed[24];
        int n = 0;
        do
        {
            reversed[n++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value && n < 23);
        char digits[24];
        for (int i = 0; i < n; ++i)
            digits[i] = reversed[n - 1 - i];
        digits[n] = '\0';
        text(digits);
    }
    // "eboot+0x..." for addresses inside the executable image.
    void code(std::uintptr_t address)
    {
        const auto base = reinterpret_cast<std::uintptr_t>(&__dso_handle);
        if (address >= base && address - base < (std::uintptr_t{256} << 20))
        {
            text("eboot+");
            hex(address - base);
        }
        else
        {
            hex(address);
        }
    }
};

const char *signal_name(int signal)
{
    switch (signal)
    {
    case SIGSEGV:
        return "SIGSEGV (invalid memory access)";
    case SIGBUS:
        return "SIGBUS (bus error)";
    case SIGILL:
        return "SIGILL (trap or invalid instruction)";
    case SIGFPE:
        return "SIGFPE (arithmetic error)";
    case SIGTRAP:
        return "SIGTRAP";
    case SIGABRT:
        return "SIGABRT (abort)";
    case SIGSYS:
        return "SIGSYS (bad system call)";
    default:
        return "signal";
    }
}

void send_notification(const char *message)
{
    std::size_t n = 0;
    while (message[n] && n + 1 < sizeof(g_request.message))
    {
        g_request.message[n] = message[n];
        ++n;
    }
    g_request.message[n] = '\0';
    (void)sceKernelSendNotificationRequest(0, &g_request, sizeof(g_request), 0);
}

void write_file(const char *path, const char *text, std::size_t bytes)
{
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return;
    (void)::write(fd, text, bytes);
    (void)::close(fd);
}

void on_signal(int signal, siginfo_t *info, void *context)
{
    const auto *uc = static_cast<const ucontext_t *>(context);
    const std::uintptr_t pc = uc ? static_cast<std::uintptr_t>(uc->uc_mcontext.mc_rip) : 0;
    const std::uintptr_t fault = info ? reinterpret_cast<std::uintptr_t>(info->si_addr) : 0;

    Writer w{g_report, sizeof(g_report)};
    w.text("AKENO STREAM ");
    w.text(akeno::kAppVersion);
    w.text(" crashed: ");
    w.text(signal_name(signal));
    w.text(" at ");
    w.code(pc);
    if (signal == SIGSEGV || signal == SIGBUS)
    {
        w.text(", address ");
        w.hex(fault);
    }
    w.text(", during ");
    w.text(g_stage.load(std::memory_order_relaxed));
    w.text(". Heap ");
    w.decimal(ps5AllocatorMappedBytes() >> 20);
    w.text(" MiB.");
    send_notification(g_report);

    // Frame-pointer backtrace. Reading a bad frame faults again; the handler
    // is already reset, so that only ends the process a little earlier.
    w.text("\nBacktrace:");
    if (uc)
    {
        std::uintptr_t frame = static_cast<std::uintptr_t>(uc->uc_mcontext.mc_rbp);
        const std::uintptr_t low = static_cast<std::uintptr_t>(uc->uc_mcontext.mc_rsp);
        for (int depth = 0; depth < 16; ++depth)
        {
            if (frame < low || frame - low > (std::uintptr_t{16} << 20) || frame % 8 != 0)
                break;
            const auto *slots = reinterpret_cast<const std::uintptr_t *>(frame);
            w.text(" ");
            w.code(slots[1]);
            if (slots[0] <= frame)
                break;
            frame = slots[0];
        }
    }
    w.text("\n");
    write_file("/download0/akeno/crash.txt", g_report, w.used);
}
} // namespace

void install_crash_reporter() noexcept
{
    stack_t alternate{};
    alternate.ss_sp = g_alternate_stack;
    alternate.ss_size = sizeof(g_alternate_stack);
    alternate.ss_flags = 0;
    (void)sigaltstack(&alternate, nullptr);

    struct sigaction action
    {
    };
    action.sa_sigaction = on_signal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    for (int signal : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP, SIGABRT, SIGSYS})
        (void)sigaction(signal, &action, nullptr);
}

void set_stage(const char *stage) noexcept
{
    g_stage.store(stage ? stage : "unknown", std::memory_order_relaxed);
}

const char *stage() noexcept
{
    return g_stage.load(std::memory_order_relaxed);
}

std::size_t heap_bytes() noexcept
{
    return ps5AllocatorMappedBytes();
}
} // namespace akeno::platform

// Called by the allocator (tooling/native/app_cpp_runtime.cpp) just before it
// stops the app because a C++ allocation could not be satisfied.
extern "C" void ps5AllocationFailed(std::size_t bytes)
{
    using akeno::platform::Writer;
    Writer w{akeno::platform::g_report, sizeof(akeno::platform::g_report)};
    w.text("AKENO STREAM ran out of memory: ");
    w.decimal(bytes);
    w.text(" bytes requested with ");
    w.decimal(ps5AllocatorMappedBytes() >> 20);
    w.text(" MiB in use, during ");
    w.text(akeno::platform::stage());
    w.text(".");
    akeno::platform::send_notification(akeno::platform::g_report);
    akeno::platform::write_file("/download0/akeno/crash.txt", akeno::platform::g_report, w.used);
}
