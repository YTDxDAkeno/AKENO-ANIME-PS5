// AKENO STREAM PS5 - Saved websites, recently visited sites and their preferences.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "web/websites.hpp"

#include "core/fs.hpp"
#include "core/json.hpp"
#include "web/address.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace akeno::web
{
namespace
{
constexpr char kFile[] = "websites.json";
constexpr std::size_t kMaxImported = 400;

std::string trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return std::string{text};
}

// Names are shown on cards and written back to websites.txt: one line, no
// control characters, bounded length (at a UTF-8 character boundary).
std::string clean_name(std::string_view name)
{
    std::string out;
    for (char c : trim(name))
        out += static_cast<unsigned char>(c) < 0x20 || c == 0x7f ? ' ' : c;
    if (out.size() > WebsiteStore::kMaxName)
    {
        std::size_t cut = WebsiteStore::kMaxName;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80)
            --cut;
        out.resize(cut);
    }
    return trim(out);
}

json::Value checks_json(const SiteChecks &c)
{
    json::Value v = json::Value::object();
    v.set("loads", mark_id(c.loads));
    v.set("signin", mark_id(c.signin));
    v.set("video", mark_id(c.video));
    v.set("sound", mark_id(c.sound));
    return v;
}

SiteChecks checks_from(const json::Value &v)
{
    SiteChecks c;
    c.loads = mark_from_id(v["loads"].str());
    c.signin = mark_from_id(v["signin"].str());
    c.video = mark_from_id(v["video"].str());
    c.sound = mark_from_id(v["sound"].str());
    return c;
}
} // namespace

const char *mark_name(Mark mark) noexcept
{
    switch (mark)
    {
    case Mark::works:
        return "Works";
    case Mark::fails:
        return "Does not work";
    case Mark::untested:
        break;
    }
    return "Not tested";
}

const char *mark_id(Mark mark) noexcept
{
    switch (mark)
    {
    case Mark::works:
        return "works";
    case Mark::fails:
        return "fails";
    case Mark::untested:
        break;
    }
    return "untested";
}

Mark mark_from_id(std::string_view id) noexcept
{
    if (id == "works")
        return Mark::works;
    if (id == "fails")
        return Mark::fails;
    return Mark::untested;
}

std::vector<TextEntry> parse_website_text(std::string_view text, std::string *problem)
{
    std::vector<TextEntry> out;
    if (text.starts_with("\xEF\xBB\xBF"))
        text.remove_prefix(3);
    int line_number = 0;
    while (!text.empty() && out.size() < WebsiteStore::kMaxSites)
    {
        const std::size_t end = text.find('\n');
        const std::string line = trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        ++line_number;
        if (line.empty() || line[0] == '#')
            continue;
        // "Name = address"; an address alone is named after its host. The
        // separator is the last " = " so names may contain '='.
        std::string name, address = line;
        if (const std::size_t eq = line.rfind(" = "); eq != std::string::npos)
        {
            name = trim(line.substr(0, eq));
            address = trim(line.substr(eq + 3));
        }
        const Destination d = check_address(address);
        if (!d.ok)
        {
            if (problem && problem->empty())
                *problem = "websites.txt line " + std::to_string(line_number) + ": " + d.error;
            continue;
        }
        name = clean_name(name);
        out.push_back({name.empty() ? display_host(d.url) : name, d.url});
    }
    return out;
}

WebsiteStore::WebsiteStore(std::string directory) : directory_{std::move(directory)}
{
}

std::string WebsiteStore::next_id()
{
    for (;;)
    {
        char id[24];
        std::snprintf(id, sizeof(id), "s%llu", static_cast<unsigned long long>(++counter_));
        if (!find(id))
            return id;
    }
}

