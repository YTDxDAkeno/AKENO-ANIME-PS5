// AKENO STREAM PS5 - A quick look at a website before the browser opens it.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "web/site_probe.hpp"

#include "core/url.hpp"
#include "providers/provider.hpp"
#include "web/address.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace akeno::web
{
namespace
{
constexpr std::size_t kMaxPage = 384 * 1024;

std::string lower(std::string_view text)
{
    std::string out{text};
    for (char &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Attributes of one tag: <link rel="icon" href='/a.png' sizes=32x32>.
std::vector<std::pair<std::string, std::string>> attributes(std::string_view tag)
{
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t i = 0;
    while (i < tag.size() && out.size() < 16)
    {
        while (i < tag.size() &&
               (std::isspace(static_cast<unsigned char>(tag[i])) || tag[i] == '/'))
            ++i;
        const std::size_t name_start = i;
        while (i < tag.size() && !std::isspace(static_cast<unsigned char>(tag[i])) &&
               tag[i] != '=' && tag[i] != '/')
            ++i;
        std::string name = lower(tag.substr(name_start, i - name_start));
        if (name.empty())
        {
            ++i;
            continue;
        }
        while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i])))
            ++i;
        std::string value;
        if (i < tag.size() && tag[i] == '=')
        {
            ++i;
            while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i])))
                ++i;
            if (i < tag.size() && (tag[i] == '"' || tag[i] == '\''))
            {
                const char quote = tag[i++];
                const std::size_t end = tag.find(quote, i);
                value = std::string{
                    tag.substr(i, end == std::string_view::npos ? tag.size() - i : end - i)};
                i = end == std::string_view::npos ? tag.size() : end + 1;
            }
            else
            {
                const std::size_t start = i;
                while (i < tag.size() && !std::isspace(static_cast<unsigned char>(tag[i])))
                    ++i;
                value = std::string{tag.substr(start, i - start)};
            }
        }
        out.emplace_back(std::move(name), std::move(value));
    }
    return out;
}

int icon_score(const std::string &rel, const std::string &href, const std::string &sizes,
               const std::string &type)
{
    const std::string h = lower(href);
    if (type.find("svg") != std::string::npos || h.ends_with(".svg") || h.starts_with("data:"))
        return -1; // not decodable here
    int score = 0;
    if (rel.find("apple-touch-icon") != std::string::npos)
        score = 400;
    else if (rel == "icon" || rel == "shortcut icon" || rel.find(" icon") != std::string::npos ||
             rel.starts_with("icon "))
        score = 100;
    else
        return -1;
    const int size = std::atoi(sizes.c_str()); // "192x192" -> 192
    if (size > 0)
        score += size >= 64 && size <= 512 ? size : std::min(size, 64);
    else if (h.find(".ico") != std::string::npos)
        score += 16;
    else
        score += 32;
    return score;
}
} // namespace

std::string find_title(std::string_view html)
{
    const std::string low = lower(html.substr(0, std::min<std::size_t>(html.size(), kMaxPage)));
    const std::size_t open = low.find("<title");
    if (open == std::string::npos)
        return {};
    const std::size_t start = low.find('>', open);
    const std::size_t end = low.find("</title", start == std::string::npos ? open : start);
    if (start == std::string::npos || end == std::string::npos || end <= start)
        return {};
    std::string title = strip_html(std::string{html.substr(start + 1, end - start - 1)});
    for (char &c : title)
        if (static_cast<unsigned char>(c) < 0x20)
            c = ' ';
    while (!title.empty() && title.front() == ' ')
        title.erase(0, 1);
    while (!title.empty() && title.back() == ' ')
        title.pop_back();
    return title.substr(0, 120);
}

std::string find_icon(std::string_view html, const std::string &page_url)
{
    const std::string low = lower(html.substr(0, std::min<std::size_t>(html.size(), kMaxPage)));
    std::string best;
    int best_score = -1;
    for (std::size_t at = low.find("<link"); at != std::string::npos;
         at = low.find("<link", at + 5))
    {
        const std::size_t end = low.find('>', at);
        if (end == std::string::npos)
            break;
        std::string rel, href, sizes, type;
        for (const auto &[name, value] : attributes(html.substr(at + 5, end - at - 5)))
        {
            if (name == "rel")
                rel = lower(value);
            else if (name == "href")
                href = value;
            else if (name == "sizes")
                sizes = lower(value);
            else if (name == "type")
                type = lower(value);
        }
        const int score = icon_score(rel, href, sizes, type);
        if (score <= best_score)
            continue;
        const auto resolved = url::resolve(page_url, href);
        if (!resolved || !check_address(*resolved).ok)
            continue;
        best = *resolved;
        best_score = score;
    }
    if (!best.empty())
        return best;
    const std::string origin = origin_of(page_url);
    return origin.empty() ? std::string{} : origin + "/favicon.ico";
}

