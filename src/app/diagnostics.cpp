// AKENO STREAM PS5 - Diagnostics: self-tests, error log and report export.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/diagnostics.hpp"

#include "core/fs.hpp"
#include "core/url.hpp"
#include "media/hls.hpp"
#include "net/http.hpp"

#include <cctype>
#include <cstdio>
#include <ctime>

namespace akeno
{
namespace
{
const char *state_text(TestResult::State s)
{
    switch (s)
    {
    case TestResult::State::not_run:
        return "not run";
    case TestResult::State::running:
        return "running";
    case TestResult::State::passed:
        return "PASSED";
    case TestResult::State::failed:
        return "FAILED";
    }
    return "?";
}

void append_test(std::string &out, const char *name, const TestResult &t)
{
    out += "  ";
    out += name;
    out += ": ";
    out += state_text(t.state);
    if (!t.summary.empty())
        out += " - " + t.summary;
    out += '\n';
    for (const auto &line : t.details)
        out += "      " + line + "\n";
}
} // namespace

std::string redact_secrets(const std::string &text)
{
    // YouTube/Google API keys: "AIza" followed by 35 key characters.
    std::string out = text;
    for (std::size_t pos = out.find("AIza"); pos != std::string::npos;
         pos = out.find("AIza", pos + 4))
    {
        std::size_t end = pos + 4;
        while (end < out.size() && (std::isalnum(static_cast<unsigned char>(out[end])) ||
                                    out[end] == '-' || out[end] == '_'))
            ++end;
        if (end - pos >= 30)
            out.replace(pos, end - pos, "[REDACTED-KEY]");
    }
    // URLs carrying credential query parameters.
    std::size_t start = 0;
    while ((start = out.find("http", start)) != std::string::npos)
    {
        std::size_t end = start;
        while (end < out.size() && !std::isspace(static_cast<unsigned char>(out[end])) &&
               out[end] != '"')
            ++end;
        const std::string redacted = url::redact(out.substr(start, end - start));
        out.replace(start, end - start, redacted);
        start += redacted.size();
    }
    return out;
}

void Diagnostics::record_error(std::uint64_t now_ms, std::string where, std::string message)
{
    errors_.push_front({now_ms, std::move(where), redact_secrets(message)});
    if (errors_.size() > 30)
        errors_.pop_back();
}

TestResult Diagnostics::run_network_test()
{
    TestResult result;
    net::Client client;
    net::Request https;
    https.url = "https://example.com/";
    https.max_bytes = 256 * 1024;
    const net::Response r1 = client.perform(https);
    result.details.push_back("HTTPS example.com: " + r1.describe());
    net::Request hls;
    hls.url = "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8";
    hls.max_bytes = 256 * 1024;
    const net::Response r2 = client.perform(hls);
    std::string hls_line = "HLS master (mux.dev): " + r2.describe();
    int variants = 0;
    if (r2.ok())
    {
        const auto parsed = hls::parse(r2.body, hls.url);
        variants = parsed.ok ? static_cast<int>(parsed.playlist.variants.size()) : 0;
        hls_line += parsed.ok ? ", " + std::to_string(variants) + " variants"
                              : ", parse error: " + parsed.error;
    }
    result.details.push_back(hls_line);
    result.details.push_back("TLS verification: on (console certificate list)");
    result.details.push_back("libcurl: " + net::Client::library_version());
    if (r1.ok() && r2.ok() && variants > 0)
    {
        result.state = TestResult::State::passed;
        result.summary = "HTTPS OK, HLS master OK (" + std::to_string(variants) + " variants)";
    }
    else
    {
        result.state = TestResult::State::failed;
        result.summary =
            !r1.ok() ? "HTTPS failed: " + r1.describe() : "HLS failed: " + r2.describe();
    }
    return result;
}

TestResult Diagnostics::run_media_self_test(const std::string &clip_path)
{
    TestResult result;
    const auto bytes = fs::read_bytes(clip_path, 8 * 1024 * 1024);
    if (!bytes)
    {
        result.state = TestResult::State::failed;
        result.summary = "could not read " + clip_path;
        return result;
    }
    media::ProbeOptions options;
    options.format = "mpegts";
    const media::ProbeReport report = media::probe_media(bytes->data(), bytes->size(), options);
    result.state = report.success ? TestResult::State::passed : TestResult::State::failed;
    result.summary = report.summary();
    result.details.push_back(report.ffmpeg_version);
    result.details.push_back("Stage reached: " + std::string{media::stage_name(report.reached)});
    char line[160];
    std::snprintf(line, sizeof(line),
                  "stream_info=%d packets=%u video_frames=%u audio_frames=%u audio=%s %d Hz %d ch",
                  report.stream_info_result, report.packets_read, report.frames_decoded,
                  report.audio_frames_decoded, report.audio_codec.c_str(), report.audio_sample_rate,
                  report.audio_channels);
    result.details.push_back(line);
    for (const auto &l : report.log)
        result.details.push_back("ffmpeg: " + l);
    return result;
}

std::string Diagnostics::build_report(const DiagnosticSnapshot &s) const
{
    std::string out;
    char line[256];
    out += "AKENO STREAM DIAGNOSTICS\n========================\n\n";
    out += "Application: " + s.app_version + "\n";
    out += "Build: " + s.build + "\n";
    out += "Firmware: " + s.firmware + "\n";
    out += "Platform: " + s.platform + "\n";
    out += "Data folder: " + s.data_dir + "\n";
    out += "Text rendering: " + s.fonts + "\n";
    out +=
        "Controller: " + std::string{s.controller_connected ? "connected" : "not connected"} + "\n";
    std::snprintf(
        line, sizeof(line), "Display: %llu frames presented, last frame %u us, copy+flip %u us\n",
        static_cast<unsigned long long>(s.frames_presented), s.last_frame_us, s.last_present_us);
    out += line;
    std::snprintf(line, sizeof(line), "Artwork cache: %zu images, %zu KiB\n", s.images_cached,
                  s.image_bytes / 1024);
    out += line;
    out += "Settings: " + s.settings_summary + "\n";
    out += "YouTube API key: " +
           std::string{s.youtube_key_present ? "configured (not shown)" : "not configured"} +
           "\n\n";

    out += "Network\n-------\n";
    out += "libcurl: " + s.curl_version + "\n";
    append_test(out, "Network test", network);
    out += "\nMedia\n-----\n";
    out += s.ffmpeg + "\n";
    append_test(out, "Software demux/decode self-test", software_decode);
    append_test(out, "Hardware playback test", hardware_playback);

    const media::PlayerStatus &p = s.player;
    out += "\nLast playback\n-------------\n";
    out += "State: " + std::string{media::state_name(p.state)} + "\n";
    if (!p.title.empty())
        out += "Title: " + p.title + "\n";
    if (!p.source_url.empty())
        out += "Source: " + p.source_url + "\n";
    out += "Container: " + p.container + ", video: " + p.video_codec + ", audio: " + p.audio_codec +
           "\n";
    std::snprintf(line, sizeof(line), "Resolution: %dx%d, variant: %s\n", p.width, p.height,
                  p.variant.c_str());
    out += line;
    std::snprintf(
        line, sizeof(line),
        "Access units: %llu video, %llu audio; frames decoded %llu, presented %llu, dropped %llu\n",
        static_cast<unsigned long long>(p.access_units),
        static_cast<unsigned long long>(p.audio_frames),
        static_cast<unsigned long long>(p.frames_decoded),
        static_cast<unsigned long long>(p.frames_presented),
        static_cast<unsigned long long>(p.frames_dropped));
    out += line;
    std::snprintf(line, sizeof(line),
                  "Decoder: %s, errors %llu, last native result %d, colour conversion %u us\n",
                  p.decoder.c_str(), static_cast<unsigned long long>(p.decoder_errors),
                  p.last_native_result, p.convert_us);
    out += line;
    std::snprintf(line, sizeof(line), "Audio: underruns %llu, output errors %llu\n",
                  static_cast<unsigned long long>(p.audio_underruns),
                  static_cast<unsigned long long>(p.audio_errors));
    out += line;
    std::snprintf(line, sizeof(line),
                  "Network: last HTTP %ld, %llu bytes, %u kbit/s, segments %d/%d, retries %d\n",
                  p.last_http_status, static_cast<unsigned long long>(p.bytes_downloaded),
                  p.throughput_kbps, p.segments_loaded, p.segments_total, p.retries);
    out += line;
    if (!p.notice.empty())
        out += "Notice: " + p.notice + "\n";
    if (!p.error.empty())
        out += "Error: " + p.error + "\n";
    if (!p.demux_error.empty())
        out += "Demuxer: " + p.demux_error + "\n";

    out += "\nBrowser (Websites, YouTube player)\n----------------------------------\n";
    if (s.browser.empty())
        out += "(no information)\n";
    for (const auto &l : s.browser)
        out += l + "\n";

    out += "\nRecent errors\n-------------\n";
    if (errors_.empty())
        out += "(none)\n";
    for (const auto &e : errors_)
    {
        std::snprintf(line, sizeof(line), "[%8.1f s] ", static_cast<double>(e.at_ms) / 1000.0);
        out += line + e.where + ": " + e.message + "\n";
    }
    out += "\nThis report contains no passwords, tokens or API keys.\n";
    return redact_secrets(out);
}

bool Diagnostics::export_report(const std::string &dir, const std::string &report,
                                std::uint64_t unix_time, std::string *path, std::string *error)
{
    char name[64];
    if (unix_time > 946684800)
    {
        const std::time_t t = static_cast<std::time_t>(unix_time);
        std::tm *utc = std::gmtime(&t);
        std::strftime(name, sizeof(name), "akeno-diagnostics-%Y%m%d-%H%M%S.txt", utc);
    }
    else
    {
        std::snprintf(name, sizeof(name), "akeno-diagnostics.txt");
    }
    const std::string target = fs::join(dir, name);
    if (!fs::write_atomic(target, report, error))
        return false;
    if (path)
        *path = target;
    return true;
}
} // namespace akeno