void WebsiteStore::load()
{
    sites_.clear();
    recent_.clear();
    imported_.clear();
    const std::string path = fs::join(directory_, kFile);
    const auto text = fs::read_text(path, 4u * 1024u * 1024u);
    if (!text)
        return;
    const auto parsed = json::parse(*text);
    if (!parsed.ok || !parsed.value.is_object())
    {
        (void)fs::write_atomic(path + ".corrupt", *text);
        (void)fs::remove_file(path);
        last_error_ = std::string{kFile} + " was damaged and has been reset";
        return;
    }
    const json::Value &root = parsed.value;
    counter_ = static_cast<std::uint64_t>(std::max(0LL, root["counter"].integer()));
    for (const auto &v : root["sites"].items())
    {
        const Destination d = check_address(v["url"].str());
        if (!d.ok || sites_.size() >= kMaxSites || find_by_url(d.url))
            continue;
        Website w;
        w.id = v["id"].str();
        if (w.id.empty() || find(w.id))
            w.id = next_id();
        w.url = d.url;
        w.name = clean_name(v["name"].str());
        if (w.name.empty())
            w.name = display_host(w.url);
        const Destination icon = check_address(v["icon"].str());
        w.icon_url = icon.ok ? icon.url : std::string{};
        w.added = static_cast<std::uint64_t>(std::max(0LL, v["added"].integer()));
        w.last_visit = static_cast<std::uint64_t>(std::max(0LL, v["last_visit"].integer()));
        w.visits = static_cast<int>(std::clamp<long long>(v["visits"].integer(), 0, 1000000));
        w.pinned = v["pinned"].boolean(false);
        w.private_site = v["private"].boolean(false);
        w.last_result = v["last_result"].str().substr(0, 200);
        w.checks = checks_from(v["checks"]);
        sites_.push_back(std::move(w));
    }
    for (const auto &v : root["recent"].items())
    {
        const Destination d = check_address(v["url"].str());
        if (!d.ok || recent_.size() >= kMaxRecent)
            continue;
        recent_.push_back({d.url, clean_name(v["title"].str()),
                           static_cast<std::uint64_t>(std::max(0LL, v["at"].integer()))});
    }
    for (const auto &v : root["imported"].items())
        if (v.is_string() && imported_.size() < kMaxImported)
            imported_.push_back(v.str());
}

void WebsiteStore::save()
{
    json::Value root = json::Value::object();
    root.set("version", 1);
    root.set("counter", static_cast<long long>(counter_));
    json::Value sites = json::Value::array();
    for (const Website &w : sites_)
    {
        json::Value v = json::Value::object();
        v.set("id", w.id);
        v.set("name", w.name);
        v.set("url", w.url);
        if (!w.icon_url.empty())
            v.set("icon", w.icon_url);
        v.set("added", static_cast<long long>(w.added));
        v.set("last_visit", static_cast<long long>(w.last_visit));
        v.set("visits", w.visits);
        v.set("pinned", w.pinned);
        v.set("private", w.private_site);
        if (!w.last_result.empty())
            v.set("last_result", w.last_result);
        v.set("checks", checks_json(w.checks));
        sites.push(v);
    }
    root.set("sites", sites);
    json::Value recent = json::Value::array();
    for (const RecentVisit &r : recent_)
    {
        json::Value v = json::Value::object();
        v.set("url", r.url);
        v.set("title", r.title);
        v.set("at", static_cast<long long>(r.at));
        recent.push(v);
    }
    root.set("recent", recent);
    json::Value imported = json::Value::array();
    for (const std::string &u : imported_)
        imported.push(u);
    root.set("imported", imported);
    std::string error;
    if (!fs::write_atomic(fs::join(directory_, kFile), root.dump(true), &error))
        last_error_ = std::string{"could not save "} + kFile + ": " + error;
}

int WebsiteStore::import_text(std::string_view text, std::string *problem)
{
    int added = 0;
    for (const TextEntry &e : parse_website_text(text, problem))
    {
        if (std::find(imported_.begin(), imported_.end(), e.url) != imported_.end())
            continue; // imported before: the user may have edited or removed it since
        if (imported_.size() < kMaxImported)
            imported_.push_back(e.url);
        std::string why;
        if (add(e.name, e.url, &why))
            ++added;
    }
    save();
    return added;
}

const Website *WebsiteStore::find(std::string_view id) const
{
    const auto it =
        std::find_if(sites_.begin(), sites_.end(), [&](const Website &w) { return w.id == id; });
    return it == sites_.end() ? nullptr : &*it;
}

