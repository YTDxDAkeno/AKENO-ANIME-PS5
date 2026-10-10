// AKENO STREAM PS5 - Small, safe file helpers (POSIX; console and host).
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace akeno::fs
{
struct Entry
{
    std::string name;
    bool directory = false;
    std::uint64_t size = 0;
};

std::optional<std::string> read_text(const std::string &path,
                                     std::size_t max_bytes = 16u * 1024u * 1024u);
std::optional<std::vector<std::uint8_t>> read_bytes(const std::string &path,
                                                    std::size_t max_bytes = 64u * 1024u * 1024u);
// Writes to path.tmp then renames over path, so readers never see half a file.
bool write_atomic(const std::string &path, const std::string &contents,
                  std::string *error = nullptr);
bool exists(const std::string &path);
bool is_directory(const std::string &path);
bool make_directory(const std::string &path);
bool remove_file(const std::string &path);
// Directory listing without "." and ".."; directories first, then by name
// (case-insensitive). error receives strerror text on failure.
std::optional<std::vector<Entry>> list(const std::string &path, std::string *error = nullptr,
                                       std::size_t max_entries = 5000);
std::string join(const std::string &a, const std::string &b);
std::string parent(const std::string &path);
std::string file_name(const std::string &path);
// Rejects paths with "..", NUL or control characters; used before opening
// anything chosen through the UI.
bool safe_path(const std::string &path);
} // namespace akeno::fs
