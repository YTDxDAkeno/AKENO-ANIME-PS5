// AKENO STREAM PS5 - Results of the browser tests, kept across restarts.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One record per capability ("drm.widevine", "youtube.playing",
// "crunchyroll.login", ...), each with its own result, so a report says what
// worked and what did not item by item. Results come from three places: the
// capability test page AKENO STREAM serves to the browser, the embedded
// YouTube player's events, and what the user records after trying a site.
// Stored in <data>/web-tests.json; contains no addresses beyond host names
// and no cookies, tokens or passwords.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace akeno::web
{
enum class Outcome : std::uint8_t
{
    unknown, // not tested, or the test could not decide
    yes,     // supported / worked
    partial, // works with limits (see detail)
    no,      // not supported / failed
    info,    // a fact, not a pass or fail (user agent, screen size)
};
const char *outcome_id(Outcome outcome) noexcept;
const char *outcome_label(Outcome outcome) noexcept;
Outcome outcome_from_id(std::string_view id) noexcept;

struct TestRecord
{
    std::string id;    // "codec.h264"
    std::string group; // "Video codecs"
    std::string name;  // "H.264 (avc1.640028)"
    Outcome outcome = Outcome::unknown;
    std::string detail; // "probably"
    std::uint64_t at = 0;
    std::string source; // "capability test", "YouTube player", "your test"
};

class WebTestLog final
{
  public:
    static constexpr std::size_t kMaxRecords = 300;

    explicit WebTestLog(std::string directory);
    void load();
    void set(TestRecord record);
    [[nodiscard]] const TestRecord *get(std::string_view id) const;
    [[nodiscard]] const std::vector<TestRecord> &records() const noexcept
    {
        return records_;
    }
    // Records whose id starts with prefix ("youtube.").
    [[nodiscard]] std::vector<TestRecord> with_prefix(std::string_view prefix) const;
    void clear();

    // Takes the report the capability test page posts. Every field is
    // checked: unknown ids are kept only in their own groups, text is cut to
    // size, and anything malformed is dropped. Returns how many were taken.
    int accept_report(std::string_view json_text, std::uint64_t now, std::string *error);

  private:
    void save();
    std::string directory_;
    std::vector<TestRecord> records_;
};

// Whether DRM-protected web video (Crunchyroll) can play, from the measured
// results only: "none" needs every key system refused (or no EME at all), and
// a site rendering or a sign-in never counts as playback.
struct DrmVerdict
{
    enum class Level : std::uint8_t
    {
        not_measured,
        partly_measured,
        unavailable, // measured: no DRM system for pages
        possible,    // a DRM system is offered; playback not yet confirmed
        confirmed,   // the user recorded that playback works
    };
    Level level = Level::not_measured;
    std::string text;
};
DrmVerdict drm_verdict(const WebTestLog &log, std::string_view playback_record_id);
} // namespace akeno::web
