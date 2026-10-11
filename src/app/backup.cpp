// AKENO STREAM PS5 - Backing up and restoring the user's data folder.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/backup.hpp"

#include "core/fs.hpp"
#include "core/json.hpp"

#include <algorithm>

namespace akeno::backup
{
namespace
{
// A backup folder can be written by anyone with FTP access: only well-formed
// JSON of a plausible size is taken from it.
std::optional<std::string> valid_file(const std::string &path)
{
    auto text = fs::read_text(path, kMaxFileBytes);
    if (!text || text->empty() || !json::parse(*text).ok)
        return std::nullopt;
    return text;
}
} // namespace

const std::vector<std::string> &files()
{
    static const std::vector<std::string> kFiles = {
        "settings.json",
        "history.json",
        "favorites.json",
        "sources.json",
        "streams.json",
        "websites.json",
        "web-tests.json",
        "playback-checks.json",
        "anime-providers.json",
        "anime-library.json",
        // 1.1.0: website providers. 1.0.0 does not back it up, restore it or
        // delete it; it stays in the data folder untouched by a rollback.
        "anime-website-providers.json",
        // 1.1.0-rc.2: which providers' episodes played natively on this console.
        "anime-native-playback.json",
    };
    return kFiles;
}

int count(const std::string &dir)
{
    if (dir.empty() || !fs::is_directory(dir))
        return 0;
    return static_cast<int>(std::count_if(files().begin(), files().end(),
                                          [&dir](const std::string &name)
                                          { return fs::exists(fs::join(dir, name)); }));
}

bool is_empty(const std::string &data_dir)
{
    return count(data_dir) == 0;
}

Result save(const std::string &data_dir, const std::string &dir)
{
    Result r;
    if (dir.empty())
    {
        r.error = "No backup folder is available.";
        return r;
    }
    if (!fs::is_directory(dir) && !fs::make_directory(dir))
    {
        r.error = "Cannot create " + dir + ".";
        return r;
    }
    std::string error;
    for (const std::string &name : files())
    {
        const std::string from = fs::join(data_dir, name);
        if (!fs::exists(from))
            continue;
        const auto text = valid_file(from);
        if (!text)
        {
            ++r.skipped;
            continue;
        }
        if (!fs::write_atomic(fs::join(dir, name), *text, &error))
        {
            r.error = "Cannot write to " + dir + " (" + error + ").";
            return r;
        }
        ++r.copied;
    }
    return r;
}

Result restore(const std::string &dir, const std::string &data_dir)
{
    Result r;
    std::string error;
    for (const std::string &name : files())
    {
        const std::string from = fs::join(dir, name);
        if (!fs::exists(from))
            continue;
        const auto text = valid_file(from);
        if (!text)
        {
            ++r.skipped;
            continue;
        }
        const std::string to = fs::join(data_dir, name);
        if (const auto current = fs::read_text(to, kMaxFileBytes); current && *current != *text)
            (void)fs::write_atomic(to + ".before-restore", *current);
        if (!fs::write_atomic(to, *text, &error))
        {
            r.error = "Cannot write " + to + " (" + error + ").";
            return r;
        }
        ++r.copied;
    }
    if (r.copied == 0 && r.error.empty())
        r.error = r.skipped ? "The backup's files are damaged." : "No backup in " + dir + ".";
    return r;
}

bool stage_restore(const std::string &dir, const std::string &data_dir,
                   const std::vector<std::string> &locations, std::string *error)
{
    const auto fail = [error](std::string why)
    {
        if (error)
            *error = std::move(why);
        return false;
    };
    if (std::find(locations.begin(), locations.end(), dir) == locations.end())
        return fail("Not a backup folder AKENO STREAM uses.");
    if (count(dir) == 0)
        return fail("No backup in " + dir + ".");
    std::string why;
    if (!fs::write_atomic(fs::join(data_dir, kStagedFile), dir + "\n", &why))
        return fail(why);
    return true;
}

std::string restore_at_start(const std::string &data_dir, const std::vector<std::string> &locations,
                             Result *result)
{
    std::string from;
    const std::string staged_path = fs::join(data_dir, kStagedFile);
    if (auto staged = fs::read_text(staged_path, 4096))
    {
        (void)fs::remove_file(staged_path);
        std::string dir = *staged;
        while (!dir.empty() && (dir.back() == '\n' || dir.back() == '\r'))
            dir.pop_back();
        // Only a folder AKENO itself offers, never a path someone wrote into the file.
        if (std::find(locations.begin(), locations.end(), dir) != locations.end())
            from = dir;
    }
    if (from.empty() && is_empty(data_dir))
        for (const std::string &dir : locations)
            if (count(dir) > 0)
            {
                from = dir;
                break;
            }
    if (from.empty())
        return {};
    Result r = restore(from, data_dir);
    if (result)
        *result = r;
    return r.copied > 0 ? from : std::string{};
}
} // namespace akeno::backup
