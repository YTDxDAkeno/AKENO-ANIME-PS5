// AKENO STREAM PS5 - The files that keep the providers the user added, safe across versions.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "anime/provider_store.hpp"

#include "core/fs.hpp"
#include "core/url.hpp"

#include <algorithm>

namespace akeno::anime
{
namespace
{
constexpr std::size_t kMaxFileBytes = 1024u * 1024u;

bool valid_website_id(const std::string &id)
{
    return !id.empty() && id.size() <= 80 &&
           std::all_of(id.begin(), id.end(),
                       [](char c) {
                           return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                                  c == '_';
                       });
}

bool http_address(const std::string &address)
{
    const auto u = url::parse(address);
    return u && u->is_http() && !u->host.empty();
}

// A copy of an object without some of its members.
json::Value without(const json::Value &object, std::initializer_list<std::string_view> keys)
{
    json::Value out = json::Value::object();
    for (const auto &[key, value] : object.members())
        if (std::find(keys.begin(), keys.end(), key) == keys.end())
            out.set(key, value);
    return out;
}

std::string label_of(const json::Value &entry)
{
    const std::string &name = entry["name"].str();
    if (!name.empty())
        return "\"" + name + "\"";
    const std::string &id = entry["id"].str();
    return id.empty() ? std::string{"An entry"} : "\"" + id + "\"";
}

// The document's shape; false when it is damaged.
struct Document
{
    json::Value root;
    int version = 1;
    int min_reader = 1;
};

bool read_document(const std::string &text, ProviderFile::Kind kind, Document *out)
{
    json::ParseLimits limits;
    limits.max_bytes = kMaxFileBytes;
    limits.max_depth = 16;
    auto parsed = json::parse(text, limits);
    if (!parsed.ok || !parsed.value.is_object() || !parsed.value["providers"].is_array())
        return false;
    const json::Value &root = parsed.value;
    if (kind == ProviderFile::Kind::websites && root.has("format") &&
        root["format"].str() != kWebsiteProvidersFormat)
        return false;
    const json::Value &version = root["version"];
    if (root.has("version") && (!version.is_number() || version.num() < 1))
        return false;
    out->version = static_cast<int>(version.integer(1));
    out->min_reader = static_cast<int>(root["minReaderVersion"].integer(out->version));
    out->root = parsed.value;
    return true;
}
} // namespace

std::string keep_damaged_copy(const std::string &path, const std::string &text)
{
    for (int n = 1; n <= 9; ++n)
    {
        const std::string copy = path + ".corrupt" + (n == 1 ? "" : "-" + std::to_string(n));
        const auto existing = fs::read_text(copy, kMaxFileBytes * 2);
        if (existing && *existing == text)
            return fs::file_name(copy); // already kept
        if (!existing)
        {
            (void)fs::write_atomic(copy, text);
            return fs::file_name(copy);
        }
    }
    return fs::file_name(path) + ".corrupt";
}

ProviderFile::ProviderFile(std::string data_dir, Kind kind)
    : data_dir_{std::move(data_dir)}, kind_{kind}
{
}

std::string ProviderFile::path() const
{
    return fs::join(data_dir_, kind_ == Kind::catalogs ? kProvidersFile : kWebsiteProvidersFile);
}

void ProviderFile::keep_aside(const json::Value &entry, const std::string &id, std::string problem,
                              StoredProviders &out)
{
    kept_.push_back(entry);
    if (!id.empty())
        reserved_.insert(id);
    ++out.kept_aside;
    out.problems.push_back(std::move(problem));
}

StoredProviders ProviderFile::load(const std::set<std::string> &taken, std::size_t max_entries,
                                   const AdapterVersion &adapter_version)
{
    StoredProviders out;
    root_ = json::Value::object();
    raw_.clear();
    kept_.clear();
    reserved_.clear();
    read_only_ = false;
    read_only_reason_.clear();
    const std::string file = path();
    const std::string name = fs::file_name(file);
    const auto text = fs::read_text(file, kMaxFileBytes);
    if (!text)
        return out; // no providers added yet
    Document doc;
    if (!read_document(*text, kind_, &doc))
    {
        const std::string kept = keep_damaged_copy(file, *text);
        const auto backup = fs::read_text(file + ".bak", kMaxFileBytes);
        if (backup && read_document(*backup, kind_, &doc))
        {
            out.recovered = true;
            out.problems.push_back(name + " was damaged (kept as " + kept +
                                   "); the previous copy was restored.");
        }
        else
        {
            out.problems.push_back(name + " is damaged; kept as " + kept + ".");
            return out;
        }
    }
    root_ = doc.root;
    const int supported =
        kind_ == Kind::catalogs ? kProvidersFileVersion : kWebsiteProvidersFileVersion;
    if (doc.version > supported)
    {
        read_only_ = true;
        read_only_reason_ = name + " was written by a newer AKENO STREAM (format " +
                            std::to_string(doc.version) +
                            "). This version leaves it unchanged: update AKENO STREAM to change "
                            "these providers.";
        out.read_only = true;
        out.problems.push_back(read_only_reason_);
        if (doc.min_reader > supported)
            return out; // nothing in it is meant for this version
    }

    std::set<std::string> ids = taken;
    for (const json::Value &entry : root_["providers"].items())
    {
        const std::string &id = entry["id"].str();
        if (!entry.is_object())
        {
            keep_aside(entry, {}, "An entry in " + name + " is not a provider; kept unchanged.",
                       out);
            continue;
        }
        ProviderConfig c;
        c.id = id;
        c.name = entry["name"].str();
        c.added = static_cast<std::uint64_t>(std::max(0LL, entry["added"].integer()));
        std::string problem;
        if (kind_ == Kind::catalogs)
        {
            // 1.0.0 wrote every entry with a type; a missing one was a catalogue.
            const std::string &type = entry["type"].str();
            const auto known = entry.has("type")
                                   ? provider_type_from_id(type)
                                   : std::optional<ProviderType>{ProviderType::catalog};
            c.address = entry["address"].str();
            c.anilist_id = static_cast<int>(entry["anilistId"].integer());
            if (!known)
                problem = label_of(entry) + " has the provider type \"" + type +
                          "\", which this version does not know; kept unchanged.";
            else if (*known == ProviderType::bundled || *known == ProviderType::website)
                problem = label_of(entry) + " in " + name + " has the type \"" + type +
                          "\", which does not belong in this file; kept unchanged.";
            else if (id.empty() || c.address.empty())
                problem = label_of(entry) + " has no id or address; kept unchanged.";
            else if (!http_address(c.address))
                problem = label_of(entry) + " is not a web address; kept unchanged.";
            if (known)
                c.type = *known;
        }
        else
        {
            c.type = ProviderType::website;
            c.original_url = entry["url"].str();
            c.address = entry["homepage"].str(c.original_url);
            c.adapter = entry["adapter"].str();
            c.adapter_version = static_cast<int>(entry["adapterVersion"].integer(1));
            c.enabled = entry["enabled"].boolean(true);
            for (const auto &[key, value] : entry["settings"].members())
                if (value.is_string())
                    c.settings[key] = value.str();
            const json::Value &summary = entry["catalogue"];
            c.known_series = static_cast<int>(summary["series"].integer());
            c.known_episodes = static_cast<int>(summary["episodes"].integer());
            c.checked = static_cast<std::uint64_t>(std::max(0LL, summary["checked"].integer()));
            const int supported_adapter = adapter_version ? adapter_version(c.adapter) : 0;
            if (!valid_website_id(id))
                problem = label_of(entry) + " has no valid id; kept unchanged.";
            else if (!http_address(c.original_url) || !http_address(c.address))
                problem = label_of(entry) + " has no valid web address; kept unchanged.";
            else if (c.adapter.empty())
                problem = label_of(entry) + " names no website adapter; kept unchanged.";
            else if (entry.has("enabled") && !entry["enabled"].is_bool())
                problem = label_of(entry) + " has a malformed \"enabled\" field; kept unchanged.";
            else if (entry.has("settings") && !entry["settings"].is_object())
                problem = label_of(entry) + " has malformed settings; kept unchanged.";
            else if (supported_adapter == 0)
                problem = label_of(entry) + " uses the website adapter \"" + c.adapter +
                          "\", which this version does not have; kept unchanged.";
            else if (c.adapter_version > supported_adapter)
                problem = label_of(entry) + " was saved by a newer version of the \"" + c.adapter +
                          "\" adapter; kept unchanged.";
        }
        if (problem.empty() && ids.count(id))
            problem = label_of(entry) + " repeats the id \"" + id + "\"; kept unchanged.";
        if (problem.empty() && out.providers.size() >= max_entries)
            problem = label_of(entry) + " is over the limit of " + std::to_string(max_entries) +
                      " providers; kept unchanged.";
        if (!problem.empty())
        {
            keep_aside(entry, id, std::move(problem), out);
            continue;
        }
        if (c.name.empty())
            if (const auto u = url::parse(c.address))
                c.name = u->host;
        ids.insert(c.id);
        raw_[c.id] = entry;
        out.providers.push_back(std::move(c));
    }
    return out;
}

json::Value ProviderFile::entry_for(const ProviderConfig &c) const
{
    const auto it = raw_.find(c.id);
    const json::Value before = it == raw_.end() ? json::Value::object() : it->second;
    if (kind_ == Kind::catalogs)
    {
        // The fields 1.0.0 writes, so 1.0.0 reads the file as it always did.
        json::Value p = without(before, {"id", "name", "type", "address", "added", "anilistId"});
        p.set("id", c.id);
        p.set("name", c.name);
        p.set("type", provider_type_id(c.type));
        p.set("address", c.address);
        p.set("added", static_cast<long long>(c.added));
        if (c.anilist_id)
            p.set("anilistId", c.anilist_id);
        return p;
    }
    json::Value p = without(before, {"id", "name", "url", "homepage", "adapter", "adapterVersion",
                                     "enabled", "added", "settings", "catalogue"});
    p.set("id", c.id);
    p.set("name", c.name);
    p.set("url", c.original_url.empty() ? c.address : c.original_url);
    p.set("homepage", c.address);
    p.set("adapter", c.adapter);
    p.set("adapterVersion", std::max(1, c.adapter_version));
    p.set("enabled", c.enabled);
    p.set("added", static_cast<long long>(c.added));
    // Settings this version does not read (not text) stay as they were.
    json::Value settings = json::Value::object();
    for (const auto &[key, value] : before["settings"].members())
        if (!value.is_string())
            settings.set(key, value);
    for (const auto &[key, value] : c.settings)
        settings.set(key, value);
    p.set("settings", std::move(settings));
    if (c.known_series || c.known_episodes || c.checked)
    {
        json::Value summary = without(before["catalogue"], {"series", "episodes", "checked"});
        summary.set("series", c.known_series);
        summary.set("episodes", c.known_episodes);
        summary.set("checked", static_cast<long long>(c.checked));
        p.set("catalogue", std::move(summary));
    }
    return p;
}

bool ProviderFile::save(const std::vector<ProviderConfig> &providers, std::string *error)
{
    const std::string file = path();
    if (read_only_)
    {
        if (error)
            *error = read_only_reason_;
        return false;
    }
    json::Value list = json::Value::array();
    std::map<std::string, json::Value> written;
    for (const ProviderConfig &c : providers)
    {
        json::Value entry = entry_for(c);
        written[c.id] = entry;
        list.push(std::move(entry));
    }
    for (const json::Value &entry : kept_)
        list.push(entry);
    json::Value root = without(root_, {"format", "version", "minReaderVersion", "providers"});
    if (kind_ == Kind::websites)
    {
        root.set("format", kWebsiteProvidersFormat);
        root.set("version", kWebsiteProvidersFileVersion);
        root.set("minReaderVersion", 1);
    }
    else
        root.set("version", kProvidersFileVersion);
    root.set("providers", std::move(list));
    // The file as it is now becomes the previous good copy.
    if (const auto current = fs::read_text(file, kMaxFileBytes))
    {
        Document doc;
        if (read_document(*current, kind_, &doc))
            (void)fs::write_atomic(file + ".bak", *current);
    }
    std::string why;
    if (!fs::write_atomic(file, root.dump(true), &why))
    {
        if (error)
            *error = "Could not save " + fs::file_name(file) + ": " + why;
        return false;
    }
    root_ = root;
    raw_ = std::move(written);
    return true;
}
} // namespace akeno::anime
