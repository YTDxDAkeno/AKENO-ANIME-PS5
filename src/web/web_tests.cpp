// AKENO STREAM PS5 - Results of the browser tests, kept across restarts.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "web/web_tests.hpp"

#include "core/fs.hpp"
#include "core/json.hpp"

#include <algorithm>
#include <cctype>

namespace akeno::web
{
namespace
{
constexpr char kFile[] = "web-tests.json";

// Report text is shown on screen and written to files: printable, bounded.
std::string clean(std::string_view text, std::size_t limit)
{
    std::string out;
    for (char c : text)
    {
        if (out.size() >= limit)
            break;
        out += static_cast<unsigned char>(c) < 0x20 || c == 0x7f ? ' ' : c;
    }
    if (out.size() >= limit)
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80)
            out.pop_back();
    return out;
}

bool valid_id(std::string_view id)
{
    return !id.empty() && id.size() <= 48 &&
           std::all_of(id.begin(), id.end(),
                       [](char c) {
                           return std::isalnum(static_cast<unsigned char>(c)) || c == '.' ||
                                  c == '_' || c == '-';
                       });
}
} // namespace

const char *outcome_id(Outcome outcome) noexcept
{
    switch (outcome)
    {
    case Outcome::yes:
        return "yes";
    case Outcome::partial:
        return "partial";
    case Outcome::no:
        return "no";
    case Outcome::info:
        return "info";
    case Outcome::unknown:
        break;
    }
    return "unknown";
}

const char *outcome_label(Outcome outcome) noexcept
{
    switch (outcome)
    {
    case Outcome::yes:
        return "Yes";
    case Outcome::partial:
        return "Partly";
    case Outcome::no:
        return "No";
    case Outcome::info:
        return "Info";
    case Outcome::unknown:
        break;
    }
    return "Unknown";
}

Outcome outcome_from_id(std::string_view id) noexcept
{
    for (Outcome o : {Outcome::yes, Outcome::partial, Outcome::no, Outcome::info})
        if (id == outcome_id(o))
            return o;
    return Outcome::unknown;
}

WebTestLog::WebTestLog(std::string directory) : directory_{std::move(directory)}
{
}

void WebTestLog::load()
{
    records_.clear();
    const std::string path = fs::join(directory_, kFile);
    const auto text = fs::read_text(path, 2u * 1024u * 1024u);
    if (!text)
        return;
    const auto parsed = json::parse(*text);
    if (!parsed.ok || !parsed.value.is_object())
    {
        (void)fs::write_atomic(path + ".corrupt", *text);
        (void)fs::remove_file(path);
        return;
    }
    for (const auto &v : parsed.value["records"].items())
    {
        TestRecord r;
        r.id = v["id"].str();
        if (!valid_id(r.id) || get(r.id) || records_.size() >= kMaxRecords)
            continue;
        r.group = clean(v["group"].str(), 60);
        r.name = clean(v["name"].str(), 120);
        r.outcome = outcome_from_id(v["outcome"].str());
        r.detail = clean(v["detail"].str(), 300);
        r.at = static_cast<std::uint64_t>(std::max(0LL, v["at"].integer()));
        r.source = clean(v["source"].str(), 40);
        records_.push_back(std::move(r));
    }
}

void WebTestLog::save()
{
    json::Value root = json::Value::object();
    root.set("version", 1);
    json::Value list = json::Value::array();
    for (const TestRecord &r : records_)
    {
        json::Value v = json::Value::object();
        v.set("id", r.id);
        v.set("group", r.group);
        v.set("name", r.name);
        v.set("outcome", outcome_id(r.outcome));
        v.set("detail", r.detail);
        v.set("at", static_cast<long long>(r.at));
        v.set("source", r.source);
        list.push(v);
    }
    root.set("records", list);
    (void)fs::write_atomic(fs::join(directory_, kFile), root.dump(true));
}

void WebTestLog::set(TestRecord record)
{
    if (!valid_id(record.id))
        return;
    record.group = clean(record.group, 60);
    record.name = clean(record.name, 120);
    record.detail = clean(record.detail, 300);
    record.source = clean(record.source, 40);
    const auto it = std::find_if(records_.begin(), records_.end(),
                                 [&](const TestRecord &r) { return r.id == record.id; });
    if (it != records_.end())
        *it = std::move(record);
    else if (records_.size() < kMaxRecords)
        records_.push_back(std::move(record));
    save();
}

const TestRecord *WebTestLog::get(std::string_view id) const
{
    const auto it = std::find_if(records_.begin(), records_.end(),
                                 [&](const TestRecord &r) { return r.id == id; });
    return it == records_.end() ? nullptr : &*it;
}

