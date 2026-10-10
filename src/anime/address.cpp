// AKENO STREAM PS5 - Provider addresses as the user types them and as they are compared.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "anime/address.hpp"

#include "core/url.hpp"

#include <cctype>

namespace akeno::anime
{
namespace
{
std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

// The parts that identify a provider, scheme left out.
std::string identity(const url::Url &u)
{
    std::string host = u.host;
    if (host.rfind("www.", 0) == 0)
        host.erase(0, 4);
    const int default_port = u.is_https() ? 443 : 80;
    std::string out = host;
    if (u.port > 0 && u.port != default_port)
        out += ":" + std::to_string(u.port);
    std::string path = u.path.empty() ? "/" : u.path;
    while (path.size() > 1 && path.back() == '/')
        path.pop_back();
    out += path;
    if (u.has_query && !u.query.empty())
        out += "?" + u.query;
    return out;
}
} // namespace

SiteAddress normalize_site_address(std::string_view typed)
{
    SiteAddress out;
    std::string_view text = trim(typed);
    if (text.empty() || text == "https://" || text == "http://")
    {
        out.why = "Enter the website's address, for example https://video.blender.org";
        return out;
    }
    if (text.size() > kMaxAddressLength)
    {
        out.why = "The address is too long.";
        return out;
    }
    std::string candidate{text};
    if (candidate.find("://") == std::string::npos)
        candidate = "https://" + candidate;
    const auto u = url::parse(candidate);
    if (!u || !u->is_http())
    {
        out.why = candidate.find('@') != std::string::npos
                      ? "Addresses with a user name or password are not accepted."
                      : "That is not an http(s) web address.";
        return out;
    }
    if (u->host.empty() || u->host.find('.') == std::string::npos)
    {
        // "localhost" and bare names only make sense on a test network.
        if (u->host != "localhost")
        {
            out.why = "Enter a full address such as example.com.";
            return out;
        }
    }
    const int default_port = u->is_https() ? 443 : 80;
    out.url = u->scheme + "://" + u->host;
    if (u->port > 0 && u->port != default_port)
        out.url += ":" + std::to_string(u->port);
    out.url += u->path.empty() ? "/" : u->path;
    if (u->has_query)
        out.url += "?" + u->query;
    out.ok = true;
    return out;
}

bool same_site_address(std::string_view a, std::string_view b)
{
    const auto ua = url::parse(a);
    const auto ub = url::parse(b);
    if (!ua || !ub)
        return !a.empty() && a == b;
    return identity(*ua) == identity(*ub);
}
} // namespace akeno::anime
