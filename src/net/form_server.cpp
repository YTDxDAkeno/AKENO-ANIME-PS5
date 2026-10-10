// AKENO STREAM PS5 - A one-page web form for adding sources from a phone.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/form_server.hpp"

#include "core/url.hpp"

#include <openssl/rand.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace akeno::net
{
namespace
{
constexpr std::size_t kMaxRequest = 16 * 1024;
constexpr std::size_t kMaxBody = 8 * 1024;
constexpr std::size_t kThreadStack = 256 * 1024;

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

std::string random_pin()
{
    static constexpr char kAlphabet[] = "abcdefghijkmnpqrstuvwxyz23456789";
    unsigned char bytes[8] = {};
    if (RAND_bytes(bytes, sizeof(bytes)) != 1)
    {
        // Fallback: the clock is a weak but non-constant source.
        std::uint64_t seed =
            platform::monotonic_us() * 6364136223846793005ULL + 1442695040888963407ULL;
        for (unsigned char &b : bytes)
        {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            b = static_cast<unsigned char>(seed >> 56);
        }
    }
    std::string pin;
    for (unsigned char b : bytes)
        pin += kAlphabet[b % (sizeof(kAlphabet) - 1)];
    return pin;
}

std::string escape_html(const std::string &text)
{
    std::string out;
    for (char c : text)
    {
        switch (c)
        {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&#39;";
            break;
        default:
            out += c;
        }
    }
    return out;
}

std::string page(const std::string &message, bool good)
{
    std::string html =
        "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<title>AKENO STREAM - Add a source</title><style>"
        "body{font-family:system-ui,sans-serif;background:#10141f;color:#eef1f8;margin:0;"
        "padding:24px}main{max-width:560px;margin:auto}h1{font-size:1.4em}"
        "label{display:block;margin:18px 0 6px;color:#a9b1c6}"
        "input{width:100%;box-sizing:border-box;font-size:1.05em;padding:12px;border-radius:10px;"
        "border:1px solid #39415a;background:#1a2030;color:#eef1f8}"
        "button{margin-top:22px;width:100%;font-size:1.1em;padding:14px;border:0;"
        "border-radius:12px;background:#2ec4d6;color:#06222a;font-weight:600}"
        ".note{color:#a9b1c6;font-size:.9em;margin-top:24px;line-height:1.4}"
        ".msg{padding:12px;border-radius:10px;margin-top:16px}"
        ".good{background:#14532d}.bad{background:#7f1d1d}</style></head><body><main>"
        "<h1>Add a source to AKENO STREAM</h1>";
    if (!message.empty())
        html += std::string{"<div class=\"msg "} + (good ? "good" : "bad") + "\">" +
                escape_html(message) + "</div>";
    html += "<form method=\"post\">"
            "<label for=\"url\">Address (M3U list, JSON feed, HLS or video file)</label>"
            "<input id=\"url\" name=\"url\" type=\"url\" required maxlength=\"2048\" "
            "placeholder=\"https://\" autocapitalize=\"off\" autocorrect=\"off\">"
            "<label for=\"name\">Name (optional)</label>"
            "<input id=\"name\" name=\"name\" maxlength=\"60\">"
            "<button type=\"submit\">Add to the PS5</button></form>"
            "<p class=\"note\">AKENO STREAM ships no sources. You decide what to add and are "
            "responsible for having the right to watch it. DRM-protected streams cannot be "
            "played, and web pages are not searched for videos.</p></main></body></html>";
    return html;
}

void send_all(int fd, const std::string &data)
{
    std::size_t sent = 0;
    while (sent < data.size())
    {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, kSendFlags);
        if (n <= 0)
            return;
        sent += static_cast<std::size_t>(n);
    }
}

void respond(int fd, int status, const std::string &body)
{
    const char *reason = status == 200 ? "OK" : status == 404 ? "Not Found" : "Bad Request";
    std::string head = "HTTP/1.1 " + std::to_string(status) + " " + reason +
                       "\r\nContent-Type: text/html; charset=utf-8\r\n"
                       "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
                       "Content-Length: " +
                       std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
    send_all(fd, head + body);
}

std::string trim(const std::string &text)
{
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};
    return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
}
} // namespace

std::vector<std::pair<std::string, std::string>> parse_form(const std::string &body)
{
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t at = 0;
    while (at <= body.size() && out.size() < 16)
    {
        std::size_t end = body.find('&', at);
        if (end == std::string::npos)
            end = body.size();
        std::string pair = body.substr(at, end - at);
        std::replace(pair.begin(), pair.end(), '+', ' ');
        const std::size_t eq = pair.find('=');
        if (!pair.empty())
            out.emplace_back(url::decode_component(pair.substr(0, eq)),
                             eq == std::string::npos ? std::string{}
                                                     : url::decode_component(pair.substr(eq + 1)));
        at = end + 1;
    }
    return out;
}

std::string local_ipv4()
{
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return {};
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(9);
    target.sin_addr.s_addr = htonl(0xC0000201u); // 192.0.2.1 (TEST-NET): routing only, no traffic
    sockaddr_in local{};
    socklen_t length = sizeof(local);
    std::string out;
    if (::connect(fd, reinterpret_cast<sockaddr *>(&target), sizeof(target)) == 0 &&
        ::getsockname(fd, reinterpret_cast<sockaddr *>(&local), &length) == 0)
    {
        const std::uint32_t a = ntohl(local.sin_addr.s_addr);
        if (a != 0)
        {
            char text[20];
            std::snprintf(text, sizeof(text), "%u.%u.%u.%u", a >> 24, (a >> 16) & 255u,
                          (a >> 8) & 255u, a & 255u);
            out = text;
        }
    }
    ::close(fd);
    return out;
}

FormServer::~FormServer()
{
    stop();
}

