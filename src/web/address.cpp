// AKENO STREAM PS5 - What the user typed, turned into an address the browser may open.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "web/address.hpp"

#include "core/url.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <vector>

namespace akeno::web
{
namespace
{
constexpr std::size_t kMaxAddress = 2047;

std::string_view trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

std::string lower(std::string_view text)
{
    std::string out{text};
    for (char &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool scheme_char(char c, bool first) noexcept
{
    const auto u = static_cast<unsigned char>(c);
    return std::isalpha(u) || (!first && (std::isdigit(u) || c == '+' || c == '-' || c == '.'));
}

// The text before a ':' when it is a URL scheme ("example.com:8080" is a host
// and a port, not the scheme "example.com").
std::optional<std::string> scheme_of(std::string_view text)
{
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos || colon == 0)
        return std::nullopt;
    for (std::size_t i = 0; i < colon; ++i)
        if (!scheme_char(text[i], i == 0))
            return std::nullopt;
    const std::string_view after = text.substr(colon + 1);
    if (!after.starts_with("//") && !after.empty() &&
        std::isdigit(static_cast<unsigned char>(after.front())))
        return std::nullopt;
    return lower(text.substr(0, colon));
}

bool is_ipv4(std::string_view host) noexcept
{
    int parts = 0, digits = 0, value = 0;
    for (char c : host)
    {
        if (c == '.')
        {
            if (digits == 0)
                return false;
            ++parts;
            digits = value = 0;
            continue;
        }
        if (c < '0' || c > '9' || ++digits > 3)
            return false;
        value = value * 10 + (c - '0');
        if (value > 255)
            return false;
    }
    return parts == 3 && digits > 0;
}

// Whether text without a scheme names a host (and so is an address, not a search).
bool looks_like_host(std::string_view host)
{
    if (host.empty() || host.size() > 253)
        return false;
    if (host.front() == '[')
        return host.back() == ']';
    const std::string h = lower(host);
    if (h == "localhost" || is_ipv4(h))
        return true;
    if (h.find('.') == std::string::npos || h.front() == '.' || h.back() == '.' ||
        h.find("..") != std::string::npos)
        return false;
    for (char c : h)
    {
        const auto u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '-' || c == '.' || c == '_' || u >= 0x80))
            return false;
    }
    const std::string tld = h.substr(h.rfind('.') + 1);
    return tld.size() >= 2 &&
           std::all_of(tld.begin(), tld.end(),
                       [](char c)
                       {
                           const auto u = static_cast<unsigned char>(c);
                           return std::isalpha(u) || c == '-' || u >= 0x80 || std::isdigit(u);
                       }) &&
           !std::all_of(tld.begin(), tld.end(),
                        [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

Destination refuse(std::string why)
{
    Destination d;
    d.error = std::move(why);
    return d;
}

// Validates an address that has an http(s) scheme. The typed text is kept as
// is (url::parse drops the #fragment, which single-page sites need).
Destination accept(std::string address)
{
    if (address.size() > kMaxAddress)
        return refuse("That address is too long.");
    const auto parsed = url::parse(address);
    if (!parsed || !parsed->is_http() || parsed->host.empty())
        return refuse("That is not a valid web address.");
    if (parsed->host.find("..") != std::string::npos || parsed->host.front() == '.')
        return refuse("That is not a valid web address.");
    if (is_loopback_host(parsed->host))
        return refuse("Addresses on this console are reserved for AKENO STREAM's own pages.");
    // Normalise the scheme and host spelling; keep path, query and fragment.
    const std::size_t rest = address.find("://") + 3;
    std::size_t end = address.find_first_of("/?#", rest);
    if (end == std::string::npos)
        end = address.size();
    std::string out = parsed->scheme + "://" + parsed->authority;
    out += end < address.size() ? address.substr(end) : std::string{"/"};
    if (out.size() > kMaxAddress)
        return refuse("That address is too long.");
    Destination d;
    d.ok = true;
    d.url = std::move(out);
    d.insecure = !parsed->is_https();
    return d;
}

Destination resolve_address(std::string_view text, bool allow_search, SearchEngine engine)
{
    text = trim(text);
    if (text.empty())
        return refuse("Enter an address or something to search for.");
    if (const auto scheme = scheme_of(text))
    {
        if (*scheme != "http" && *scheme != "https")
            return refuse("Only web addresses (http:// or https://) can be opened, not \"" +
                          *scheme + ":\".");
        if (text.substr(scheme->size() + 1).substr(0, 2) != "//")
            return refuse("That is not a valid web address.");
        return accept(std::string{text});
    }
    const bool has_space = std::any_of(text.begin(), text.end(), [](char c)
                                       { return std::isspace(static_cast<unsigned char>(c)); });
    std::string_view authority = text.substr(0, text.find_first_of("/?#"));
    if (authority.find('@') != std::string_view::npos && !has_space)
        return refuse("Addresses with a user name or password are not allowed.");
    std::string_view host = authority;
    if (!host.empty() && host.front() != '[')
        host = host.substr(0, host.rfind(':'));
    if (!has_space && looks_like_host(host))
        return accept("https://" + std::string{text});
    if (!allow_search)
        return refuse("That is not a web address.");
    Destination d;
    d.ok = true;
    d.is_search = true;
    d.url = search_url(engine, text);
    return d;
}
} // namespace

const char *engine_id(SearchEngine engine) noexcept
{
    switch (engine)
    {
    case SearchEngine::duckduckgo:
        return "duckduckgo";
    case SearchEngine::google:
        return "google";
    case SearchEngine::bing:
        return "bing";
    case SearchEngine::startpage:
        return "startpage";
    }
    return "duckduckgo";
}

const char *engine_name(SearchEngine engine) noexcept
{
    switch (engine)
    {
    case SearchEngine::duckduckgo:
        return "DuckDuckGo";
    case SearchEngine::google:
        return "Google";
    case SearchEngine::bing:
        return "Bing";
    case SearchEngine::startpage:
        return "Startpage";
    }
    return "DuckDuckGo";
}

SearchEngine engine_from_id(std::string_view id) noexcept
{
    for (int i = 0; i < kSearchEngineCount; ++i)
        if (id == engine_id(static_cast<SearchEngine>(i)))
            return static_cast<SearchEngine>(i);
    return SearchEngine::duckduckgo;
}

std::string search_url(SearchEngine engine, std::string_view query)
{
    const std::string q = url::encode_component(trim(query));
    switch (engine)
    {
    case SearchEngine::google:
        return "https://www.google.com/search?q=" + q;
    case SearchEngine::bing:
        return "https://www.bing.com/search?q=" + q;
    case SearchEngine::startpage:
        return "https://www.startpage.com/do/search?q=" + q;
    case SearchEngine::duckduckgo:
        break;
    }
    return "https://duckduckgo.com/?q=" + q;
}

Destination interpret(std::string_view typed, SearchEngine engine)
{
    return resolve_address(typed, true, engine);
}

Destination check_address(std::string_view text)
{
    return resolve_address(text, false, SearchEngine::duckduckgo);
}

std::string display_host(std::string_view address)
{
    const auto parsed = url::parse(address);
    if (!parsed)
        return std::string{address.substr(0, 60)};
    std::string host = parsed->host;
    for (const char *prefix : {"www.", "m."})
        if (host.starts_with(prefix) && host.size() > std::string_view{prefix}.size() + 3)
            host.erase(0, std::string_view{prefix}.size());
    return parsed->port > 0 ? host + ":" + std::to_string(parsed->port) : host;
}

std::string origin_of(std::string_view address)
{
    const auto parsed = url::parse(address);
    return parsed ? parsed->scheme + "://" + parsed->authority : std::string{};
}

bool is_loopback_host(std::string_view host) noexcept
{
    const std::string h = lower(host);
    if (h == "localhost" || h.ends_with(".localhost") || h == "0.0.0.0" || h == "0" ||
        h == "[::1]" || h == "::1" || h == "[::]")
        return true;
    return h.starts_with("127.") && is_ipv4(h);
}

bool valid_video_id(std::string_view id) noexcept
{
    return id.size() == 11 && std::all_of(id.begin(), id.end(),
                                          [](char c) {
                                              return std::isalnum(static_cast<unsigned char>(c)) ||
                                                     c == '-' || c == '_';
                                          });
}

bool valid_list_id(std::string_view id) noexcept
{
    return id.size() >= 2 && id.size() <= 64 &&
           std::all_of(
               id.begin(), id.end(), [](char c)
               { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'; });
}

int parse_start_time(std::string_view text) noexcept
{
    text = trim(text);
    if (text.empty() || text.size() > 16)
        return 0;
    long total = 0, number = 0;
    bool digits = false;
    for (char c : text)
    {
        if (c >= '0' && c <= '9')
        {
            number = number * 10 + (c - '0');
            digits = true;
            if (number > 1000000)
                return 0;
            continue;
        }
        if (!digits)
            return 0;
        switch (c)
        {
        case 'h':
            total += number * 3600;
            break;
        case 'm':
            total += number * 60;
            break;
        case 's':
            total += number;
            break;
        default:
            return 0;
        }
        number = 0;
        digits = false;
    }
    total += number;
    return total > 172800 ? 0 : static_cast<int>(total);
}

std::optional<YouTubeTarget> parse_youtube(std::string_view text)
{
    text = trim(text);
    if (valid_video_id(text))
        return YouTubeTarget{std::string{text}, {}, 0};
    std::string address{text};
    if (address.find("://") == std::string::npos)
        address = "https://" + address;
    const auto parsed = url::parse(address);
    if (!parsed)
        return std::nullopt;
    std::string host = parsed->host;
    for (const char *prefix : {"www.", "m.", "music."})
        if (host.starts_with(prefix))
            host.erase(0, std::string_view{prefix}.size());
    const bool short_link = host == "youtu.be";
    if (!short_link && host != "youtube.com" && host != "youtube-nocookie.com")
        return std::nullopt;

    std::map<std::string, std::string> query;
    std::string_view q = parsed->query;
    while (!q.empty())
    {
        const std::size_t amp = q.find('&');
        const std::string_view pair = q.substr(0, amp);
        const std::size_t eq = pair.find('=');
        if (eq != std::string_view::npos)
            query[std::string{pair.substr(0, eq)}] = url::decode_component(pair.substr(eq + 1));
        if (amp == std::string_view::npos)
            break;
        q.remove_prefix(amp + 1);
    }
    std::vector<std::string> segments;
    std::string_view path = parsed->path;
    while (!path.empty())
    {
        if (path.front() == '/')
        {
            path.remove_prefix(1);
            continue;
        }
        const std::size_t slash = path.find('/');
        segments.emplace_back(path.substr(0, slash));
        if (slash == std::string_view::npos)
            break;
        path.remove_prefix(slash);
    }

    YouTubeTarget target;
    if (short_link)
    {
        if (!segments.empty())
            target.video_id = segments[0];
    }
    else if (!segments.empty() && segments[0] == "watch")
    {
        target.video_id = query["v"];
    }
    else if (segments.size() >= 2 &&
             (segments[0] == "embed" || segments[0] == "shorts" || segments[0] == "live" ||
              segments[0] == "v" || segments[0] == "e"))
    {
        if (segments[1] != "videoseries")
            target.video_id = segments[1];
    }
    else if (segments.empty() || segments[0] != "playlist")
    {
        return std::nullopt;
    }
    target.list_id = query["list"];
    if (!target.video_id.empty() && !valid_video_id(target.video_id))
        return std::nullopt;
    if (!target.list_id.empty() && !valid_list_id(target.list_id))
        target.list_id.clear();
    if (target.video_id.empty() && target.list_id.empty())
        return std::nullopt;
    const std::string &t = query.count("t") ? query["t"] : query["start"];
    target.start_seconds = parse_start_time(t);
    return target;
}
} // namespace akeno::web