std::string find_meta(std::string_view html, std::initializer_list<std::string_view> names)
{
    const std::string low = lower(html.substr(0, std::min<std::size_t>(html.size(), kMaxPage)));
    for (std::string_view wanted : names)
        for (std::size_t at = low.find("<meta"); at != std::string::npos;
             at = low.find("<meta", at + 5))
        {
            const std::size_t end = low.find('>', at);
            if (end == std::string::npos)
                break;
            std::string key, content;
            for (const auto &[name, value] : attributes(html.substr(at + 5, end - at - 5)))
            {
                if (name == "name" || name == "property")
                    key = lower(value);
                else if (name == "content")
                    content = value;
            }
            if (key != wanted)
                continue;
            std::string text = strip_html(content);
            for (char &c : text)
                if (static_cast<unsigned char>(c) < 0x20)
                    c = ' ';
            while (!text.empty() && text.front() == ' ')
                text.erase(0, 1);
            while (!text.empty() && text.back() == ' ')
                text.pop_back();
            if (!text.empty())
                return text.substr(0, 120);
        }
    return {};
}

std::string normalise_color(std::string_view text)
{
    std::string c = lower(text);
    while (!c.empty() && c.back() == ' ')
        c.pop_back();
    while (!c.empty() && c.front() == ' ')
        c.erase(0, 1);
    const auto hex = [](std::string_view s)
    {
        return std::all_of(s.begin(), s.end(), [](char ch)
                           { return std::isxdigit(static_cast<unsigned char>(ch)) != 0; });
    };
    if (c.size() == 4 && c[0] == '#' && hex(c.substr(1)))
        return std::string{"#"} + c[1] + c[1] + c[2] + c[2] + c[3] + c[3];
    if (c.size() == 7 && c[0] == '#' && hex(c.substr(1)))
        return c;
    return {};
}

std::string describe_failure(const net::Response &r, bool *blocking)
{
    if (blocking)
        *blocking = r.outcome != net::Outcome::http_error && r.outcome != net::Outcome::ok;
    if (r.outcome == net::Outcome::http_error)
    {
        const std::string code = "HTTP " + std::to_string(r.status);
        if (r.status == 401 || r.status == 403)
            return "The site answered " + code +
                   ". Some sites refuse apps that are not browsers; the browser may still open "
                   "it.";
        if (r.status == 404)
            return "The page was not found (" + code + ").";
        if (r.status == 429)
            return "The site asks to slow down (" + code + ").";
        if (r.status == 451)
            return "The site is unavailable for legal reasons in your region (" + code + ").";
        if (r.status >= 500)
            return "The site has a server problem (" + code + ").";
        return "The site answered " + code + ".";
    }
    switch (r.curl_code)
    {
    case 6:
        return "The site's name could not be found (DNS). Check the address and the "
               "network.";
    case 7:
        return "The site could not be reached (connection refused or no route).";
    case 28:
        return "The site did not answer in time.";
    case 35:
        return "A secure connection (TLS) to the site could not be set up.";
    case 47:
        return "The site redirects too often.";
    case 51:
    case 58:
    case 60:
    case 77:
    case 83:
    case 90:
    case 91:
        return "The site's security certificate could not be verified.";
    default:
        break;
    }
    if (r.outcome == net::Outcome::invalid_url)
        return "The site redirected to an address that cannot be opened (for example from "
               "https to unencrypted http).";
    return "The site could not be reached: " + r.describe();
}

SiteProbe probe_site(const std::string &address, const net::CancelFlag &cancel)
{
    SiteProbe probe;
    std::string body;
    bool truncated = false;
    net::Client client;
    net::Request request;
    request.url = address;
    request.cancel = cancel;
    request.connect_timeout_ms = 6000;
    request.total_timeout_ms = 12000;
    request.max_bytes = 64u * 1024u * 1024u; // the sink stops after kMaxPage
    request.headers = {"Accept: text/html,application/xhtml+xml;q=0.9,*/*;q=0.5"};
    request.on_data = [&](const std::uint8_t *data, std::size_t size)
    {
        const std::size_t room = kMaxPage - body.size();
        body.append(reinterpret_cast<const char *>(data), std::min(size, room));
        if (body.size() >= kMaxPage)
        {
            truncated = true; // enough for the <head>
            return false;
        }
        return true;
    };
    net::Response r = client.perform(request);
    if (truncated)
    {
        // Stopping after the <head> is not a failure: judge by the status.
        r.status = r.status ? r.status : r.head.status;
        r.outcome = r.status >= 200 && r.status < 300 ? net::Outcome::ok : net::Outcome::http_error;
    }
    probe.final_url = r.final_url.empty() ? address : r.final_url;
    probe.status = r.status;
    probe.detail = truncated ? "HTTP " + std::to_string(r.status) + ", first " +
                                   std::to_string(body.size() / 1024) + " KiB read"
                             : r.describe();
    probe.reached = r.ok() || r.outcome == net::Outcome::http_error;
    if (!r.ok())
        probe.problem = describe_failure(r, &probe.blocking);
    if (body.empty() && !r.body.empty())
        body = r.body.substr(0, kMaxPage);
    if (!body.empty())
    {
        probe.title = find_title(body);
        probe.icon_url = find_icon(body, probe.final_url);
        probe.site_name =
            find_meta(body, {"og:site_name", "application-name", "apple-mobile-web-app-title"});
        probe.theme_color = normalise_color(find_meta(body, {"theme-color"}));
    }
    else if (probe.reached)
    {
        probe.icon_url = find_icon({}, probe.final_url);
    }
    return probe;
}

