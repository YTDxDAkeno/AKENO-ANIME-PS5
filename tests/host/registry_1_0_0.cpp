// AKENO STREAM PS5 - AKENO STREAM 1.0.0's provider storage, frozen for rollback tests.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from 1.0.0-rc.1 (commit 8e0d67b2a85f); see registry_1_0_0.hpp.
#include "registry_1_0_0.hpp"

#include "anime/catalog_format.hpp"
#include "core/fs.hpp"
#include "core/json.hpp"
#include "core/url.hpp"
#include "platform/platform.hpp"

#include <algorithm>

namespace akeno::test::v1_0_0
{
namespace
{
constexpr char kFile[] = "anime-providers.json";
using anime::slug;
} // namespace

const char *provider_type_id(ProviderType t) noexcept
{
    switch (t)
    {
    case ProviderType::bundled:
        return "bundled";
    case ProviderType::catalog:
        return "catalog";
    case ProviderType::peertube:
        return "peertube";
    }
    return "catalog";
}

ProviderType provider_type_from_id(std::string_view id) noexcept
{
    if (id == "bundled")
        return ProviderType::bundled;
    if (id == "peertube")
        return ProviderType::peertube;
    return ProviderType::catalog;
}

const std::vector<std::string> &backup_files()
{
    static const std::vector<std::string> kFiles = {
        "settings.json",        "history.json",       "favorites.json", "sources.json",
        "streams.json",         "websites.json",      "web-tests.json", "playback-checks.json",
        "anime-providers.json", "anime-library.json",
    };
    return kFiles;
}

void Registry100::load()
{
    configs_.clear();
    ProviderConfig sample;
    sample.id = kSampleId;
    sample.name = "AKENO Sample Series";
    sample.type = ProviderType::bundled;
    sample.address = fs::join(app_dir_, "assets/anime/sample/catalog.json");
    sample.builtin = true;
    configs_.push_back(sample);
    const std::string personal_path = fs::join(app_dir_, "anime-catalog.json");
    if (fs::exists(personal_path))
    {
        ProviderConfig personal;
        personal.id = kPersonalId;
        personal.name = "My Anime Library";
        personal.type = ProviderType::catalog;
        personal.address = personal_path;
        personal.builtin = true;
        configs_.push_back(personal);
    }
    const std::string path = fs::join(data_dir_, kFile);
    const auto text = fs::read_text(path, 1024 * 1024);
    if (!text)
        return;
    const auto parsed = json::parse(*text);
    if (!parsed.ok || !parsed.value["providers"].is_array())
    {
        last_error_ = std::string{kFile} + " is damaged; kept as " + kFile + ".corrupt";
        (void)fs::write_atomic(path + ".corrupt", *text);
        return;
    }
    for (const auto &p : parsed.value["providers"].items())
    {
        ProviderConfig c;
        c.id = p["id"].str();
        c.name = p["name"].str();
        c.type = provider_type_from_id(p["type"].str());
        c.address = p["address"].str();
        c.added = static_cast<std::uint64_t>(p["added"].integer());
        c.anilist_id = static_cast<int>(p["anilistId"].integer());
        if (c.id.empty() || c.address.empty() || c.type == ProviderType::bundled || config(c.id) ||
            configs_.size() >= kMaxProviders)
            continue;
        // Added providers come from the web only; files are the built-in ones.
        const auto u = url::parse(c.address);
        if (!u || !u->is_http())
            continue;
        configs_.push_back(std::move(c));
    }
}

void Registry100::save()
{
    json::Value list = json::Value::array();
    for (const ProviderConfig &c : configs_)
    {
        if (c.builtin)
            continue;
        json::Value p = json::Value::object();
        p.set("id", c.id);
        p.set("name", c.name);
        p.set("type", provider_type_id(c.type));
        p.set("address", c.address);
        p.set("added", static_cast<long long>(c.added));
        if (c.anilist_id)
            p.set("anilistId", c.anilist_id);
        list.push(std::move(p));
    }
    json::Value root = json::Value::object();
    root.set("version", 1);
    root.set("providers", std::move(list));
    std::string error;
    if (!fs::write_atomic(fs::join(data_dir_, kFile), root.dump(true), &error))
        last_error_ = "Could not save " + std::string{kFile} + ": " + error;
}

const ProviderConfig *Registry100::config(std::string_view id) const
{
    for (const ProviderConfig &c : configs_)
        if (c.id == id)
            return &c;
    return nullptr;
}

bool Registry100::add(ProviderConfig c, std::string *why)
{
    const auto fail = [why](std::string text)
    {
        if (why)
            *why = std::move(text);
        return false;
    };
    const auto u = url::parse(c.address);
    if (!u || !u->is_http())
        return fail("A provider needs an http(s) address.");
    if (configs_.size() >= kMaxProviders)
        return fail("At most " + std::to_string(kMaxProviders) + " providers can be added.");
    for (const ProviderConfig &x : configs_)
        if (x.address == c.address)
            return fail("This address is already a provider: " + x.name);
    if (c.name.empty())
        c.name = u->host;
    if (c.name.size() > 60)
        c.name.resize(60);
    std::string base = slug(c.name);
    if (base.empty())
        base = "provider";
    c.id = base;
    for (int n = 2; config(c.id) || c.id == kSampleId || c.id == kPersonalId; ++n)
        c.id = base + "-" + std::to_string(n);
    c.builtin = false;
    if (c.type == ProviderType::bundled)
        c.type = ProviderType::catalog;
    if (!c.added)
        c.added = platform::wall_clock_seconds();
    configs_.push_back(std::move(c));
    save();
    return true;
}

bool Registry100::remove(std::string_view id)
{
    const auto it = std::find_if(configs_.begin(), configs_.end(),
                                 [id](const ProviderConfig &c) { return c.id == id; });
    if (it == configs_.end() || it->builtin)
        return false;
    configs_.erase(it);
    // (1.0.0 also drops the provider's loaded series here: memory, not storage.)
    save();
    return true;
}
} // namespace akeno::test::v1_0_0
