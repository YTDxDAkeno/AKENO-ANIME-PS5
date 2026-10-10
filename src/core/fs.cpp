// AKENO STREAM PS5 - Small, safe file helpers (POSIX; console and host).
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/fs.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace akeno::fs
{
namespace
{
template <typename Container>
std::optional<Container> read_all(const std::string &path, std::size_t max_bytes)
{
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0)
        return std::nullopt;
    Container out;
    char buffer[16384];
    for (;;)
    {
        const ssize_t got = ::read(fd, buffer, sizeof(buffer));
        if (got < 0)
        {
            ::close(fd);
            return std::nullopt;
        }
        if (got == 0)
            break;
        if (out.size() + static_cast<std::size_t>(got) > max_bytes)
        {
            ::close(fd);
            return std::nullopt;
        }
        out.insert(out.end(), buffer, buffer + got);
    }
    ::close(fd);
    return out;
}

std::string lower(std::string s)
{
    for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
} // namespace

std::optional<std::string> read_text(const std::string &path, std::size_t max_bytes)
{
    return read_all<std::string>(path, max_bytes);
}

std::optional<std::vector<std::uint8_t>> read_bytes(const std::string &path, std::size_t max_bytes)
{
    return read_all<std::vector<std::uint8_t>>(path, max_bytes);
}

bool write_atomic(const std::string &path, const std::string &contents, std::string *error)
{
    const std::string temporary = path + ".tmp";
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
    {
        if (error)
            *error = std::strerror(errno);
        return false;
    }
    std::size_t written = 0;
    while (written < contents.size())
    {
        const ssize_t n = ::write(fd, contents.data() + written, contents.size() - written);
        if (n <= 0)
        {
            if (error)
                *error = std::strerror(errno);
            ::close(fd);
            ::unlink(temporary.c_str());
            return false;
        }
        written += static_cast<std::size_t>(n);
    }
    ::close(fd);
    if (::rename(temporary.c_str(), path.c_str()) != 0)
    {
        if (error)
            *error = std::strerror(errno);
        ::unlink(temporary.c_str());
        return false;
    }
    return true;
}

bool exists(const std::string &path)
{
    struct stat facts
    {
    };
    return ::stat(path.c_str(), &facts) == 0;
}

bool is_directory(const std::string &path)
{
    struct stat facts
    {
    };
    return ::stat(path.c_str(), &facts) == 0 && S_ISDIR(facts.st_mode);
}

bool make_directory(const std::string &path)
{
    return ::mkdir(path.c_str(), 0777) == 0 || errno == EEXIST;
}

bool remove_file(const std::string &path)
{
    return ::unlink(path.c_str()) == 0;
}

std::optional<std::vector<Entry>> list(const std::string &path, std::string *error,
                                       std::size_t max_entries)
{
    DIR *dir = ::opendir(path.c_str());
    if (!dir)
    {
        if (error)
            *error = std::strerror(errno);
        return std::nullopt;
    }
    std::vector<Entry> entries;
    while (dirent *item = ::readdir(dir))
    {
        const std::string name = item->d_name;
        if (name == "." || name == ".." || name.empty())
            continue;
        if (entries.size() >= max_entries)
            break;
        Entry e;
        e.name = name;
        struct stat facts
        {
        };
        const std::string full = join(path, name);
        if (::stat(full.c_str(), &facts) == 0)
        {
            e.directory = S_ISDIR(facts.st_mode);
            e.size = static_cast<std::uint64_t>(facts.st_size);
        }
        else
        {
            e.directory = item->d_type == DT_DIR;
        }
        entries.push_back(std::move(e));
    }
    ::closedir(dir);
    std::sort(entries.begin(), entries.end(),
              [](const Entry &a, const Entry &b)
              {
                  if (a.directory != b.directory)
                      return a.directory;
                  return lower(a.name) < lower(b.name);
              });
    return entries;
}

std::string join(const std::string &a, const std::string &b)
{
    if (a.empty())
        return b;
    if (a.back() == '/')
        return a + b;
    return a + "/" + b;
}

std::string parent(const std::string &path)
{
    if (path.size() <= 1)
        return path;
    std::string p = path;
    while (p.size() > 1 && p.back() == '/')
        p.pop_back();
    const std::size_t slash = p.rfind('/');
    if (slash == std::string::npos)
        return ".";
    return slash == 0 ? "/" : p.substr(0, slash);
}

std::string file_name(const std::string &path)
{
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool safe_path(const std::string &path)
{
    if (path.empty() || path.size() > 1024 || path.front() != '/')
        return false;
    if (path.find("/../") != std::string::npos || path.ends_with("/.."))
        return false;
    return std::none_of(path.begin(), path.end(),
                        [](char c) { return static_cast<unsigned char>(c) < 0x20; });
}
} // namespace akeno::fs
