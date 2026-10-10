// AKENO STREAM PS5 - AKENO STREAM 1.0.0's provider storage, frozen for rollback tests.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What 1.0.0-rc.1 (commit 8e0d67b2a85f) does with <data>/anime-providers.json,
// so tests can run "1.0.0" against a data folder 1.1.0 wrote and check that
// upgrading, rolling back and upgrading again keeps every provider. The
// method bodies in registry_1_0_0.cpp are copied unchanged from that commit's
// src/anime/registry.cpp (Registry::load, save, config, add, remove) and its
// provider_type_id / provider_type_from_id; only the names are 1.0.0's own
// here so they can live next to the current code, and remove() leaves out
// the line that drops loaded series from memory (no storage involved). Do
// not "fix" them: they are the released behaviour, including mapping an
// unknown type to catalog.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace akeno::test::v1_0_0
{
enum class ProviderType : std::uint8_t
{
    bundled,
    catalog,
    peertube,
};
const char *provider_type_id(ProviderType t) noexcept;
ProviderType provider_type_from_id(std::string_view id) noexcept;

struct ProviderConfig
{
    std::string id;
    std::string name;
    ProviderType type = ProviderType::catalog;
    std::string address;
    std::uint64_t added = 0;
    int anilist_id = 0;
    bool builtin = false;
};

class Registry100 final
{
  public:
    static constexpr std::size_t kMaxProviders = 30;
    static constexpr char kSampleId[] = "akeno-sample";
    static constexpr char kPersonalId[] = "personal";

    Registry100(std::string data_dir, std::string app_dir)
        : data_dir_{std::move(data_dir)}, app_dir_{std::move(app_dir)}
    {
    }
    void load();
    [[nodiscard]] const std::vector<ProviderConfig> &configs() const noexcept
    {
        return configs_;
    }
    [[nodiscard]] const ProviderConfig *config(std::string_view id) const;
    bool add(ProviderConfig config, std::string *why);
    bool remove(std::string_view id);
    [[nodiscard]] const std::string &last_error() const noexcept
    {
        return last_error_;
    }

  private:
    void save();

    std::string data_dir_;
    std::string app_dir_;
    std::vector<ProviderConfig> configs_;
    std::string last_error_;
};

// 1.0.0's backup::files(): the only data files 1.0.0 backs up and restores.
const std::vector<std::string> &backup_files();
} // namespace akeno::test::v1_0_0
