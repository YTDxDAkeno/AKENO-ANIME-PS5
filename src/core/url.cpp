// AKENO STREAM PS5 - URL parsing, reference resolution and encoding.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/url.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace akeno::url
{
namespace
{
bool forbidden(char c) noexcept
{
    const auto u = static_cast<unsigned char>(c);
    return u <= 0x20 || u == 0x7f || c == '\\' || c == '<' || c == '>' || c == '"';
}

std::string lower(std::string_view s)
{
    std::string out{s};
    for (char &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// RFC 3986 section 5.2.4.
std::string remove_dot_segments(std::string_view input)
{
    std::string in{input};
    std::string out;
    while (!in.empty())
    {
        if (in.starts_with("../"))
            in.erase(0, 3);
        else if (in.starts_with("./"))
            in.erase(0, 2);
        else if (in.starts_with("/./"))
            in.replace(0, 3, "/");
        else if (in == "/.")
            in = "/";
        else if (in.starts_with("/../") || in == "/..")
        {
            in = in.size() == 3 ? "/" : in.substr(3);
            const std::size_t slash = out.rfind('/');
            out.erase(slash == std::string::npos ? 0 : slash);
        }
        else if (in == "." || in == "..")
            in.clear();
        else
        {
            const std::size_t start = in[0] == '/' ? 1 : 0;
            const std::size_t next = in.find('/', start);
            const std::size_t len = next == std::string::npos ? in.size() : next;
            out += in.substr(0, len);
            in.erase(0, len);
        }
    }
    return out;
}

std::string merge_paths(const Url &base, std::string_view ref_path)
{
    if (!base.authority.empty() && base.path.empty())
        return "/" + std::string{ref_path};
    const std::size_t slash = base.path.rfind('/');
    return (slash == std::string::npos ? std::string{} : base.path.substr(0, slash + 1)) +
           std::string{ref_path};
}
} // namespace

std::string Url::str() const
{
    std::string out = scheme + "://" + authority + (path.empty() ? "/" : path);
    if (has_query)
        out += "?" + query;
    return out;
}

std::optional<Url> parse(std::string_view text)
{
    if (text.empty() || text.size() > 8192)
        return std::nullopt;
    if (std::any_of(text.begin(), text.end(), forbidden))
        return std::nullopt;
    const std::size_t hash = text.find('#');
    if (hash != std::string_view::npos)
        text = text.substr(0, hash);
    const std::size_t colon = text.find("://");
    if (colon == std::string_view::npos || colon == 0)
        return std::nullopt;
    Url url;
    url.scheme = lower(text.substr(0, colon));
    if (!url.is_http())
        return std::nullopt;
    std::string_view rest = text.substr(colon + 3);
    const std::size_t end = rest.find_first_of("/?");
    const std::string_view authority = rest.substr(0, end);
    rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end);
    if (authority.empty() || authority.find('@') != std::string_view::npos)
        return std::nullopt;
    std::string_view host = authority;
    const std::size_t port_colon = authority.rfind(':');
    if (port_colon != std::string_view::npos && authority.find(']') == std::string_view::npos)
    {
        const std::string_view digits = authority.substr(port_colon + 1);
        if (digits.empty() || digits.size() > 5 ||
            !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return std::nullopt;
        int port = 0;
        for (char c : digits)
            port = port * 10 + (c - '0');
        if (port <= 0 || port > 65535)
            return std::nullopt;
        url.port = port;
        host = authority.substr(0, port_colon);
    }
    if (host.empty())
        return std::nullopt;
    url.host = lower(host);
    url.authority = url.port > 0 ? url.host + ":" + std::to_string(url.port) : url.host;
    const std::size_t question = rest.find('?');
    url.path = std::string{rest.substr(0, question)};
    if (question != std::string_view::npos)
    {
        url.has_query = true;
        url.query = std::string{rest.substr(question + 1)};
    }
    if (url.path.empty())
        url.path = "/";
    return url;
}

std::optional<std::string> resolve(std::string_view base_text, std::string_view reference)
{
    const auto base = parse(base_text);
    if (!base)
        return std::nullopt;
    // Trim surrounding whitespace that playlists sometimes carry.
    while (!reference.empty() && (reference.front() == ' ' || reference.front() == '\t'))
        reference.remove_prefix(1);
    while (!reference.empty() &&
           (reference.back() == ' ' || reference.back() == '\t' || reference.back() == '\r'))
        reference.remove_suffix(1);
    if (std::any_of(reference.begin(), reference.end(), forbidden))
        return std::nullopt;
    const std::size_t hash = reference.find('#');
    if (hash != std::string_view::npos)
        reference = reference.substr(0, hash);

    // Absolute reference with a scheme.
    const std::size_t scheme_end = reference.find(':');
    const std::size_t first_delim = reference.find_first_of("/?");
    if (scheme_end != std::string_view::npos &&
        (first_delim == std::string_view::npos || scheme_end < first_delim))
    {
        auto absolute = parse(reference);
        if (!absolute)
            return std::nullopt;
        absolute->path = remove_dot_segments(absolute->path);
        return absolute->str();
    }
    Url target;
    target.scheme = base->scheme;
    if (reference.starts_with("//"))
    {
        auto absolute = parse(base->scheme + ":" + std::string{reference});
        if (!absolute)
            return std::nullopt;
        absolute->path = remove_dot_segments(absolute->path);
        return absolute->str();
    }
    target.authority = base->authority;
    target.host = base->host;
    target.port = base->port;
    const std::size_t question = reference.find('?');
    const std::string_view ref_path = reference.substr(0, question);
    const bool ref_has_query = question != std::string_view::npos;
    const std::string_view ref_query =
        ref_has_query ? reference.substr(question + 1) : std::string_view{};
    if (ref_path.empty())
    {
        target.path = base->path;
        target.has_query = ref_has_query ? true : base->has_query;
        target.query = ref_has_query ? std::string{ref_query} : base->query;
    }
    else
    {
        target.path = ref_path.front() == '/' ? remove_dot_segments(ref_path)
                                              : remove_dot_segments(merge_paths(*base, ref_path));
        target.has_query = ref_has_query;
        target.query = std::string{ref_query};
    }
    if (target.path.empty() || target.path.front() != '/')
        target.path.insert(target.path.begin(), '/');
    return target.str();
}

std::string encode_component(std::string_view text)
{
    static const char digits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 3);
    for (const char c : text)
    {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            out += c;
        }
        else
        {
            out += '%';
            out += digits[u >> 4];
            out += digits[u & 15];
        }
    }
    return out;
}

std::string decode_component(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    const auto hexval = [](char c) -> int
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '%' && i + 2 < text.size())
        {
            const int hi = hexval(text[i + 1]), lo = hexval(text[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                out += static_cast<char>(hi * 16 + lo);
                i += 2;
                continue;
            }
        }
        out += text[i] == '+' ? ' ' : text[i];
    }
    return out;
}

