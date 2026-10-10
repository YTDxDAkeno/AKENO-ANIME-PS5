// AKENO STREAM PS5 - C library entry points the console libc lacks.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The payload SDK's libc++abi is built against FreeBSD headers, whose assert()
// calls __assert. The console's libSceLibcInternal does not export it.
#include <cstdio>

extern "C" [[noreturn]] void __assert(const char *function, const char *file, int line,
                                      const char *expression)
{
    std::fprintf(stderr, "[akeno] assertion failed: %s (%s:%d, %s)\n",
                 expression ? expression : "?", file ? file : "?", line, function ? function : "?");
    __builtin_trap();
}