bool FormServer::start(std::uint16_t port, std::string *error)
{
    stop();
    listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener_ < 0)
    {
        if (error)
            *error = "could not open a network socket";
        return false;
    }
    int yes = 1;
    (void)::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    bool bound = false;
    for (int attempt = 0; attempt < (port ? 10 : 1) && !bound; ++attempt)
    {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(static_cast<std::uint16_t>(port ? port + attempt : 0));
        bound = ::bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0;
    }
    sockaddr_in actual{};
    socklen_t length = sizeof(actual);
    if (!bound || ::listen(listener_, 4) != 0 ||
        ::getsockname(listener_, reinterpret_cast<sockaddr *>(&actual), &length) != 0)
    {
        ::close(listener_);
        listener_ = -1;
        if (error)
            *error = "could not listen on the local network";
        return false;
    }
    port_ = ntohs(actual.sin_port);
    pin_ = random_pin();
    bad_requests_ = 0;
    accepted_.store(0);
    running_.store(true);
    if (!platform::start_thread(thread_, thread_entry, this, kThreadStack, "akeno-form"))
    {
        running_.store(false);
        ::close(listener_);
        listener_ = -1;
        if (error)
            *error = "could not start the server thread";
        return false;
    }
    return true;
}

void FormServer::stop()
{
    running_.store(false);
    platform::join_thread(thread_);
    if (listener_ >= 0)
    {
        ::close(listener_);
        listener_ = -1;
    }
}

std::vector<FormServer::Submission> FormServer::take()
{
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<Submission> out;
    out.swap(pending_);
    return out;
}

void *FormServer::thread_entry(void *self)
{
    static_cast<FormServer *>(self)->serve();
    return nullptr;
}

void FormServer::serve()
{
    while (running_.load())
    {
        pollfd waiting{listener_, POLLIN, 0};
        if (::poll(&waiting, 1, 200) <= 0 || !(waiting.revents & POLLIN))
            continue;
        const int client = ::accept(listener_, nullptr, nullptr);
        if (client < 0)
            continue;
#ifdef SO_NOSIGPIPE
        int yes = 1;
        (void)::setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
        timeval timeout{3, 0};
        (void)::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        (void)::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        handle(client);
        ::close(client);
        if (bad_requests_ >= kMaxBadRequests || accepted_.load() >= kMaxSubmissions)
            running_.store(false);
    }
}

void FormServer::handle(int client)
{
    std::string request;
    char buffer[4096];
    std::size_t header_end = std::string::npos;
    while (request.size() < kMaxRequest)
    {
        const ssize_t got = ::recv(client, buffer, sizeof(buffer), 0);
        if (got <= 0)
            break;
        request.append(buffer, static_cast<std::size_t>(got));
        header_end = request.find("\r\n\r\n");
        if (header_end != std::string::npos)
            break;
    }
    if (header_end == std::string::npos)
    {
        ++bad_requests_;
        respond(client, 400, page("The request could not be read.", false));
        return;
    }
    const std::size_t space = request.find(' ');
    const std::size_t space2 = request.find(' ', space + 1);
    if (space == std::string::npos || space2 == std::string::npos)
    {
        ++bad_requests_;
        respond(client, 400, page("The request could not be read.", false));
        return;
    }
    const std::string method = request.substr(0, space);
    std::string path = request.substr(space + 1, space2 - space - 1);
    path = path.substr(0, path.find('?'));
    if (path == "/favicon.ico")
    {
        respond(client, 404, "");
        return;
    }
    if (path != "/" + pin_ && path != "/" + pin_ + "/")
    {
        ++bad_requests_;
        respond(client, 404, "<!doctype html><title>Not found</title><p>Not found.</p>");
        return;
    }
    if (method == "GET")
    {
        respond(client, 200, page({}, true));
        return;
    }
    if (method != "POST")
    {
        ++bad_requests_;
        respond(client, 400, page("Unsupported request.", false));
        return;
    }
    // Content-Length (case-insensitive header name).
    std::size_t length = 0;
    {
        std::string headers = request.substr(0, header_end);
        for (char &c : headers)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::size_t at = headers.find("\r\ncontent-length:");
        if (at != std::string::npos)
            length =
                static_cast<std::size_t>(std::strtoull(headers.c_str() + at + 17, nullptr, 10));
    }
    if (length > kMaxBody)
    {
        ++bad_requests_;
        respond(client, 400, page("That is too long.", false));
        return;
    }
    std::string body = request.substr(header_end + 4);
    while (body.size() < length)
    {
        const ssize_t got = ::recv(client, buffer, sizeof(buffer), 0);
        if (got <= 0)
            break;
        body.append(buffer, static_cast<std::size_t>(got));
    }
    body.resize(std::min(body.size(), length));
    std::string name, address;
    for (const auto &[key, value] : parse_form(body))
    {
        if (key == "name")
            name = trim(value).substr(0, 60);
        else if (key == "url")
            address = trim(value);
    }
    const auto parsed = url::parse(address);
    if (address.size() > 2048 || !parsed || !parsed->is_http() || parsed->host.empty())
    {
        respond(client, 200,
                page("Please enter an address that starts with http:// or https://.", false));
        return;
    }
    if (accepted_.load() >= kMaxSubmissions)
    {
        respond(client, 200,
                page("Enough for now - close and reopen the screen on the PS5.", false));
        return;
    }
    {
        std::lock_guard<std::mutex> guard(lock_);
        pending_.push_back({name.empty() ? parsed->host : name, address});
    }
    accepted_.fetch_add(1);
    respond(client, 200,
            page("Added \"" + (name.empty() ? parsed->host : name) +
                     "\". It is now in Sources "
                     "on your PS5. Add another one or close this page.",
                 true));
}
} // namespace akeno::net
