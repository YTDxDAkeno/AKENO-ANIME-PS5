// AKENO STREAM PS5 - The embedded web browser on the console (libSceWebBrowserDialog).
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The parameter and result layouts (48-byte common block with a check value
// derived from the block's own address, 328-byte parameter block, 256-byte
// result) and the order CommonDialog -> sysmodule 0xAB -> Initialize -> Open ->
// UpdateStatus every frame -> GetResult -> Close follow two independent,
// GPL-3.0 sources: SharpProspero's Interop/Dialog/WebBrowserDialog.cs and
// EVO-PLAYER-PS5's src/evo_webui.c, whose author opened this dialog from a
// fake-signed native title on firmware 12.70 (docs/BROWSER_RESEARCH.md).
//
// Only the six dialog functions EVO-PLAYER-PS5 called on hardware and
// sceCommonDialogInitialize are imported, as positional imports through the
// link stubs in tooling/stubs: a fake-signed title cannot load system modules
// with sceKernelLoadStartModule, and a name the firmware's module lacks would
// stop the whole title from loading.
#include "platform/web_view.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C"
{
    int sceCommonDialogInitialize(void);
    int sceSysmoduleLoadModule(std::uint32_t id);
    int sceUserServiceGetInitialUser(int *user);
    int sceWebBrowserDialogInitialize(void);
    int sceWebBrowserDialogOpen(void *param);
    int sceWebBrowserDialogUpdateStatus(void);
    int sceWebBrowserDialogGetResult(void *result);
    int sceWebBrowserDialogClose(void);
}

namespace akeno::platform
{
namespace
{
constexpr std::uint32_t kSysmoduleWebBrowserDialog = 0x00AB;
constexpr std::uint32_t kMagic = 0xC0D1A109u;
constexpr int kAlreadyInitialized = static_cast<int>(0x80B80002u);
constexpr int kModeDefault = 1;
constexpr int kModeCustom = 2;
constexpr int kStatusInitialized = 1;
constexpr int kStatusRunning = 2;
constexpr int kStartGraceFrames = 30;
constexpr int kCloseTimeoutFrames = 600; // ~10 s for the dialog to go away

struct CommonDialogBaseParam
{
    std::uint64_t size;
    std::uint8_t reserved[36];
    std::uint32_t magic; // kMagic + the address of this block
};
static_assert(sizeof(CommonDialogBaseParam) == 48);

struct WebBrowserDialogParam
{
    CommonDialogBaseParam base;  // 0
    std::uint64_t size;          // 48
    std::int32_t mode;           // 56: 1 default, 2 custom rectangle
    std::int32_t user_id;        // 60
    const char *url;             // 64
    void *callback_init;         // 72: not used (does not work, per EVO-PLAYER-PS5)
    std::uint16_t width, height; // 80: custom mode only
    std::uint16_t x, y;          // 84
    std::uint32_t parts;         // 88
    std::uint16_t header_width;  // 92
    std::uint16_t header_x;      // 94
    std::uint16_t header_y;      // 96
    std::uint16_t padding;       // 98
    std::uint32_t control;       // 100
    void *ime_param;             // 104
    void *webview_param;         // 112
    std::uint32_t animation;     // 120
    std::uint8_t reserved[202];  // 124
    std::uint16_t tail_padding;  // 326
};
static_assert(sizeof(WebBrowserDialogParam) == 328);
static_assert(offsetof(WebBrowserDialogParam, url) == 64);
static_assert(offsetof(WebBrowserDialogParam, width) == 80);
static_assert(offsetof(WebBrowserDialogParam, control) == 100);
static_assert(offsetof(WebBrowserDialogParam, animation) == 120);

struct WebBrowserDialogResult
{
    std::int32_t result;
    std::int32_t padding;
    void *callback_result;
    std::uint8_t reserved[240];
};
static_assert(sizeof(WebBrowserDialogResult) == 256);

// The check value depends on the block's address and the URL must stay valid
// while the browser runs: both live at fixed addresses for the whole session.
alignas(16) WebBrowserDialogParam g_param;
char g_url[WebView::kMaxUrlBytes + 1];

std::string hex(int value)
{
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08x", static_cast<unsigned>(value));
    return text;
}

bool valid_user(int user) noexcept
{
    // -1 is "invalid"; 0xFE and 0xFF are the "everyone" and system ids, which
    // the browser refuses (SharpProspero, WebBrowser.cs).
    return user != -1 && user != 0xFE && user != 0xFF;
}

class SystemWebView final : public WebView
{
  public:
    bool prepare(std::string *error) override
    {
        if (!info_.attempted)
        {
            info_.attempted = true;
            info_.engine = "PS5 system browser (libSceWebBrowserDialog)";
            int rc = sceCommonDialogInitialize();
            step("sceCommonDialogInitialize", rc);
            if (rc < 0 && rc != kAlreadyInitialized)
                info_.error = "the system dialog service did not start (" + hex(rc) + ")";
            if (info_.error.empty())
            {
                rc = sceSysmoduleLoadModule(kSysmoduleWebBrowserDialog);
                step("sceSysmoduleLoadModule(0xAB)", rc);
                if (rc < 0)
                    info_.error = "the system browser module did not load (" + hex(rc) + ")";
            }
            if (info_.error.empty())
            {
                rc = sceWebBrowserDialogInitialize();
                step("sceWebBrowserDialogInitialize", rc);
                if (rc < 0 && rc != kAlreadyInitialized)
                    info_.error = "the system browser did not initialize (" + hex(rc) + ")";
            }
            info_.available = info_.error.empty();
        }
        if (!info_.available && error)
            *error = info_.error;
        return info_.available;
    }

    bool open(const WebOpenRequest &request, std::string *error) override
    {
        const auto fail = [&](std::string why)
        {
            if (error)
                *error = std::move(why);
            return false;
        };
        if (!prepare(error))
            return false;
        if (open_)
            return fail("the browser is already open");
        if (request.url.empty() || request.url.size() > kMaxUrlBytes)
            return fail("the address is empty or too long");
        int user = -1;
        const int user_rc = sceUserServiceGetInitialUser(&user);
        if (user_rc != 0 || !valid_user(user))
        {
            step("sceUserServiceGetInitialUser", user_rc);
            return fail("no signed-in user for the browser (" + hex(user_rc) + ")");
        }
        std::memcpy(g_url, request.url.data(), request.url.size());
        g_url[request.url.size()] = '\0';
        std::memset(&g_param, 0, sizeof(g_param));
        g_param.base.size = sizeof(g_param.base);
        g_param.base.magic = static_cast<std::uint32_t>(
            kMagic + static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&g_param.base)));
        g_param.size = sizeof(g_param);
        g_param.mode = kModeDefault;
        g_param.user_id = user;
        g_param.url = g_url;
        if (request.layout == WebLayout::custom)
        {
            // Parts 0 / control 0 in a rectangle: the layout EVO-PLAYER-PS5
            // opened on hardware. Used only for AKENO's own pages, which carry
            // their own way back.
            const auto clamp16 = [](int v, int high)
            { return static_cast<std::uint16_t>(v < 0 ? 0 : (v > high ? high : v)); };
            g_param.mode = kModeCustom;
            g_param.x = clamp16(request.rect.x, 1919);
            g_param.y = clamp16(request.rect.y, 1079);
            g_param.width = clamp16(request.rect.w, 1920 - g_param.x);
            g_param.height = clamp16(request.rect.h, 1080 - g_param.y);
            g_param.header_width = g_param.width;
            g_param.header_x = g_param.x;
            g_param.header_y = g_param.y;
        }
        const int rc = sceWebBrowserDialogOpen(&g_param);
        step(request.layout == WebLayout::custom ? "sceWebBrowserDialogOpen (custom)"
                                                 : "sceWebBrowserDialogOpen",
             rc);
        if (rc != 0)
            return fail("the system browser refused to open (" + hex(rc) + ")");
        open_ = true;
        closing_ = false;
        closed_by_app_ = false;
        close_frames_ = 0;
        open_frames_ = 0;
        last_status_ = -1000;
        return true;
    }

    WebStatus update() override
    {
        if (!open_)
            return WebStatus::closed;
        const int status = sceWebBrowserDialogUpdateStatus();
        if (status != last_status_)
        {
            step("sceWebBrowserDialogUpdateStatus", status);
            last_status_ = status;
        }
        ++open_frames_;
        bool gone = status != kStatusRunning;
        // Right after Open the dialog may still report "initialized" for a
        // moment before it runs; only a lasting non-running status ends it.
        if (gone && status == kStatusInitialized && !closing_ && open_frames_ < kStartGraceFrames)
            return WebStatus::running;
        if (closing_ && !gone && ++close_frames_ >= kCloseTimeoutFrames)
        {
            step("close timeout: the dialog still reports running", status);
            gone = true;
        }
        if (!gone)
            return WebStatus::running;
        WebBrowserDialogResult r;
        std::memset(&r, 0, sizeof(r));
        const int rc = sceWebBrowserDialogGetResult(&r);
        step("sceWebBrowserDialogGetResult", rc);
        result_ = rc == 0 ? r.result : rc;
        // A dialog the user closed is closed here too, so the next open
        // starts from a clean state (EVO-PLAYER-PS5 reopens the same way).
        if (!closed_by_app_)
            step("sceWebBrowserDialogClose (after the user closed it)", sceWebBrowserDialogClose());
        open_ = false;
        closing_ = false;
        return WebStatus::finished;
    }

    void close() override
    {
        if (!open_ || closing_)
            return;
        closing_ = true;
        closed_by_app_ = true;
        step("sceWebBrowserDialogClose", sceWebBrowserDialogClose());
    }

    [[nodiscard]] bool is_open() const override
    {
        return open_;
    }
    [[nodiscard]] int result() const override
    {
        return result_;
    }
    [[nodiscard]] WebEngineInfo info() const override
    {
        return info_;
    }

  private:
    void step(const char *what, int rc)
    {
        info_.steps.push_back(std::string{what} + " -> " + hex(rc));
        if (info_.steps.size() > 40)
            info_.steps.erase(info_.steps.begin() + 4); // keep the start-up lines
    }

    WebEngineInfo info_;
    bool open_ = false;
    bool closing_ = false;
    bool closed_by_app_ = false;
    int close_frames_ = 0;
    int open_frames_ = 0;
    int last_status_ = -1000;
    int result_ = 0;
};
} // namespace

std::unique_ptr<WebView> make_web_view()
{
    return std::make_unique<SystemWebView>();
}
} // namespace akeno::platform
