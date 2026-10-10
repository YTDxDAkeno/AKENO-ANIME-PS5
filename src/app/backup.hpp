// AKENO STREAM PS5 - Backing up and restoring the user's data folder.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The data folder (/download0/akeno) belongs to the title's registration: a
// re-registration in the homebrew loader - needed once to refresh the name
// and icon the dashboard shows - can remove it together with the title. A
// copy outside it survives that:
//   - platform::backup_dir() (/data/akeno-backup, which ShadowMountPlus mounts
//     writable into the app's sandbox), written at every start;
//   - akeno-backup/ in the install folder, which a PC can fill over FTP.
// A data folder without any of the user's files is restored from the first
// backup found before anything is loaded; a restore chosen in Settings is
// staged and applied at the next start, so the running app cannot overwrite
// it. Existing data is never replaced without that explicit choice.
#pragma once

#include <string>
#include <vector>

namespace akeno::backup
{
// The files that hold the user's data. The YouTube key (secrets.json), crash
// reports and exported diagnostics are not backed up.
const std::vector<std::string> &files();

inline constexpr std::size_t kMaxFileBytes = 4u * 1024u * 1024u;
inline constexpr char kStagedFile[] = "restore-from.txt";

struct Result
{
    int copied = 0;
    int skipped = 0;   // unreadable, too large or not JSON
    std::string error; // why nothing was copied (empty on success)
};

// How many of the user's files dir holds (0: no backup there).
int count(const std::string &dir);
// True when data_dir holds none of the user's files (a fresh registration).
bool is_empty(const std::string &data_dir);
// Copies the user's files from data_dir into dir (created when missing).
Result save(const std::string &data_dir, const std::string &dir);
// Copies the valid files of dir into data_dir; a file it replaces is kept as
// <name>.before-restore.
Result restore(const std::string &dir, const std::string &data_dir);

// Remembers dir for the next start (dir must be one of locations).
bool stage_restore(const std::string &dir, const std::string &data_dir,
                   const std::vector<std::string> &locations, std::string *error);
// At start, before the data is loaded: applies a staged restore, or restores
// an empty data folder from the first location holding a backup. Returns the
// folder restored from, empty when nothing was restored.
std::string restore_at_start(const std::string &data_dir, const std::vector<std::string> &locations,
                             Result *result);
} // namespace akeno::backup
