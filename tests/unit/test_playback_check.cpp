// AKENO STREAM PS5 - Tests for the per-site playback record and its states.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/fs.hpp"
#include "web/playback_check.hpp"
#include "web/web_tests.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace akeno;
using web::Outcome;
using web::PlaybackState;
using web::Seen;

namespace
{
std::string fresh_dir()
{
    char path[] = "/tmp/akeno-playback-XXXXXX";
    return mkdtemp(path) ? path : "/tmp";
}

void put(web::WebTestLog &log, const char *id, Outcome outcome)
{
    web::TestRecord r;
    r.id = id;
    r.group = "test";
    r.name = id;
    r.outcome = outcome;
    log.set(r);
}

web::PlaybackCheck seen(Seen s, std::string code = {})
{
    web::PlaybackCheck c;
    c.key = "example.com";
    c.seen = s;
    c.error_code = std::move(code);
    return c;
}

// The lab as it would look on a browser with working streaming and no DRM.
void lab_without_drm(web::WebTestLog &log)
{
    put(log, "captest.finished", Outcome::yes);
    put(log, "playback.video", Outcome::yes);
    put(log, "mse.available", Outcome::yes);
    put(log, "playback.mse", Outcome::yes);
    put(log, "drm.eme", Outcome::yes);
    put(log, "drm.widevine", Outcome::no);
    put(log, "drm.playready", Outcome::no);
    put(log, "drm.fairplay", Outcome::no);
    put(log, "drm.fairplay_1", Outcome::no);
}
} // namespace

TEST(PlaybackCheck, KeysAreHostNamesOnly)
{
    EXPECT_EQ(web::check_key("https://www.crunchyroll.com/watch/GXXX/episode?x=1"),
              "crunchyroll.com");
    EXPECT_EQ(web::check_key("crunchyroll.com"), "crunchyroll.com");
    EXPECT_EQ(web::check_key("https://HLSJS.video-dev.org/demo/"), "hlsjs.video-dev.org");
    EXPECT_EQ(web::check_key("http://example.com:8080/a"), "example.com");
    EXPECT_EQ(web::check_key(""), "");
    EXPECT_EQ(web::clean_error_code("  KAT-6005\n "), "KAT-6005");
    EXPECT_LE(web::clean_error_code(std::string(500, 'x')).size(), 60u);
}

TEST(PlaybackCheck, SeenIdsRoundTrip)
{
    for (int i = 0; i < web::kSeenCount; ++i)
    {
        const auto s = static_cast<Seen>(i);
        EXPECT_EQ(web::seen_from_id(web::seen_id(s)), s);
        EXPECT_STRNE(web::seen_label(s), "");
    }
    EXPECT_EQ(web::seen_from_id("rm -rf"), Seen::not_tested);
}

TEST(PlaybackCheck, LogPersistsAndStaysBounded)
{
    const std::string dir = fresh_dir();
    {
        web::PlaybackLog log{dir};
        log.load();
        web::PlaybackCheck c = seen(Seen::error_message, "KAT-6005");
        c.key = "crunchyroll.com";
        c.name = "Crunchyroll";
        c.at = 10;
        log.set(c);
        c.key = "bad key/with path";
        log.set(c); // refused: not a host name
        EXPECT_EQ(log.checks().size(), 1u);
    }
    web::PlaybackLog again{dir};
    again.load();
    ASSERT_NE(again.get("crunchyroll.com"), nullptr);
    EXPECT_EQ(again.get("crunchyroll.com")->seen, Seen::error_message);
    EXPECT_EQ(again.get("crunchyroll.com")->error_code, "KAT-6005");
    for (std::size_t i = 0; i < web::PlaybackLog::kMaxChecks + 5; ++i)
    {
        web::PlaybackCheck c = seen(Seen::plays);
        c.key = "site" + std::to_string(i) + ".example";
        c.at = 100 + i;
        again.set(c);
    }
    EXPECT_EQ(again.checks().size(), web::PlaybackLog::kMaxChecks);
    EXPECT_EQ(again.get("crunchyroll.com"), nullptr); // the oldest made room
    again.remove("site104.example");
    EXPECT_EQ(again.get("site104.example"), nullptr);
    // A damaged file is set aside, not trusted.
    ASSERT_TRUE(fs::write_atomic(fs::join(dir, "playback-checks.json"), "{not json"));
    web::PlaybackLog damaged{dir};
    damaged.load();
    EXPECT_TRUE(damaged.checks().empty());
}

