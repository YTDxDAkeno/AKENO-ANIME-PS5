// AKENO STREAM PS5 - The files that keep the providers the user added, safe across versions.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Two files in the data folder:
//   anime-providers.json          AKENO catalogues and PeerTube (since 1.0.0)
//   anime-website-providers.json  website providers (since 1.1.0)
//
// Why two: 1.0.0 reads anime-providers.json, maps any type it does not know
// to "catalog" and writes that back at its next change. A website provider
// stored there would turn into a broken catalogue after a rollback to 1.0.0
// and stay broken after upgrading again. 1.0.0 never opens the second file,
// and 1.1.0 never writes a website provider into the first one, so rolling
// back and upgrading again leaves both exactly as they were.
//
// Both files are read defensively and nothing is silently reinterpreted:
//   - an entry this version cannot use (an unknown type or adapter, a newer
//     adapter format, a missing or malformed field, a duplicate id) is kept
//     in the file with all its fields, reported, and not loaded;
//   - unknown fields of a usable entry, and unknown top-level fields, are
//     written back unchanged;
//   - a damaged file is kept as <file>.corrupt (never over a different one)
//     and the previous good copy <file>.bak, kept at every save, is used;
//   - a file in a newer format is loaded as far as it says older readers may
//     and is never written by this version.
// Writes go to <file>.tmp first and are renamed over the file, so an
// interrupted write leaves the previous file in place.
#pragma once

#include "anime/provider.hpp"
#include "core/json.hpp"

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace akeno::anime
{
inline constexpr char kProvidersFile[] = "anime-providers.json";
inline constexpr char kWebsiteProvidersFile[] = "anime-website-providers.json";
inline constexpr char kWebsiteProvidersFormat[] = "akeno-website-providers";
inline constexpr int kProvidersFileVersion = 1;
inline constexpr int kWebsiteProvidersFileVersion = 1;
// Website provider ids start with this; 1.0.0 makes ids from letters, digits
// and '-' only, so an id it creates can never collide with one of these.
inline constexpr char kWebsiteIdPrefix[] = "web_";

struct StoredProviders
{
    std::vector<ProviderConfig> providers; // usable entries, in file order
    std::vector<std::string> problems;     // one sentence each (diagnostics)
    int kept_aside = 0;                    // entries kept in the file but not loaded
    bool read_only = false;                // never written by this version
    bool recovered = false;                // loaded from <file>.bak
};

class ProviderFile final
{
  public:
    enum class Kind : std::uint8_t
    {
        catalogs, // anime-providers.json: catalog and peertube entries
        websites, // anime-website-providers.json: website entries
    };
    // The format version an adapter id is read with; 0: unknown adapter.
    using AdapterVersion = std::function<int(const std::string &)>;

    ProviderFile(std::string data_dir, Kind kind);

    // taken: ids already in use (built-in providers, the other file).
    StoredProviders load(const std::set<std::string> &taken, std::size_t max_entries,
                         const AdapterVersion &adapter_version = {});
    // Writes providers (each over the entry it was read from, keeping fields
    // this version does not know) followed by every entry kept aside at load.
    // Refused when the file is read-only.
    bool save(const std::vector<ProviderConfig> &providers, std::string *error);

    [[nodiscard]] std::string path() const;
    [[nodiscard]] bool read_only() const noexcept
    {
        return read_only_;
    }
    [[nodiscard]] const std::string &read_only_reason() const noexcept
    {
        return read_only_reason_;
    }
    // Ids of entries kept aside: not loaded, but not free for a new provider.
    [[nodiscard]] const std::set<std::string> &reserved_ids() const noexcept
    {
        return reserved_;
    }

  private:
    [[nodiscard]] json::Value entry_for(const ProviderConfig &config) const;
    void keep_aside(const json::Value &entry, const std::string &id, std::string problem,
                    StoredProviders &out);

    std::string data_dir_;
    Kind kind_;
    json::Value root_ = json::Value::object(); // as read: unknown top-level fields
    std::map<std::string, json::Value> raw_;   // usable entries as read, by id
    std::vector<json::Value> kept_;            // entries not loaded, as read
    std::set<std::string> reserved_;
    bool read_only_ = false;
    std::string read_only_reason_;
};

// Keeps a damaged file's text next to it as <path>.corrupt (or .corrupt-2 ...
// when a different damaged copy is already there). Returns the name used.
std::string keep_damaged_copy(const std::string &path, const std::string &text);
} // namespace akeno::anime
