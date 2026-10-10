// AKENO STREAM PS5 - Provider helpers.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "providers/provider.hpp"

#include <cmath>
#include <cstdio>

namespace akeno
{
Fetched fetch_text(const std::string &url, const net::CancelFlag &cancel,
                   const std::string &service, std::size_t max_bytes)
{
    Fetched out;
    net::Client client;
    net::Request request;
    request.url = url;
    request.headers = {"Accept: application/json"};
    request.cancel = cancel;
    request.max_bytes = max_bytes;
    const net::Response r = client.perform(request);
    out.status = r.status;
    out.body = r.body;
    out.ok = r.ok();
    if (!out.ok)
        out.error = r.outcome == net::Outcome::http_error
                        ? service + " answered HTTP " + std::to_string(r.status)
                        : "Could not reach " + service + " (" + r.describe() + ")";
    return out;
}

const char *support_label(Support support) noexcept
{
    switch (support)
    {
    case Support::available:
        return "Available";
    case Support::needs_setup:
        return "Setup needed";
    case Support::unavailable:
        return "Not available";
    }
    return "Unknown";
}

std::string strip_html(const std::string &html)
{
    std::string out;
    out.reserve(html.size());
    for (std::size_t i = 0; i < html.size(); ++i)
    {
        const char c = html[i];
        if (c == '<')
        {
            const std::size_t end = html.find('>', i);
            if (end == std::string::npos)
                break;
            std::string tag = html.substr(i + 1, end - i - 1);
            for (char &t : tag)
                t = static_cast<char>(t >= 'A' && t <= 'Z' ? t - 'A' + 'a' : t);
            if (tag.starts_with("br") || tag == "p" || tag == "/p")
                out += '\n';
            i = end;
            continue;
        }
        if (c == '&')
        {
            static constexpr struct
            {
                const char *entity;
                const char *text;
            } entities[] = {{"&amp;", "&"},
                            {"&lt;", "<"},
                            {"&gt;", ">"},
                            {"&quot;", "\""},
                            {"&#39;", "'"},
                            {"&apos;", "'"},
                            {"&nbsp;", " "},
                            {"&mdash;", "\xE2\x80\x94"},
                            {"&ndash;", "\xE2\x80\x93"},
                            {"&hellip;", "\xE2\x80\xA6"}};
            bool matched = false;
            for (const auto &e : entities)
            {
                const std::string entity = e.entity;
                if (html.compare(i, entity.size(), entity) == 0)
                {
                    out += e.text;
                    i += entity.size() - 1;
                    matched = true;
                    break;
                }
            }
            if (!matched)
                out += c;
            continue;
        }
        if (c == '\r')
            continue;
        out += c;
    }
    // Collapse runs of blank lines.
    std::string compact;
    int newlines = 0;
    for (char c : out)
    {
        if (c == '\n')
        {
            if (++newlines > 2)
                continue;
        }
        else
        {
            newlines = 0;
        }
        compact += c;
    }
    while (!compact.empty() && (compact.back() == '\n' || compact.back() == ' '))
        compact.pop_back();
    while (!compact.empty() && (compact.front() == '\n' || compact.front() == ' '))
        compact.erase(compact.begin());
    return compact;
}

double parse_iso8601_duration(const std::string &text)
{
    if (!text.starts_with("P"))
        return 0.0;
    double total = 0.0, value = 0.0;
    bool in_time = false, have_digits = false;
    for (std::size_t i = 1; i < text.size(); ++i)
    {
        const char c = text[i];
        if (c >= '0' && c <= '9')
        {
            value = value * 10 + (c - '0');
            have_digits = true;
            continue;
        }
        if (c == 'T')
        {
            in_time = true;
            continue;
        }
        if (!have_digits)
            return 0.0;
        switch (c)
        {
        case 'D':
            total += value * 86400;
            break;
        case 'H':
            total += value * 3600;
            break;
        case 'M':
            total += in_time ? value * 60 : value * 30 * 86400;
            break;
        case 'S':
            total += value;
            break;
        case 'W':
            total += value * 7 * 86400;
            break;
        default:
            return 0.0;
        }
        value = 0.0;
        have_digits = false;
    }
    return have_digits ? 0.0 : total;
}

std::string format_clock(double seconds)
{
    if (!(seconds >= 0.0) || seconds > 1.0e7)
        seconds = 0.0;
    const long long s = static_cast<long long>(seconds);
    char text[32];
    if (s >= 3600)
        std::snprintf(text, sizeof(text), "%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    else
        std::snprintf(text, sizeof(text), "%lld:%02lld", s / 60, s % 60);
    return text;
}

std::string format_count(long long value)
{
    char text[32];
    if (value >= 1000000000)
        std::snprintf(text, sizeof(text), "%.1fB", value / 1.0e9);
    else if (value >= 1000000)
        std::snprintf(text, sizeof(text), "%.1fM", value / 1.0e6);
    else if (value >= 1000)
        std::snprintf(text, sizeof(text), "%.1fK", value / 1.0e3);
    else
        std::snprintf(text, sizeof(text), "%lld", value);
    return text;
}
} // namespace akeno
