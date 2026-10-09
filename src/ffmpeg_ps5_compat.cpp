// Akeno Anime - compatibility symbols needed by the PacBrew FFmpeg build.
// Copyright (C) 2026 Akeno Anime contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PacBrew's FFmpeg build targets a FreeBSD-like libc. The native PS5 title
// runtime exposes only a subset of its POSIX/locale compatibility surface.
// Do not lie about unsupported locale facilities or ignore assertions.

#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <pthread.h>

extern "C" {

struct tm *localtime_r(const time_t *value, struct tm *destination)
{
    if (!value || !destination)
        return nullptr;
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    if (pthread_mutex_lock(&lock) != 0)
        return nullptr;
    const tm *converted = localtime(value);
    if (converted)
        *destination = *converted;
    (void)pthread_mutex_unlock(&lock);
    return converted ? destination : nullptr;
}

[[noreturn]] void __assert(const char *function, const char *file, int line,
                           const char *expression)
{
    std::fprintf(stderr, "[Akeno Anime] assert: %s (%s:%d %s)\n",
                 expression ? expression : "?",
                 file ? file : "?", line, function ? function : "?");
    std::abort();
}

// libiconv's localcharset detection is the only current nl_langinfo caller.
// PS5 homebrew uses UTF-8 strings; no other nl_langinfo items are supported.
char *nl_langinfo(int item)
{
    (void)item;
    static char utf8[] = "UTF-8";
    return utf8;
}

// FreeBSD stdlib.h declares: extern int ___mb_cur_max(void).
// Allow four-byte UTF-8 characters in libiconv's wchar conversion.
int ___mb_cur_max(void)
{
    return 4;
}

} // extern "C"