namespace
{
bool media_extension(const std::string &url_text)
{
    // The HTML src attribute can be relative, and may contain a query string.
    const std::size_t query = url_text.find_first_of("?#");
    const std::string path = lower(url_text.substr(0, query));
    for (const char *extension : {".mp4", ".m4v", ".mov", ".mkv", ".ts", ".m3u8"})
        if (path.ends_with(extension))
            return true;
    return false;
}

bool video_media_type(std::string_view type)
{
    const std::string value = lower(type);
    return value.starts_with("video/") || value.find("mpegurl") != std::string::npos;
}
} // namespace

std::vector<PublicVideo> public_videos_from_html(std::string_view html, const std::string &page_url)
{
    std::vector<PublicVideo> out;
    if (!url::parse(page_url))
        return out;
    html = html.substr(0, std::min<std::size_t>(html.size(), kMaxPage));
    const std::string low = lower(html);
    std::size_t at = 0;
    while (at < low.size() && out.size() < 16)
    {
        at = low.find('<', at);
        if (at == std::string::npos)
            break;
        const std::size_t begin = at + 1;
        std::size_t tag_end = begin;
        while (tag_end < low.size() &&
               (std::isalnum(static_cast<unsigned char>(low[tag_end])) || low[tag_end] == '-'))
            ++tag_end;
        const std::string tag = low.substr(begin, tag_end - begin);
        at = tag_end;
        if (tag != "video" && tag != "source" && tag != "meta")
            continue;
        const std::size_t end = low.find('>', tag_end);
        if (end == std::string::npos || end - tag_end > 4096)
            continue;
        at = end + 1;
        std::string media, type, property;
        for (const auto &[name, value] : attributes(html.substr(tag_end, end - tag_end)))
        {
            if (name == "src" && tag != "meta")
                media = value;
            else if (name == "content" && tag == "meta")
                media = value;
            else if (name == "type")
                type = value;
            else if (name == "property" || name == "name")
                property = lower(value);
        }
        if (tag == "meta" && property != "og:video" && property != "og:video:url" &&
            property != "og:video:secure_url" && property != "twitter:player:stream")
            continue;
        if (tag != "video" && !video_media_type(type) && !media_extension(media))
            continue;
        if (media.empty() || media.size() > 2048)
            continue;
        // HTML attributes can encode '&' as '&amp;'; canonicalize only this
        // normal ampersand escape before ordinary URL resolution.
        for (std::size_t p = 0; (p = media.find("&amp;", p)) != std::string::npos;)
        {
            media.replace(p, 5, "&");
            ++p;
        }
        const auto resolved = url::resolve(page_url, media);
        if (!resolved)
            continue;
        const auto address = check_address(*resolved);
        if (!address.ok)
            continue;
        if (tag == "meta" && !media_extension(address.url) && !video_media_type(type))
            continue;
        const bool duplicate = std::any_of(out.begin(), out.end(), [&](const PublicVideo &v)
                                           { return v.url == address.url; });
        if (!duplicate)
            out.push_back({address.url, tag == "video"    ? "HTML video"
                                        : tag == "source" ? "HTML source"
                                                          : "Public video metadata"});
    }
    return out;
}

PublicVideos probe_public_videos(const std::string &page_url, const net::CancelFlag &cancel)
{
    PublicVideos result;
    const auto destination = check_address(page_url);
    if (!destination.ok)
    {
        result.message = destination.error;
        return result;
    }
    net::Request request;
    request.url = destination.url;
    request.cancel = cancel;
    request.max_bytes = kMaxPage;
    request.connect_timeout_ms = 6000;
    request.total_timeout_ms = 12000;
    request.headers = {"Accept: text/html,application/xhtml+xml"};
    net::Client client;
    const net::Response response = client.perform(request);
    result.http_status = response.status;
    if (!response.ok())
    {
        result.message =
            "Could not read public page metadata: " + describe_failure(response, nullptr);
        return result;
    }
    if (!response.content_type.empty() &&
        lower(response.content_type).find("text/html") == std::string::npos &&
        lower(response.content_type).find("application/xhtml+xml") == std::string::npos)
    {
        result.message =
            "The address did not return an HTML page. Direct media URLs belong in Sources.";
        return result;
    }
    result.videos = public_videos_from_html(
        response.body, response.final_url.empty() ? page_url : response.final_url);
    result.message = result.videos.empty()
                         ? "No publicly declared playable media was found. Dynamic players, "
                           "account-only media and DRM streams cannot be imported."
                         : std::to_string(result.videos.size()) +
                               " public media address(es) found. "
                               "Playback still depends on the source and its permissions.";
    return result;
}

} // namespace akeno::web
