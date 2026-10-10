// AKENO STREAM PS5 - Diagnostics: self-tests, error log and report export.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "media/ffmpeg_probe.hpp"
#include "media/player.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace akeno
{
struct TestResult
{
    enum class State : std::uint8_t
    {
        not_run,
        running,
        passed,
        failed,
    };
    State state = State::not_run;
    std::string summary;              // one line for the screen
    std::vector<std::string> details; // extra lines for the report
};

struct ErrorRecord
{
    std::uint64_t at_ms = 0;
    std::string where;
    std::string message;
};

// Facts gathered by the app for the report (no secrets).
struct DiagnosticSnapshot
{
    std::string app_version;
    std::string build;
    std::string firmware;
    std::string platform;
    std::string data_dir;
    std::string fonts;
    std::string curl_version;
    std::string ffmpeg;
    std::string settings_summary;
    bool youtube_key_present = false;
    std::uint64_t frames_presented = 0;
    std::uint32_t last_frame_us = 0;
    std::uint32_t last_present_us = 0;
    std::size_t images_cached = 0;
    std::size_t image_bytes = 0;
    bool controller_connected = false;
    media::PlayerStatus player;
    // The embedded browser: engine start-up steps, saved sites' test marks
    // (host names only) and the browser test results.
    std::vector<std::string> browser;
};

class Diagnostics final
{
  public:
    void record_error(std::uint64_t now_ms, std::string where, std::string message);
    [[nodiscard]] const std::deque<ErrorRecord> &errors() const noexcept
    {
        return errors_;
    }

    TestResult network;
    TestResult hls;
    TestResult software_decode;
    TestResult hardware_playback;

    // Runs on a worker thread: HTTPS to example.com plus the public HLS master
    // that v0.2 loaded on the console.
    static TestResult run_network_test();
    // Runs on a worker thread: FFmpeg demux + software decode of the packaged clip.
    static TestResult run_media_self_test(const std::string &clip_path);

    [[nodiscard]] std::string build_report(const DiagnosticSnapshot &snapshot) const;
    // Writes the report to dir/akeno-diagnostics-<time>.txt; returns the path.
    static bool export_report(const std::string &dir, const std::string &report,
                              std::uint64_t unix_time, std::string *path, std::string *error);

  private:
    std::deque<ErrorRecord> errors_;
};

// Removes anything that looks like a credential from free text.
std::string redact_secrets(const std::string &text);
} // namespace akeno