TEST(PlaybackCheck, WithoutTheLabNothingIsClaimed)
{
    const std::string dir = fresh_dir();
    web::WebTestLog log{dir};
    const web::LabFacts lab = web::lab_facts(log);
    EXPECT_FALSE(lab.measured);
    EXPECT_EQ(web::classify(seen(Seen::not_tested), true, lab).state, PlaybackState::not_tested);
    // KAT-6005 alone does not make it a DRM problem.
    const auto cls = web::classify(seen(Seen::error_message, "KAT-6005"), true, lab);
    EXPECT_EQ(cls.state, PlaybackState::undetermined);
    EXPECT_FALSE(cls.measured);
    EXPECT_NE(cls.reason.find("KAT-6005"), std::string::npos);
    EXPECT_EQ(web::classify(seen(Seen::never_starts), false, lab).state,
              PlaybackState::undetermined);
    EXPECT_NE(web::lab_meaning(lab).find("Run the AKENO playback lab"), std::string::npos);
}

TEST(PlaybackCheck, MeasuredDrmAbsenceExplainsAProtectedSite)
{
    const std::string dir = fresh_dir();
    web::WebTestLog log{dir};
    lab_without_drm(log);
    const web::LabFacts lab = web::lab_facts(log);
    EXPECT_TRUE(lab.measured);
    EXPECT_EQ(lab.drm, Outcome::no);
    const auto cr = web::classify(seen(Seen::error_message, "KAT-6005"), true, lab);
    EXPECT_EQ(cr.state, PlaybackState::drm_unavailable);
    EXPECT_TRUE(cr.measured);
    EXPECT_NE(cr.reason.find("KAT-6005"), std::string::npos);
    // The same error on a site without DRM is not blamed on DRM.
    const auto other = web::classify(seen(Seen::error_message, "Error 2"), false, lab);
    EXPECT_EQ(other.state, PlaybackState::undetermined);
    EXPECT_NE(web::lab_meaning(lab).find("No DRM system"), std::string::npos);
    EXPECT_NE(web::lab_meaning(lab).find("Streaming players"), std::string::npos);
}

TEST(PlaybackCheck, ADrmSystemPointsElsewhere)
{
    const std::string dir = fresh_dir();
    web::WebTestLog log{dir};
    lab_without_drm(log);
    put(log, "drm.widevine", Outcome::yes);
    const auto cls =
        web::classify(seen(Seen::error_message, "KAT-6005"), true, web::lab_facts(log));
    EXPECT_EQ(cls.state, PlaybackState::undetermined);
    EXPECT_NE(cls.reason.find("another cause"), std::string::npos);
}

TEST(PlaybackCheck, TheSecureDrmCheckDecides)
{
    const std::string dir = fresh_dir();
    web::WebTestLog log{dir};
    put(log, "captest.finished", Outcome::yes);
    put(log, "drm.eme", Outcome::unknown); // the lab page was not a secure context
    EXPECT_EQ(web::lab_facts(log).drm, Outcome::unknown);
    put(log, "drm.secure_check", Outcome::no);
    EXPECT_EQ(web::lab_facts(log).drm, Outcome::no);
    EXPECT_EQ(web::drm_verdict(log, "").level, web::DrmVerdict::Level::unavailable);
    put(log, "drm.secure_check", Outcome::yes);
    EXPECT_EQ(web::drm_verdict(log, "").level, web::DrmVerdict::Level::possible);
    // The lab page cannot overwrite what the user read on the secure page.
    const int taken = log.accept_report(
        R"({"results":[{"id":"drm.secure_check","group":"g","name":"n","status":"no"}]})", 1,
        nullptr);
    EXPECT_EQ(taken, 0);
    EXPECT_EQ(log.get("drm.secure_check")->outcome, Outcome::yes);
}

TEST(PlaybackCheck, MissingMediaSourceIsMeasured)
{
    const std::string dir = fresh_dir();
    web::WebTestLog log{dir};
    put(log, "captest.finished", Outcome::yes);
    put(log, "playback.video", Outcome::yes);
    put(log, "mse.available", Outcome::no);
    const web::LabFacts lab = web::lab_facts(log);
    for (Seen s : {Seen::no_player, Seen::never_starts, Seen::error_message, Seen::format_message})
    {
        const auto cls = web::classify(seen(s), false, lab);
        EXPECT_EQ(cls.state, PlaybackState::media_api_unsupported);
        EXPECT_TRUE(cls.measured);
    }
    put(log, "mse.available", Outcome::yes);
    put(log, "playback.mse", Outcome::no);
    EXPECT_EQ(web::classify(seen(Seen::never_starts), false, web::lab_facts(log)).state,
              PlaybackState::media_api_unsupported);
}

