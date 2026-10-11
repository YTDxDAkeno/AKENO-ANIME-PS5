// AKENO STREAM PS5 - Officially supported embedded players for the video-only page.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "web/embed.hpp"

#include "core/url.hpp"
#include "web/address.hpp"

#include <cctype>
#include <vector>

namespace akeno::web
{
namespace
{
std::string lower(std::string_view text)
{
    std::string out{text};
    for (char &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    return text;
}

std::vector<std::string_view> split(std::string_view text, char separator)
{
    std::vector<std::string_view> out;
    while (true)
    {
        const std::size_t at = text.find(separator);
        out.push_back(text.substr(0, at));
        if (at == std::string_view::npos)
            return out;
        text.remove_prefix(at + 1);
    }
}

std::string query_value(std::string_view query, std::string_view key)
{
    for (std::string_view pair : split(query, '&'))
    {
        const std::size_t eq = pair.find('=');
        if (eq != std::string_view::npos && pair.substr(0, eq) == key)
            return url::decode_component(pair.substr(eq + 1));
    }
    return {};
}

bool is_hex(char c) noexcept
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// One frame-ancestors source against the embedding page's origin.
bool source_matches(std::string_view source, std::string_view embed_origin,
                    const url::Url &embedder)
{
    const std::string s = lower(source);
    if (s == "*")
        return embedder.is_http();
    if (s == "'self'")
        return lower(embed_origin) == embedder.scheme + "://" + embedder.authority;
    if (s.size() > 1 && s.back() == ':' && s.find('/') == std::string::npos)
        return s.substr(0, s.size() - 1) == embedder.scheme; // scheme-source "http:"
    std::string_view rest = s;
    const std::size_t scheme_end = rest.find("://");
    if (scheme_end != std::string_view::npos)
    {
        if (rest.substr(0, scheme_end) != embedder.scheme)
            return false;
        rest.remove_prefix(scheme_end + 3);
    }
    rest = rest.substr(0, rest.find('/'));
    std::string_view host = rest, port;
    if (const std::size_t colon = rest.rfind(':'); colon != std::string_view::npos)
    {
        host = rest.substr(0, colon);
        port = rest.substr(colon + 1);
    }
    const int default_port = embedder.scheme == "https" ? 443 : 80;
    const int actual_port = embedder.port > 0 ? embedder.port : default_port;
    if (!port.empty() && port != "*" && port != std::to_string(actual_port))
        return false;
    if (port.empty() && actual_port != default_port)
        return false;
    if (host.starts_with("*."))
        return embedder.host.size() > host.size() - 1 &&
               embedder.host.ends_with(host.substr(1)); // "*.example.com"
    return host == embedder.host;
}
} // namespace

const char *embed_kind_id(EmbedKind kind) noexcept
{
    switch (kind)
    {
    case EmbedKind::youtube:
        return "youtube";
    case EmbedKind::peertube:
        return "peertube";
    default:
        return "none";
    }
}

const char *embed_kind_label(EmbedKind kind) noexcept
{
    switch (kind)
    {
    case EmbedKind::youtube:
        return "YouTube";
    case EmbedKind::peertube:
        return "PeerTube";
    default:
        return "None";
    }
}

bool valid_embed_host(std::string_view host) noexcept
{
    std::string_view name = host;
    if (const std::size_t colon = host.find(':'); colon != std::string_view::npos)
    {
        const std::string_view port = host.substr(colon + 1);
        if (port.empty() || port.size() > 5)
            return false;
        int value = 0;
        for (char c : port)
        {
            if (c < '0' || c > '9')
                return false;
            value = value * 10 + (c - '0');
        }
        if (value < 1 || value > 65535)
            return false;
        name = host.substr(0, colon);
    }
    if (name.empty() || name.size() > 253 || name.front() == '.' || name.back() == '.' ||
        name.front() == '-' || name.find("..") != std::string_view::npos ||
        name.find('.') == std::string_view::npos)
        return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.'))
            return false;
    return true;
}

bool valid_peertube_id(std::string_view id) noexcept
{
    if (id.size() == 36)
    {
        for (std::size_t i = 0; i < id.size(); ++i)
        {
            const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
            if (dash ? id[i] != '-' : !is_hex(id[i]))
                return false;
        }
        return true;
    }
    // Short UUIDs: base58 (no 0, O, I, l), as PeerTube prints them.
    if (id.size() < 16 || id.size() > 24)
        return false;
    for (char c : id)
        if (!std::isalnum(static_cast<unsigned char>(c)) || c == '0' || c == 'O' || c == 'I' ||
            c == 'l')
            return false;
    return true;
}

bool EmbedTarget::valid() const noexcept
{
    switch (kind)
    {
    case EmbedKind::youtube:
        return valid_video_id(video_id) && host.empty();
    case EmbedKind::peertube:
        return valid_embed_host(host) && valid_peertube_id(video_id);
    default:
        return false;
    }
}

std::string EmbedTarget::origin() const
{
    switch (kind)
    {
    case EmbedKind::youtube:
        return "https://www.youtube.com";
    case EmbedKind::peertube:
        return valid_embed_host(host) ? "https://" + host : std::string{};
    default:
        return {};
    }
}

std::string EmbedTarget::page_url() const
{
    if (!valid())
        return {};
    if (kind == EmbedKind::youtube)
        return "https://www.youtube.com/watch?v=" + video_id;
    return "https://" + host + "/w/" + video_id;
}

std::optional<EmbedTarget> parse_embed(std::string_view address)
{
    address = trim(address);
    if (const auto yt = parse_youtube(address); yt && !yt->video_id.empty())
    {
        EmbedTarget t;
        t.kind = EmbedKind::youtube;
        t.video_id = yt->video_id;
        t.start_seconds = yt->start_seconds;
        return t;
    }
    const auto parsed = url::parse(address);
    if (!parsed || !parsed->is_https())
        return std::nullopt;
    std::vector<std::string_view> segments;
    for (std::string_view s : split(parsed->path, '/'))
        if (!s.empty())
            segments.push_back(s);
    std::string_view id;
    if (segments.size() == 3 && segments[0] == "videos" &&
        (segments[1] == "embed" || segments[1] == "watch"))
        id = segments[2];
    else if (segments.size() == 2 && segments[0] == "w")
        id = segments[1];
    EmbedTarget t;
    t.kind = EmbedKind::peertube;
    t.host = parsed->host + (parsed->port > 0 ? ":" + std::to_string(parsed->port) : std::string{});
    t.video_id = std::string{id};
    if (!t.valid())
        return std::nullopt;
    std::string start = query_value(parsed->query, "start");
    if (start.empty())
        start = query_value(parsed->query, "t");
    t.start_seconds = parse_start_time(start);
    return t;
}

std::optional<EmbedTarget> peertube_embed_from_ref(std::string_view source_ref)
{
    const std::size_t at = source_ref.find('@');
    if (at == std::string_view::npos || source_ref.find('@', at + 1) != std::string_view::npos)
        return std::nullopt;
    EmbedTarget t;
    t.kind = EmbedKind::peertube;
    t.video_id = std::string{source_ref.substr(0, at)};
    t.host = lower(source_ref.substr(at + 1));
    if (!t.valid())
        return std::nullopt;
    return t;
}

std::string watch_query(const EmbedTarget &target)
{
    if (!target.valid())
        return {};
    std::vector<std::pair<std::string, std::string>> q{{"p", embed_kind_id(target.kind)}};
    if (target.kind == EmbedKind::peertube)
        q.emplace_back("h", target.host);
    q.emplace_back("v", target.video_id);
    if (target.start_seconds > 0)
        q.emplace_back("t", std::to_string(target.start_seconds));
    return url::build_query(q);
}

FrameCheck frame_policy(std::string_view x_frame_options, std::string_view content_security_policy,
                        std::string_view embed_origin, std::string_view embedder_origin)
{
    FrameCheck out;
    const auto embedder = url::parse(std::string{embedder_origin} + "/");
    if (!embedder)
    {
        out.detail = "the embedding page's origin is not valid";
        return out;
    }
    // frame-ancestors, when present, decides; X-Frame-Options is then ignored.
    // Several policies (one header each, or joined with ',') must all allow it.
    bool any_ancestors = false;
    for (std::string_view policy : split(content_security_policy, ','))
        for (std::string_view directive : split(policy, ';'))
        {
            directive = trim(directive);
            const std::string name = lower(directive.substr(0, directive.find(' ')));
            if (name != "frame-ancestors")
                continue;
            any_ancestors = true;
            bool allowed = false;
            std::string_view sources =
                directive.size() > name.size() ? directive.substr(name.size()) : std::string_view{};
            for (std::string_view source : split(trim(sources), ' '))
            {
                source = trim(source);
                if (source.empty())
                    continue;
                if (lower(source) == "'none'")
                {
                    allowed = false;
                    break;
                }
                if (source_matches(source, embed_origin, *embedder))
                    allowed = true;
            }
            if (!allowed)
            {
                out.policy = FramePolicy::blocked;
                out.detail = "Content-Security-Policy: " + std::string{directive};
                return out;
            }
        }
    if (any_ancestors)
    {
        out.policy = FramePolicy::allowed;
        out.detail = "frame-ancestors allows AKENO's page";
        return out;
    }
    const std::string xfo = lower(trim(x_frame_options));
    if (xfo == "deny")
    {
        out.policy = FramePolicy::blocked;
        out.detail = "X-Frame-Options: DENY";
        return out;
    }
    if (xfo == "sameorigin")
    {
        const bool same = lower(embed_origin) == embedder->scheme + "://" + embedder->authority;
        out.policy = same ? FramePolicy::allowed : FramePolicy::blocked;
        out.detail = "X-Frame-Options: SAMEORIGIN";
        return out;
    }
    out.detail = xfo.empty() ? "no frame restriction in the response"
                             : "X-Frame-Options \"" + xfo + "\" is not a value browsers apply";
    return out;
}
} // namespace akeno::web
