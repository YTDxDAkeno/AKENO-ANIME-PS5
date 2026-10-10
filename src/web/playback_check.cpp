// AKENO STREAM PS5 - Why a website's video plays or not, per site.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "web/playback_check.hpp"

#include "core/fs.hpp"
#include "core/json.hpp"
#include "core/url.hpp"
#include "web/address.hpp"

#include <algorithm>
#include <cctype>

namespace akeno::web
{
namespace
{
constexpr char kFile[] = "playback-checks.json";

std::string clean(std::string_view text, std::size_t limit)
{
    std::string out;
    for (char c : text)
    {
        if (out.size() >= limit)
            break;
        out += static_cast<unsigned char>(c) < 0x20 || c == 0x7f ? ' ' : c;
    }
    while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80)
        out.pop_back();
    return out;
}

bool valid_key(std::string_view key)
{
    return !key.empty() && key.size() <= 120 &&
           std::all_of(key.begin(), key.end(),
                       [](char c)
                       {
                           return std::isalnum(static_cast<unsigned char>(c)) || c == '.' ||
                                  c == '-' || c == ':' || c == '_';
                       });
}

Outcome outcome_of(const WebTestLog &log, std::string_view id)
{
    const TestRecord *r = log.get(id);
    return r ? r->outcome : Outcome::unknown;
}

std::string with_code(std::string text, const PlaybackCheck &check)
{
    if (!check.error_code.empty())
        text += " The player showed: " + check.error_code + ".";
    return text;
}
} // namespace

const char *seen_id(Seen seen) noexcept
{
    switch (seen)
    {
    case Seen::plays:
        return "plays";
    case Seen::page_failed:
        return "page-failed";
    case Seen::no_player:
        return "no-player";
    case Seen::never_starts:
        return "never-starts";
    case Seen::error_message:
        return "error-message";
    case Seen::format_message:
        return "format-message";
    case Seen::embed_message:
        return "embed-message";
    case Seen::not_tested:
        break;
    }
    return "not-tested";
}

Seen seen_from_id(std::string_view id) noexcept
{
    for (int i = 1; i < kSeenCount; ++i)
        if (id == seen_id(static_cast<Seen>(i)))
            return static_cast<Seen>(i);
    return Seen::not_tested;
}

const char *seen_label(Seen seen) noexcept
{
    switch (seen)
    {
    case Seen::plays:
        return "The video plays with picture and sound";
    case Seen::page_failed:
        return "The page does not load";
    case Seen::no_player:
        return "The page loads, but no player appears";
    case Seen::never_starts:
        return "The player loads, but the video never starts";
    case Seen::error_message:
        return "The player shows an error message or code";
    case Seen::format_message:
        return "\"Format not supported\", or no picture / no sound";
    case Seen::embed_message:
        return "\"Not allowed on this site\" / embedding disabled";
    case Seen::not_tested:
        break;
    }
    return "Not tested yet";
}

const char *seen_hint(Seen seen) noexcept
{
    switch (seen)
    {
    case Seen::plays:
        return "Let it play for a minute; try pause and full screen";
    case Seen::page_failed:
        return "Blank page, a browser error, or it never finishes loading";
    case Seen::no_player:
        return "The video area stays empty or black";
    case Seen::never_starts:
        return "Endless loading circle, or Play does nothing";
    case Seen::error_message:
        return "Enter the code it shows on the next line (for example KAT-6005)";
    case Seen::format_message:
        return "The player names the format or the browser as the problem";
    case Seen::embed_message:
        return "The video's owner or host refuses playback inside other pages";
    case Seen::not_tested:
        break;
    }
    return "";
}