TEST(PlaybackCheck, WhatTheUserSawMapsToTheEightStates)
{
    const std::string dir = fresh_dir();
    web::WebTestLog log{dir};
    lab_without_drm(log);
    put(log, "drm.widevine", Outcome::yes);
    const web::LabFacts lab = web::lab_facts(log);
    EXPECT_EQ(web::classify(seen(Seen::plays), true, lab).state, PlaybackState::works);
    EXPECT_EQ(web::classify(seen(Seen::page_failed), false, lab).state, PlaybackState::page_failed);
    EXPECT_EQ(web::classify(seen(Seen::no_player), false, lab).state,
              PlaybackState::player_init_failed);
    EXPECT_EQ(web::classify(seen(Seen::never_starts), false, lab).state,
              PlaybackState::no_compatible_resource);
    EXPECT_EQ(web::classify(seen(Seen::format_message), false, lab).state,
              PlaybackState::codec_unsupported);
    EXPECT_EQ(web::classify(seen(Seen::embed_message), false, lab).state,
              PlaybackState::embedding_denied);
    for (int i = 0; i <= static_cast<int>(PlaybackState::undetermined); ++i)
        EXPECT_STRNE(web::state_label(static_cast<PlaybackState>(i)), "");
}

// The Clear Key clip separates "the browser cannot decrypt at all" from "the
// service refuses this browser's DRM"; an unanswered Clear Key decides nothing.
TEST(PlaybackCheck, ClearKeyPlaybackIsEvidence)
{
    {
        web::WebTestLog log(fresh_dir());
        put(log, "captest.finished", Outcome::yes);
        put(log, "playback.video", Outcome::yes);
        put(log, "mse.available", Outcome::yes);
        put(log, "playback.mse", Outcome::yes);
        put(log, "drm.eme", Outcome::yes);
        put(log, "playback.clearkey", Outcome::no);
        const web::LabFacts lab = web::lab_facts(log);
        EXPECT_EQ(lab.encrypted_playback, Outcome::no);
        EXPECT_EQ(lab.drm, Outcome::unknown); // no key system measured yet
        const auto cr = web::classify(seen(Seen::error_message, "KAT-6005"), true, lab);
        EXPECT_EQ(cr.state, PlaybackState::drm_unavailable);
        EXPECT_TRUE(cr.measured);
        EXPECT_NE(cr.reason.find("Clear Key"), std::string::npos);
        EXPECT_NE(cr.reason.find("KAT-6005"), std::string::npos);
        // A site without DRM is not judged by it.
        EXPECT_EQ(web::classify(seen(Seen::error_message), false, lab).state,
                  PlaybackState::undetermined);
        EXPECT_NE(web::lab_meaning(lab).find("Clear Key"), std::string::npos);
    }
    {
        // Widevine on offer: a failed Clear Key clip alone proves nothing.
        web::WebTestLog log(fresh_dir());
        put(log, "captest.finished", Outcome::yes);
        put(log, "mse.available", Outcome::yes);
        put(log, "playback.mse", Outcome::yes);
        put(log, "drm.eme", Outcome::yes);
        put(log, "drm.widevine", Outcome::yes);
        put(log, "playback.clearkey", Outcome::no);
        const auto cls =
            web::classify(seen(Seen::error_message, "KAT-6005"), true, web::lab_facts(log));
        EXPECT_EQ(cls.state, PlaybackState::undetermined);
    }
    {
        // Decryption works and a DRM system is offered: the error lies elsewhere.
        web::WebTestLog log(fresh_dir());
        put(log, "captest.finished", Outcome::yes);
        put(log, "mse.available", Outcome::yes);
        put(log, "playback.mse", Outcome::yes);
        put(log, "drm.eme", Outcome::yes);
        put(log, "drm.widevine", Outcome::yes);
        put(log, "playback.clearkey", Outcome::yes);
        const web::LabFacts lab = web::lab_facts(log);
        const auto cls = web::classify(seen(Seen::error_message, "KAT-6005"), true, lab);
        EXPECT_EQ(cls.state, PlaybackState::undetermined);
        EXPECT_FALSE(cls.measured);
        EXPECT_NE(cls.reason.find("plays encrypted video"), std::string::npos);
        EXPECT_NE(web::lab_meaning(lab).find("can decrypt"), std::string::npos);
    }
    {
        // Clear Key not offered: 'unknown', nothing is concluded from it.
        web::WebTestLog log(fresh_dir());
        put(log, "captest.finished", Outcome::yes);
        put(log, "mse.available", Outcome::yes);
        put(log, "playback.mse", Outcome::yes);
        put(log, "drm.eme", Outcome::yes);
        put(log, "playback.clearkey", Outcome::unknown);
        const auto cls =
            web::classify(seen(Seen::error_message, "KAT-6005"), true, web::lab_facts(log));
        EXPECT_EQ(cls.state, PlaybackState::undetermined);
        EXPECT_FALSE(cls.measured);
    }
}