const Website *WebsiteStore::find_by_url(std::string_view url) const
{
    const auto it =
        std::find_if(sites_.begin(), sites_.end(), [&](const Website &w) { return w.url == url; });
    return it == sites_.end() ? nullptr : &*it;
}

bool WebsiteStore::add(std::string name, std::string url, std::string *why, std::string *added_id)
{
    const auto fail = [&](std::string reason)
    {
        if (why)
            *why = std::move(reason);
        return false;
    };
    const Destination d = check_address(url);
    if (!d.ok)
        return fail(d.error);
    if (find_by_url(d.url))
        return fail("This address is already in your websites.");
    if (sites_.size() >= kMaxSites)
        return fail("The list is full (" + std::to_string(kMaxSites) + " websites).");
    Website w;
    w.id = next_id();
    w.url = d.url;
    w.name = clean_name(name);
    if (w.name.empty())
        w.name = display_host(d.url);
    sites_.push_back(w);
    if (added_id)
        *added_id = w.id;
    save();
    return true;
}

bool WebsiteStore::update(const Website &site, std::string *why)
{
    const auto it = std::find_if(sites_.begin(), sites_.end(),
                                 [&](const Website &w) { return w.id == site.id; });
    if (it == sites_.end())
    {
        if (why)
            *why = "That website is no longer in the list.";
        return false;
    }
    const Destination d = check_address(site.url);
    if (!d.ok)
    {
        if (why)
            *why = d.error;
        return false;
    }
    const Website *other = find_by_url(d.url);
    if (other && other->id != site.id)
    {
        if (why)
            *why = "Another saved website already uses this address.";
        return false;
    }
    Website updated = site;
    updated.url = d.url;
    updated.name = clean_name(site.name);
    if (updated.name.empty())
        updated.name = display_host(d.url);
    if (updated.url != it->url)
        updated.icon_url.clear(); // the old site's icon
    *it = std::move(updated);
    save();
    return true;
}

bool WebsiteStore::remove(std::string_view id)
{
    const auto before = sites_.size();
    sites_.erase(
        std::remove_if(sites_.begin(), sites_.end(), [&](const Website &w) { return w.id == id; }),
        sites_.end());
    if (sites_.size() == before)
        return false;
    save();
    return true;
}

void WebsiteStore::set_icon(std::string_view id, std::string icon_url)
{
    for (Website &w : sites_)
        if (w.id == id)
        {
            const Destination d = check_address(icon_url);
            w.icon_url = d.ok ? d.url : std::string{};
            save();
            return;
        }
}

void WebsiteStore::set_result(std::string_view id, std::string result)
{
    for (Website &w : sites_)
        if (w.id == id)
        {
            w.last_result = result.substr(0, 200);
            save();
            return;
        }
}

void WebsiteStore::record_visit(const std::string &url, const std::string &title, std::uint64_t now)
{
    bool keep_private = false;
    for (Website &w : sites_)
        if (w.url == url)
        {
            ++w.visits;
            w.last_visit = now;
            keep_private = w.private_site;
        }
    if (!keep_private)
    {
        recent_.erase(std::remove_if(recent_.begin(), recent_.end(),
                                     [&](const RecentVisit &r) { return r.url == url; }),
                      recent_.end());
        recent_.insert(recent_.begin(), {url, clean_name(title), now});
        if (recent_.size() > kMaxRecent)
            recent_.resize(kMaxRecent);
    }
    save();
}

void WebsiteStore::remove_recent(std::string_view url)
{
    recent_.erase(std::remove_if(recent_.begin(), recent_.end(),
                                 [&](const RecentVisit &r) { return r.url == url; }),
                  recent_.end());
    save();
}

void WebsiteStore::clear_recent()
{
    recent_.clear();
    save();
}

std::string WebsiteStore::export_text() const
{
    std::string out = "# AKENO STREAM websites - one per line: Name = address\n";
    for (const Website &w : sites_)
        out += w.name + " = " + w.url + "\n";
    return out;
}
} // namespace akeno::web
