// AKENO STREAM PS5 - Scripted stand-in for the console's embedded browser.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "host_web_view.hpp"

#include <mutex>

namespace akeno::test
{
namespace
{
std::mutex g_lock;
WebViewScript g_script;
} // namespace

WebViewScript &web_view_script()
{
    return g_script;
}

void reset_web_view_script()
{
    std::lock_guard<std::mutex> guard(g_lock);
    g_script = WebViewScript{};
}
} // namespace akeno::test

namespace akeno::platform
{
namespace
{
class HostWebView final : public WebView
{
  public:
    bool prepare(std::string *error) override
    {
        auto &script = test::web_view_script();
        info_.attempted = true;
        info_.engine = "host stand-in (no browser on the build machine)";
        info_.available = script.available;
        info_.error = script.available ? "" : script.unavailable_reason;
        if (info_.steps.empty())
            info_.steps.push_back("host: prepare -> " +
                                  std::string{script.available ? "ok" : "unavailable"});
        if (!script.available && error)
            *error = script.unavailable_reason;
        return script.available;
    }

    bool open(const WebOpenRequest &request, std::string *error) override
    {
        auto &script = test::web_view_script();
        if (!prepare(error))
            return false;
        if (open_ || script.refuse_open || request.url.empty() || request.url.size() > kMaxUrlBytes)
        {
            if (error)
                *error = open_ ? "the browser is already open"
                               : "the system browser refused to open (0x80b8000a)";
            return false;
        }
        script.opened.push_back(request);
        open_ = true;
        closing_ = false;
        updates_ = 0;
        return true;
    }

    WebStatus update() override
    {
        if (!open_)
            return WebStatus::closed;
        auto &script = test::web_view_script();
        ++updates_;
        const bool user_closed =
            script.finish_after_updates >= 0 && updates_ >= script.finish_after_updates;
        if (!closing_ && !user_closed)
            return WebStatus::running;
        open_ = false;
        result_ = script.result_code;
        return WebStatus::finished;
    }

    void close() override
    {
        if (!open_)
            return;
        closing_ = true;
        ++test::web_view_script().closes_requested;
    }

    [[nodiscard]] bool is_open() const override
    {
        return open_;
    }
    [[nodiscard]] int result() const override
    {
        return result_;
    }
    bool clear_cookies(std::string *error) override
    {
        test::WebViewScript &script = test::web_view_script();
        if (open_ || !script.can_clear_cookies)
        {
            if (error)
                *error = open_ ? "close the browser first"
                               : "this browser does not offer clearing its data to apps";
            return false;
        }
        ++script.cookies_cleared;
        return true;
    }

    [[nodiscard]] WebEngineInfo info() const override
    {
        return info_;
    }

  private:
    WebEngineInfo info_;
    bool open_ = false;
    bool closing_ = false;
    int updates_ = 0;
    int result_ = 0;
};
} // namespace

std::unique_ptr<WebView> make_web_view()
{
    return std::make_unique<HostWebView>();
}
} // namespace akeno::platform