const char *state_label(PlaybackState state) noexcept
{
    switch (state)
    {
    case PlaybackState::works:
        return "Works";
    case PlaybackState::page_failed:
        return "Page failed to load";
    case PlaybackState::player_init_failed:
        return "Player failed to initialize";
    case PlaybackState::no_compatible_resource:
        return "No compatible video resource";
    case PlaybackState::codec_unsupported:
        return "Codec unsupported";
    case PlaybackState::media_api_unsupported:
        return "Media API unsupported";
    case PlaybackState::drm_unavailable:
        return "DRM unavailable";
    case PlaybackState::embedding_denied:
        return "Embedding denied";
    case PlaybackState::undetermined:
        return "Not determined";
    case PlaybackState::not_tested:
        break;
    }
    return "Not tested";
}

std::string check_key(std::string_view url_or_host)
{
    std::string text{url_or_host};
    if (text.find("://") == std::string::npos)
        text = "https://" + text;
    const auto parsed = url::parse(text);
    if (!parsed || parsed->host.empty())
        return {};
    std::string host = display_host(text);
    for (char &c : host)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // The port is not part of the key: one site, one record.
    if (const auto colon = host.rfind(':'); colon != std::string::npos && host.front() != '[')
        host.erase(colon);
    return valid_key(host) ? host : std::string{};
}

