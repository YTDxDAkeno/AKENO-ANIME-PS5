// AKENO STREAM PS5 - The embedded web browser.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// On the console this is libSceWebBrowserDialog: the system's own WebKit
// browser, which the system draws over this app's output while the app keeps
// running (and presenting) underneath. It is a common dialog: opened by the
// app, pumped once per frame, closed by the user or by the app, after which
// AKENO STREAM is on screen again. See docs/BROWSER_RESEARCH.md for what is
// known about it and what was verified where.
//
// The engine is a separate system component: AKENO STREAM never sees the
// pages' cookies, storage, passwords or TLS state, and pages get no access to
// the app. The host build has a scripted stand-in (tests/host).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace akeno::platform
{
enum class WebLayout : std::uint8_t
{
    standard, // the system browser's full presentation with its own controls
    custom,   // a rectangle chosen by the app, without the browser's controls
};

struct WebRect
{
    int x = 0, y = 0, w = 1920, h = 1080;
};

struct WebOpenRequest
{
    std::string url; // http(s), at most kMaxUrlBytes
    WebLayout layout = WebLayout::standard;
    WebRect rect; // custom layout only
};

enum class WebStatus : std::uint8_t
{
    closed,   // nothing open
    running,  // the browser is on screen
    finished, // the browser closed since the last update(); read result()
};

// What the engine reported, for the diagnostics screen and report.
struct WebEngineInfo
{
    bool available = false;         // prepare() succeeded
    bool attempted = false;         // prepare() has run
    std::string engine;             // "PS5 system browser (libSceWebBrowserDialog)"
    std::string error;              // why it is unavailable
    std::vector<std::string> steps; // "sceWebBrowserDialogInitialize -> 0x00000000"
};

class WebView
{
  public:
    static constexpr std::size_t kMaxUrlBytes = 2047;

    virtual ~WebView() = default;
    // Brings the browser subsystem up once per process. False + reason when
    // the engine cannot be used; later calls return the first answer.
    virtual bool prepare(std::string *error) = 0;
    // Opens the browser. Only one at a time.
    virtual bool open(const WebOpenRequest &request, std::string *error) = 0;
    // Call once per frame while open (the dialog only advances when pumped).
    virtual WebStatus update() = 0;
    // Asks an open browser to close; update() reports finished shortly after.
    virtual void close() = 0;
    [[nodiscard]] virtual bool is_open() const = 0;
    // The engine's result code of the last finished session (0 = none).
    [[nodiscard]] virtual int result() const = 0;
    [[nodiscard]] virtual WebEngineInfo info() const = 0;
    // Clears every cookie the browser keeps (all sites: it signs the user out
    // everywhere). Only while the browser is closed; false with a reason
    // where the console's browser does not offer it to apps.
    virtual bool clear_cookies(std::string *error) = 0;
};

std::unique_ptr<WebView> make_web_view();
} // namespace akeno::platform
