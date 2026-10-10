// AKENO STREAM PS5 - Why a website's video plays or not, per site.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// AKENO STREAM cannot look inside a website shown in the system browser: the
// dialog gives the app no access to the page, its player or its errors. What
// happened is therefore recorded by the user ("the player shows error
// KAT-6005"), and AKENO STREAM puts that next to what the playback lab
// measured in the same browser (MediaSource, HLS, DRM key systems...) to name
// the most likely of eight states. A state that rests on a measurement says
// so; one that does not is labelled "likely" or "not determined" - it never
// claims more than the evidence shows.
//
// Stored in <data>/playback-checks.json, keyed by host name only (no paths,
// no queries, no cookies or account data).
#pragma once

#include "web/web_tests.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace akeno::web
{
// What the user saw when trying to play a video on a site.
enum class Seen : std::uint8_t
{
    not_tested,
    plays,          // picture and sound play
    page_failed,    // the page itself did not load
    no_player,      // the page loads, but the player never appears / stays black
    never_starts,   // the player appears and loads, but playback never begins
    error_message,  // the player shows an error message or code
    format_message, // "format / browser not supported", or picture or sound missing
    embed_message,  // "playback on other websites is disabled" / "not allowed here"
};
inline constexpr int kSeenCount = 8;
const char *seen_id(Seen seen) noexcept;
Seen seen_from_id(std::string_view id) noexcept;
const char *seen_label(Seen seen) noexcept;
const char *seen_hint(Seen seen) noexcept;

// The eight states a site's video can be in (plus "not tested" and "not
// determined" when the evidence does not decide).
enum class PlaybackState : std::uint8_t
{
    not_tested,
    works,
    page_failed,
    player_init_failed,
    no_compatible_resource,
    codec_unsupported,
    media_api_unsupported,
    drm_unavailable,
    embedding_denied,
    undetermined,
};
const char *state_label(PlaybackState state) noexcept;

struct PlaybackCheck
{
    std::string key;  // "crunchyroll.com" (host without "www.")
    std::string name; // "Crunchyroll"
    Seen seen = Seen::not_tested;
    std::string error_code; // what the player showed: "KAT-6005", "Error 150"
    std::uint64_t at = 0;
};

// The host a check is filed under: lower case, without "www." and port.
std::string check_key(std::string_view url_or_host);
// Error codes are shown on screen and written to reports: printable, short.
std::string clean_error_code(std::string_view text);

class PlaybackLog final
{
  public:
    static constexpr std::size_t kMaxChecks = 100;

    explicit PlaybackLog(std::string directory);
    void load();
    void set(PlaybackCheck check);
    void remove(std::string_view key);
    void clear();
    [[nodiscard]] const PlaybackCheck *get(std::string_view key) const;
    [[nodiscard]] const std::vector<PlaybackCheck> &checks() const noexcept
    {
        return checks_;
    }

  private:
    void save();
    std::string directory_;
    std::vector<PlaybackCheck> checks_;
};

// The lab facts that decide between the states, from the browser test log.
struct LabFacts
{
    Outcome secure_context = Outcome::unknown;
    Outcome html5 = Outcome::unknown;        // progressive MP4 played
    Outcome mse = Outcome::unknown;          // MediaSource present
    Outcome mse_playback = Outcome::unknown; // MSE actually played the lab clip
    Outcome native_hls = Outcome::unknown;   // video.src = .m3u8 played
    Outcome iframe = Outcome::unknown;       // a cross-origin frame played video
    Outcome eme = Outcome::unknown;
    Outcome drm = Outcome::unknown; // any commercial key system (yes/no/unknown)
    bool measured = false;          // the lab ran at least once
};
LabFacts lab_facts(const WebTestLog &lab);
// What the facts mean for websites, in a few plain sentences.
std::string lab_meaning(const LabFacts &facts);

struct Classification
{
    PlaybackState state = PlaybackState::not_tested;
    bool measured = false; // the state rests on a lab measurement
    std::string reason;
};
// drm_expected: the site is known to deliver its videos with DRM (Crunchyroll).
Classification classify(const PlaybackCheck &check, bool drm_expected, const LabFacts &lab);
} // namespace akeno::web