std::string clean_error_code(std::string_view text)
{
    std::string out = clean(text, 60);
    while (!out.empty() && out.front() == ' ')
        out.erase(0, 1);
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

PlaybackLog::PlaybackLog(std::string directory) : directory_{std::move(directory)}
{
}

void PlaybackLog::load()
{
    checks_.clear();
    const std::string path = fs::join(directory_, kFile);
    const auto text = fs::read_text(path, 512u * 1024u);
    if (!text)
        return;
    const auto parsed = json::parse(*text);
    if (!parsed.ok || !parsed.value.is_object())
    {
        (void)fs::write_atomic(path + ".corrupt", *text);
        (void)fs::remove_file(path);
        return;
    }
    for (const auto &v : parsed.value["checks"].items())
    {
        PlaybackCheck c;
        c.key = v["key"].str();
        if (!valid_key(c.key) || get(c.key) || checks_.size() >= kMaxChecks)
            continue;
        c.name = clean(v["name"].str(), 60);
        c.seen = seen_from_id(v["seen"].str());
        c.error_code = clean_error_code(v["error"].str());
        c.at = static_cast<std::uint64_t>(std::max(0LL, v["at"].integer()));
        checks_.push_back(std::move(c));
    }
}

void PlaybackLog::save()
{
    json::Value root = json::Value::object();
    root.set("version", 1);
    json::Value list = json::Value::array();
    for (const PlaybackCheck &c : checks_)
    {
        json::Value v = json::Value::object();
        v.set("key", c.key);
        v.set("name", c.name);
        v.set("seen", seen_id(c.seen));
        v.set("error", c.error_code);
        v.set("at", static_cast<long long>(c.at));
        list.push(v);
    }
    root.set("checks", list);
    (void)fs::write_atomic(fs::join(directory_, kFile), root.dump(true));
}

void PlaybackLog::set(PlaybackCheck check)
{
    if (!valid_key(check.key))
        return;
    check.name = clean(check.name, 60);
    check.error_code = clean_error_code(check.error_code);
    const auto it = std::find_if(checks_.begin(), checks_.end(),
                                 [&](const PlaybackCheck &c) { return c.key == check.key; });
    if (it != checks_.end())
        *it = std::move(check);
    else
    {
        if (checks_.size() >= kMaxChecks)
        {
            // The oldest record makes room.
            const auto oldest = std::min_element(checks_.begin(), checks_.end(),
                                                 [](const PlaybackCheck &a, const PlaybackCheck &b)
                                                 { return a.at < b.at; });
            checks_.erase(oldest);
        }
        checks_.push_back(std::move(check));
    }
    save();
}

void PlaybackLog::remove(std::string_view key)
{
    const auto before = checks_.size();
    checks_.erase(std::remove_if(checks_.begin(), checks_.end(),
                                 [&](const PlaybackCheck &c) { return c.key == key; }),
                  checks_.end());
    if (checks_.size() != before)
        save();
}

void PlaybackLog::clear()
{
    checks_.clear();
    save();
}

const PlaybackCheck *PlaybackLog::get(std::string_view key) const
{
    const auto it = std::find_if(checks_.begin(), checks_.end(),
                                 [&](const PlaybackCheck &c) { return c.key == key; });
    return it == checks_.end() ? nullptr : &*it;
}

LabFacts lab_facts(const WebTestLog &lab)
{
    LabFacts f;
    f.secure_context = outcome_of(lab, "browser.secure");
    f.html5 = outcome_of(lab, "playback.video");
    f.mse = outcome_of(lab, "mse.available");
    f.mse_playback = outcome_of(lab, "playback.mse");
    f.native_hls = outcome_of(lab, "playback.hls_native");
    f.iframe = outcome_of(lab, "playback.iframe");
    f.eme = outcome_of(lab, "drm.eme");
    f.encrypted_playback = outcome_of(lab, "playback.clearkey");
    switch (drm_verdict(lab, "").level)
    {
    case DrmVerdict::Level::unavailable:
        f.drm = Outcome::no;
        break;
    case DrmVerdict::Level::possible:
    case DrmVerdict::Level::confirmed:
        f.drm = Outcome::yes;
        break;
    default:
        f.drm = Outcome::unknown;
        break;
    }
    f.measured = lab.get("captest.finished") != nullptr;
    return f;
}

std::string lab_meaning(const LabFacts &f)
{
    if (!f.measured)
        return "Run the AKENO playback lab first (about a minute): it measures what this "
               "browser can play.";
    std::string out;
    const auto say = [&out](const char *text)
    {
        if (!out.empty())
            out += ' ';
        out += text;
    };
    if (f.html5 == Outcome::yes)
        say("Plain video files play.");
    else if (f.html5 == Outcome::no)
        say("Even a plain video file did not play - websites' videos will not work.");
    if (f.mse_playback == Outcome::yes)
        say("Streaming players (HLS.js, DASH, Shaka) can work.");
    else if (f.mse == Outcome::no)
        say("No MediaSource: most streaming sites cannot play here.");
    else if (f.mse_playback == Outcome::no)
        say("MediaSource is present but did not play: streaming sites will likely fail.");
    if (f.iframe == Outcome::no)
        say("Video inside another site's frame did not play - many sites embed their player "
            "that way.");
    if (f.encrypted_playback == Outcome::yes)
        say("Encrypted video plays (Clear Key): the browser can decrypt video.");
    else if (f.encrypted_playback == Outcome::no)
        say("Even Clear Key-encrypted video did not play: the browser's decryption path fails.");
    if (f.drm == Outcome::no)
        say("No DRM system: DRM-protected services cannot play their videos here.");
    else if (f.drm == Outcome::yes)
        say("A DRM system is offered to pages.");
    else
        say("DRM is not measured yet - run the secure DRM check.");
    return out;
}

Classification classify(const PlaybackCheck &check, bool drm_expected, const LabFacts &lab)
{
    using S = PlaybackState;
    Classification c;
    const bool no_mse = lab.mse == Outcome::no;
    const bool mse_broken = lab.mse == Outcome::yes && lab.mse_playback == Outcome::no;
    const bool no_drm = lab.drm == Outcome::no;
    switch (check.seen)
    {
    case Seen::not_tested:
        c.state = S::not_tested;
        c.reason = "Play a video on this site, then record what you saw.";
        return c;
    case Seen::plays:
        c.state = S::works;
        c.measured = true;
        c.reason = "You recorded that the video plays with picture and sound.";
        return c;
    case Seen::page_failed:
        c.state = S::page_failed;
        c.reason = with_code("The page itself did not load in the PS5 browser - a network, "
                             "certificate or compatibility problem before any video.",
                             check);
        return c;
    case Seen::embed_message:
        c.state = S::embedding_denied;
        c.reason = with_code("The video's owner or host does not allow playback inside this page. "
                             "Nothing on the console can change that.",
                             check);
        return c;
    default:
        break;
    }
    // The video did not play: the lab results decide what is measured.
    if (no_mse || mse_broken)
    {
        c.state = S::media_api_unsupported;
        c.measured = true;
        c.reason = with_code(no_mse ? "Measured: this browser has no MediaSource, which streaming "
                                      "web players (HLS.js, DASH, Shaka) need."
                                    : "Measured: MediaSource exists but did not play the lab's "
                                      "clip, so streaming web players cannot work here.",
                             check);
        return c;
    }
    // EME present, but the lab's Clear Key clip did not play: decryption
    // itself fails here, whatever key system a service would bring.
    if (drm_expected && lab.drm != Outcome::yes && lab.eme == Outcome::yes &&
        lab.encrypted_playback == Outcome::no)
    {
        c.state = S::drm_unavailable;
        c.measured = true;
        c.reason = with_code(
            "Measured: this browser's DRM interface could not play even the lab's Clear "
            "Key-encrypted clip, and no commercial DRM system (Widevine, PlayReady, FairPlay) "
            "was confirmed. This service delivers its videos with DRM: the website and sign-in "
            "can work; protected videos cannot play in this browser.",
            check);
        return c;
    }
    if (drm_expected && no_drm)
    {
        c.state = S::drm_unavailable;
        c.measured = true;
        c.reason = with_code(
            "Measured: the browser offers no DRM system (Widevine, PlayReady, FairPlay) to "
            "pages, and this service delivers its videos with DRM. The website and sign-in can "
            "still work; protected videos cannot play in this browser.",
            check);
        return c;
    }
    switch (check.seen)
    {
    case Seen::format_message:
        c.state = S::codec_unsupported;
        c.reason = with_code(
            lab.html5 == Outcome::yes
                ? "Likely: the site's video uses a format this browser does not decode (the "
                  "lab's H.264 + AAC clip does play)."
                : "Likely: the player could not decode the video's format.",
            check);
        return c;
    case Seen::no_player:
        c.state = S::player_init_failed;
        c.reason = with_code(
            lab.measured
                ? "The site's player did not start, although the lab's streaming tests pass - "
                  "the player probably refuses this browser or needs a feature the lab does "
                  "not test."
                : "The site's player did not start. Run the playback lab to see whether the "
                  "browser lacks something the player needs.",
            check);
        return c;
    case Seen::never_starts:
        c.state = lab.measured ? S::no_compatible_resource : S::undetermined;
        c.reason = with_code(
            lab.measured
                ? "Likely: the player found no video it is allowed to play in this browser "
                  "(the lab's streaming tests pass). Video hosts often refuse unknown "
                  "browsers or playback inside other sites' frames."
                : "Not determined: run the playback lab, then record this site again.",
            check);
        return c;
    case Seen::error_message:
    default:
        break;
    }
    if (drm_expected && lab.drm == Outcome::unknown)
    {
        c.state = S::undetermined;
        c.reason = with_code("Not determined: the DRM check has no result yet. Run the playback "
                             "lab and the secure DRM check, then look here again.",
                             check);
        return c;
    }
    c.state = S::undetermined;
    c.reason = with_code(
        drm_expected
            ? (lab.encrypted_playback == Outcome::yes
                   ? "Not determined: the browser offers a DRM system and plays encrypted "
                     "video (Clear Key), so the error has another cause - the service may "
                     "refuse this browser or its DRM security level, the account or region, "
                     "or it was a temporary fault. Try another episode and another time."
                   : "Not determined: the browser offers a DRM system, so the error has "
                     "another cause - the service may refuse this browser version, the "
                     "account or region, or it was a temporary fault. Try another episode "
                     "and another time.")
            : "Not determined: the player reported an error the lab cannot explain. Try "
              "another video on the same site.",
        check);
    return c;
}
} // namespace akeno::web
