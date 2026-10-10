// AKENO STREAM PS5 - Saved websites, recently visited sites and their preferences.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stored in <data>/websites.json (/download0/akeno on the console), which an
// app update does not touch. The list starts empty; every entry comes from the
// user - typed on the console or listed in websites.txt in the install folder
// ("Name = address" per line), which is imported once per address. Only the
// address, name and the user's own notes are kept: AKENO STREAM never sees the
// sites' cookies, sign-ins or passwords (they stay inside the system browser).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace akeno::web
{
enum class Mark : std::uint8_t
{
    untested,
    works,
    fails,
};
const char *mark_name(Mark mark) noexcept; // "Not tested" / "Works" / "Does not work"
const char *mark_id(Mark mark) noexcept;   // "untested" / "works" / "fails"
Mark mark_from_id(std::string_view id) noexcept;

// What the user saw on the console, recorded per site (hardware acceptance).
struct SiteChecks
{
    Mark loads = Mark::untested;  // the page renders
    Mark signin = Mark::untested; // the site's own sign-in works
    Mark video = Mark::untested;  // a video plays
    Mark sound = Mark::untested;  // with sound
};

struct Website
{
    std::string id; // stable, assigned by the store
    std::string name;
    std::string url;      // start address (http or https)
    std::string icon_url; // favicon or touch icon found on the site ("" until found)
    std::uint64_t added = 0;
    std::uint64_t last_visit = 0;
    int visits = 0;
    bool pinned = false;       // also shown on Home
    bool private_site = false; // visits stay out of Recently Visited; hidden from Home
    std::string last_result;   // what happened the last time it was opened
    SiteChecks checks;
};

struct RecentVisit
{
    std::string url;
    std::string title;
    std::uint64_t at = 0;
};

class WebsiteStore final
{
  public:
    static constexpr std::size_t kMaxSites = 200;
    static constexpr std::size_t kMaxRecent = 30;
    static constexpr std::size_t kMaxName = 60;

    explicit WebsiteStore(std::string directory);

    // Reads websites.json; a damaged file is kept as websites.json.corrupt.
    void load();
    // Adds the entries of websites.txt (install folder) that were never
    // imported before; returns how many were added.
    int import_text(std::string_view text, std::string *problem);

    [[nodiscard]] const std::vector<Website> &sites() const noexcept
    {
        return sites_;
    }
    [[nodiscard]] const Website *find(std::string_view id) const;
    [[nodiscard]] const Website *find_by_url(std::string_view url) const;
    // False with a reason when the address is invalid, listed already or the
    // list is full. On success *added_id names the new entry.
    bool add(std::string name, std::string url, std::string *why, std::string *added_id = nullptr);
    // Replaces an entry (matched by id). False when the new address is
    // invalid or belongs to another entry.
    bool update(const Website &site, std::string *why = nullptr);
    bool remove(std::string_view id);
    void set_icon(std::string_view id, std::string icon_url);
    void set_result(std::string_view id, std::string result);

    // Counts a visit to a saved site (matched by address) and remembers the
    // address in Recently Visited unless that site is private.
    void record_visit(const std::string &url, const std::string &title, std::uint64_t now);
    [[nodiscard]] const std::vector<RecentVisit> &recent() const noexcept
    {
        return recent_;
    }
    void remove_recent(std::string_view url);
    void clear_recent();

    // "Name = address" lines, the format websites.txt uses.
    [[nodiscard]] std::string export_text() const;
    [[nodiscard]] const std::string &last_error() const noexcept
    {
        return last_error_;
    }

  private:
    void save();
    std::string next_id();

    std::string directory_;
    std::vector<Website> sites_;
    std::vector<RecentVisit> recent_;
    std::vector<std::string> imported_; // addresses taken from websites.txt once
    std::uint64_t counter_ = 0;
    std::string last_error_;
};

// Parses websites.txt: "Name = address", "address" alone, "#" comments.
struct TextEntry
{
    std::string name;
    std::string url;
};
std::vector<TextEntry> parse_website_text(std::string_view text, std::string *problem);
} // namespace akeno::web