std::vector<TestRecord> WebTestLog::with_prefix(std::string_view prefix) const
{
    std::vector<TestRecord> out;
    for (const TestRecord &r : records_)
        if (std::string_view{r.id}.starts_with(prefix))
            out.push_back(r);
    return out;
}

void WebTestLog::clear()
{
    records_.clear();
    save();
}

int WebTestLog::accept_report(std::string_view json_text, std::uint64_t now, std::string *error)
{
    json::ParseLimits limits;
    limits.max_bytes = 64 * 1024;
    limits.max_depth = 8;
    const auto parsed = json::parse(json_text, limits);
    if (!parsed.ok || !parsed.value.is_object())
    {
        if (error)
            *error = "the report could not be read";
        return 0;
    }
    int taken = 0;
    for (const auto &v : parsed.value["results"].items())
    {
        if (taken >= 150)
            break;
        const std::string id = v["id"].str();
        if (!valid_id(id) || id.starts_with("youtube.") || id.starts_with("crunchyroll.") ||
            id == "drm.secure_check")
            continue; // those come from the player and from the user, not this page
        TestRecord r;
        r.id = id;
        r.group = v["group"].str();
        r.name = v["name"].str();
        r.outcome = outcome_from_id(v["status"].str());
        r.detail = v["detail"].str();
        r.at = now;
        r.source = "capability test";
        if (r.group.empty() || r.name.empty())
            continue;
        records_.erase(std::remove_if(records_.begin(), records_.end(),
                                      [&](const TestRecord &x) { return x.id == id; }),
                       records_.end());
        r.group = clean(r.group, 60);
        r.name = clean(r.name, 120);
        r.detail = clean(r.detail, 300);
        if (records_.size() < kMaxRecords)
        {
            records_.push_back(std::move(r));
            ++taken;
        }
    }
    save();
    if (taken == 0 && error)
        *error = "the report contained no results";
    return taken;
}
DrmVerdict drm_verdict(const WebTestLog &log, std::string_view playback_record_id)
{
    const auto outcome = [&](std::string_view id)
    {
        const TestRecord *r = log.get(id);
        return r ? r->outcome : Outcome::unknown;
    };
    using Level = DrmVerdict::Level;
    if (!playback_record_id.empty() && outcome(playback_record_id) == Outcome::yes)
        return {Level::confirmed, "You recorded that episodes play on this console."};
    const Outcome widevine = outcome("drm.widevine"), playready = outcome("drm.playready"),
                  fairplay = outcome("drm.fairplay"), fairplay1 = outcome("drm.fairplay_1");
    // What the user read on a public DRM support page served over HTTPS (a
    // secure context, where EME is never hidden for that reason).
    const Outcome secure_check = outcome("drm.secure_check");
    if (secure_check == Outcome::yes)
        return {Level::possible,
                "The secure DRM check listed a DRM system for this browser, so playback may "
                "work - the service still decides which browsers it serves. Try an episode and "
                "record the result."};
    if (secure_check == Outcome::no)
        return {Level::unavailable,
                "Measured on a secure page: this browser offers no DRM system (Widevine, "
                "PlayReady, FairPlay) to web pages. The website and sign-in may work, but "
                "protected episodes cannot play here."};
    if (widevine == Outcome::yes || playready == Outcome::yes || fairplay == Outcome::yes ||
        fairplay1 == Outcome::yes)
        return {Level::possible,
                "The browser offers a DRM system to pages, so playback may work - the service "
                "still decides which browsers it serves. Try an episode and record the result."};
    const bool no_eme = outcome("drm.eme") == Outcome::no;
    const bool all_refused = widevine == Outcome::no && playready == Outcome::no &&
                             (fairplay == Outcome::no || fairplay1 == Outcome::no);
    if (no_eme || all_refused)
        return {Level::unavailable,
                "Measured: this browser offers no DRM system (Widevine, PlayReady, FairPlay) to "
                "web pages. The website and sign-in may work, but protected episodes cannot play "
                "here."};
    if (widevine != Outcome::unknown || playready != Outcome::unknown ||
        fairplay != Outcome::unknown || fairplay1 != Outcome::unknown)
        return {Level::partly_measured,
                "Partly measured: not every DRM system has a result yet. Run the browser test "
                "again and wait for \"Done\"."};
    return {Level::not_measured, "Not measured yet: run the browser test to see whether the "
                                 "browser offers the DRM protected episodes need."};
}
} // namespace akeno::web