std::string build_query(const std::vector<std::pair<std::string, std::string>> &params)
{
    std::string out;
    for (const auto &[key, value] : params)
    {
        if (!out.empty())
            out += '&';
        out += encode_component(key);
        out += '=';
        out += encode_component(value);
    }
    return out;
}

std::string redact(std::string_view text)
{
    const std::size_t question = text.find('?');
    if (question == std::string_view::npos)
        return std::string{text};
    std::string out{text.substr(0, question + 1)};
    std::string_view query = text.substr(question + 1);
    bool first = true;
    while (!query.empty())
    {
        const std::size_t amp = query.find('&');
        const std::string_view pair = query.substr(0, amp);
        const std::size_t eq = pair.find('=');
        const std::string key = lower(pair.substr(0, eq));
        static constexpr std::string_view secret_keys[] = {
            "key",           "api_key", "apikey",        "token",  "access_token",
            "refresh_token", "auth",    "signature",     "sig",    "password",
            "pass",          "secret",  "client_secret", "session"};
        const bool secret =
            std::find(std::begin(secret_keys), std::end(secret_keys), key) != std::end(secret_keys);
        if (!first)
            out += '&';
        first = false;
        if (secret && eq != std::string_view::npos)
            out += std::string{pair.substr(0, eq)} + "=REDACTED";
        else
            out += pair;
        if (amp == std::string_view::npos)
            break;
        query.remove_prefix(amp + 1);
    }
    return out;
}
} // namespace akeno::url
