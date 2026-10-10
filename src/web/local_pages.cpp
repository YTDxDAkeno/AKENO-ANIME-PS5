// AKENO STREAM PS5 - AKENO STREAM's own pages for the embedded browser.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "web/local_pages.hpp"

#include "core/fs.hpp"
#include "core/json.hpp"

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

namespace akeno::web
{
namespace
{
constexpr std::size_t kMaxHead = 16 * 1024;
constexpr std::size_t kMaxBody = 64 * 1024;
constexpr std::size_t kMaxEventBody = 4 * 1024;
constexpr std::size_t kMaxClients = 8;
constexpr std::uint64_t kIdleMs = 10000;
constexpr std::size_t kMaxMedia = 4u * 1024u * 1024u;
constexpr std::size_t kThreadStack = 256 * 1024;

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

// The pages may load the official YouTube player (www.youtube.com and its
// script and image hosts) and talk to this server; nothing else.
constexpr char kPageSecurity[] =
    "Content-Security-Policy: default-src 'none'; script-src 'self' https://www.youtube.com "
    "https://s.ytimg.com; style-src 'self' 'unsafe-inline'; img-src 'self' data: "
    "https://i.ytimg.com; media-src 'self' blob:; connect-src 'self'; frame-src "
    "https://www.youtube.com https://www.youtube-nocookie.com; base-uri 'none'; form-action "
    "'none'; frame-ancestors 'none'";

std::string random_token()
{
    unsigned char bytes[16] = {};
    if (RAND_bytes(bytes, sizeof(bytes)) != 1)
    {
        std::uint64_t seed = platform::monotonic_us() ^ 0x9E3779B97F4A7C15ULL;
        for (unsigned char &b : bytes)
        {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            b = static_cast<unsigned char>(seed >> 56);
        }
    }
    static constexpr char kHex[] = "0123456789abcdef";
    std::string token;
    for (unsigned char b : bytes)
    {
        token += kHex[b >> 4];
        token += kHex[b & 15];
    }
    return token;
}

std::string lower(std::string_view text)
{
    std::string out{text};
    for (char &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Value of a header in a request head ("" when absent).
std::string header(std::string_view head, std::string_view name)
{
    const std::string low = lower(head);
    const std::string key = "\r\n" + lower(name) + ":";
    const std::size_t at = low.find(key);
    if (at == std::string::npos)
        return {};
    std::size_t start = at + key.size();
    const std::size_t end = head.find("\r\n", start);
    while (start < head.size() && head[start] == ' ')
        ++start;
    std::string value{
        head.substr(start, (end == std::string_view::npos ? head.size() : end) - start)};
    while (!value.empty() && value.back() == ' ')
        value.pop_back();
    return value;
}

std::string clean(std::string_view text, std::size_t limit)
{
    std::string out;
    for (char c : text.substr(0, limit))
        out += static_cast<unsigned char>(c) < 0x20 || c == 0x7f ? ' ' : c;
    return out;
}

bool event_type_known(std::string_view type)
{
    static constexpr std::string_view kTypes[] = {
        "loaded",  "api",    "ready", "state",   "error",    "quality",
        "playing", "paused", "ended", "volume",  "autoplay", "fullscreen",
        "time",    "log",    "input", "storage", "playlist"};
    return std::find(std::begin(kTypes), std::end(kTypes), type) != std::end(kTypes);
}

const char *reason(int status)
{
    switch (status)
    {
    case 200:
        return "OK";
    case 204:
        return "No Content";
    case 206:
        return "Partial Content";
    case 400:
        return "Bad Request";
    case 403:
        return "Forbidden";
    case 405:
        return "Method Not Allowed";
    case 413:
        return "Payload Too Large";
    case 416:
        return "Range Not Satisfiable";
    default:
        return "Not Found";
    }
}

void send_all(int fd, const char *data, std::size_t size)
{
    std::size_t sent = 0;
    while (sent < size)
    {
        const ssize_t n = ::send(fd, data + sent, size - sent, kSendFlags);
        if (n <= 0)
            return;
        sent += static_cast<std::size_t>(n);
    }
}
} // namespace

LocalPages::~LocalPages()
{
    stop();
}

bool LocalPages::start(const std::string &media_file, std::string *error)
{
    stop();
    media_.clear();
    if (!media_file.empty())
        if (auto bytes = fs::read_bytes(media_file, kMaxMedia))
            media_.assign(bytes->begin(), bytes->end());
    listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener_ < 0)
    {
        if (error)
            *error = "could not open a local socket";
        return false;
    }
    int yes = 1;
    (void)::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    bool bound = false;
    // The same port each time keeps the pages' origin (and so their storage)
    // stable; a few neighbours, then any free port.
    for (int attempt = 0; attempt <= 8 && !bound; ++attempt)
    {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port =
            htons(attempt == 8 ? 0 : static_cast<std::uint16_t>(kPreferredPort + attempt));
        bound = ::bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0;
    }
    sockaddr_in actual{};
    socklen_t length = sizeof(actual);
    if (!bound || ::listen(listener_, 8) != 0 ||
        ::getsockname(listener_, reinterpret_cast<sockaddr *>(&actual), &length) != 0)
    {
        ::close(listener_);
        listener_ = -1;
        if (error)
            *error = "could not listen on the console's loopback address";
        return false;
    }
    port_ = ntohs(actual.sin_port);
    token_ = random_token();
    {
        std::lock_guard<std::mutex> guard(lock_);
        events_.clear();
        report_.reset();
        close_requested_ = false;
    }
    last_contact_.store(0);
    page_loaded_.store(false);
    rejected_.store(0);
    running_.store(true);
    if (!platform::start_thread(thread_, thread_entry, this, kThreadStack, "akeno-pages"))
    {
        running_.store(false);
        ::close(listener_);
        listener_ = -1;
        if (error)
            *error = "could not start the page server";
        return false;
    }
    return true;
}

void LocalPages::stop()
{
    running_.store(false);
    platform::join_thread(thread_);
    if (listener_ >= 0)
    {
        ::close(listener_);
        listener_ = -1;
    }
    token_.clear();
}

std::string LocalPages::page_url(std::string_view page, std::string_view query) const
{
    std::string out =
        "http://127.0.0.1:" + std::to_string(port_) + "/s/" + token_ + "/" + std::string{page};
    if (!query.empty())
        out += "?" + std::string{query};
    return out;
}

std::vector<PageEvent> LocalPages::take_events()
{
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<PageEvent> out;
    out.swap(events_);
    return out;
}

std::optional<std::string> LocalPages::take_report()
{
    std::lock_guard<std::mutex> guard(lock_);
    std::optional<std::string> out;
    out.swap(report_);
    return out;
}

bool LocalPages::take_close_request()
{
    std::lock_guard<std::mutex> guard(lock_);
    const bool requested = close_requested_;
    close_requested_ = false;
    return requested;
}

void *LocalPages::thread_entry(void *self)
{
    static_cast<LocalPages *>(self)->serve();
    return nullptr;
}

LocalPages::Reply LocalPages::respond_to(std::string_view request)
{
    Reply reply;
    const auto reject = [&](int status)
    {
        rejected_.fetch_add(1);
        reply.status = status;
        reply.body = reason(status);
        return reply;
    };
    const std::size_t head_end = request.find("\r\n\r\n");
    const std::size_t space = request.find(' ');
    const std::size_t space2 =
        space == std::string_view::npos ? space : request.find(' ', space + 1);
    if (head_end == std::string_view::npos || space2 == std::string_view::npos || space2 > head_end)
        return reject(400);
    const std::string_view head = request.substr(0, head_end);
    const std::string method{request.substr(0, space)};
    std::string_view target = request.substr(space + 1, space2 - space - 1);
    target = target.substr(0, target.find('?'));

    // DNS rebinding: a page on another name resolving to 127.0.0.1 would
    // send that name here.
    const std::string host = lower(header(head, "Host"));
    if (host != "127.0.0.1:" + std::to_string(port_) &&
        host != "localhost:" + std::to_string(port_))
        return reject(403);
    const std::string prefix = "/s/" + token_ + "/";
    if (token_.empty() || !target.starts_with(prefix))
        return reject(404);
    const std::string page{target.substr(prefix.size())};
    const std::string_view body = request.substr(std::min(request.size(), head_end + 4));
    last_contact_.store(platform::monotonic_us() / 1000);

    const auto page_reply = [&](std::string_view content, const char *type)
    {
        reply.status = 200;
        reply.content_type = type;
        reply.body = std::string{content};
        reply.headers = {kPageSecurity, "Referrer-Policy: strict-origin-when-cross-origin",
                         "X-Content-Type-Options: nosniff", "X-Frame-Options: DENY"};
        return reply;
    };
    if (method == "GET" || method == "HEAD")
    {
        if (page == "youtube")
            return page_reply(youtube_page_html(), "text/html; charset=utf-8");
        if (page == "captest")
            return page_reply(capability_page_html(), "text/html; charset=utf-8");
        if (page == "youtube.js" || page == "captest.js")
        {
            page_loaded_.store(true);
            return page_reply(page == "youtube.js" ? youtube_page_js() : capability_page_js(),
                              "text/javascript; charset=utf-8");
        }
        if (page == "akeno.css")
            return page_reply(pages_css(), "text/css; charset=utf-8");
        if (page == "ping")
        {
            reply.status = 204;
            return reply;
        }
        if (page == "test.mp4" && !media_.empty())
        {
            // Byte ranges: WebKit's media loader asks for them and gives up without.
            reply.content_type = "video/mp4";
            reply.headers = {"Accept-Ranges: bytes", "X-Content-Type-Options: nosniff"};
            std::size_t first = 0, last = media_.size() - 1;
            const std::string range = lower(header(head, "Range"));
            if (range.starts_with("bytes="))
            {
                char *end = nullptr;
                const unsigned long long a = std::strtoull(range.c_str() + 6, &end, 10);
                if (end == range.c_str() + 6 || *end != '-' || a >= media_.size())
                {
                    reply.status = 416;
                    reply.headers.push_back("Content-Range: bytes */" +
                                            std::to_string(media_.size()));
                    return reply;
                }
                first = static_cast<std::size_t>(a);
                if (end[1] >= '0' && end[1] <= '9')
                    last = std::min<std::size_t>(
                        last, static_cast<std::size_t>(std::strtoull(end + 1, nullptr, 10)));
                if (last < first)
                    return reject(416);
                reply.status = 206;
                reply.headers.push_back("Content-Range: bytes " + std::to_string(first) + "-" +
                                        std::to_string(last) + "/" + std::to_string(media_.size()));
            }
            else
            {
                reply.status = 200;
            }
            reply.body = media_.substr(first, last - first + 1);
            return reply;
        }
        if (page == "close")
        {
            std::lock_guard<std::mutex> guard(lock_);
            close_requested_ = true;
            reply.status = 204;
            return reply;
        }
        return reject(404);
    }
    if (method != "POST")
        return reject(405);
    if (page == "close")
    {
        std::lock_guard<std::mutex> guard(lock_);
        close_requested_ = true;
        reply.status = 204;
        return reply;
    }
    if (page == "event")
    {
        if (body.size() > kMaxEventBody)
            return reject(413);
        json::ParseLimits limits;
        limits.max_bytes = kMaxEventBody;
        limits.max_depth = 4;
        const auto parsed = json::parse(body, limits);
        const std::string type = parsed.value["type"].str();
        if (!parsed.ok || !event_type_known(type))
            return reject(400);
        const json::Value &value = parsed.value["value"];
        PageEvent event;
        event.type = type;
        event.value = clean(value.is_number() ? std::to_string(value.integer()) : value.str(), 64);
        event.detail = clean(parsed.value["detail"].str(), 200);
        {
            std::lock_guard<std::mutex> guard(lock_);
            if (events_.size() < kMaxEvents)
                events_.push_back(std::move(event));
        }
        reply.status = 204;
        return reply;
    }
    if (page == "report")
    {
        if (body.size() > kMaxBody)
            return reject(413);
        {
            std::lock_guard<std::mutex> guard(lock_);
            report_ = std::string{body};
        }
        reply.status = 204;
        return reply;
    }
    return reject(404);
}

void LocalPages::serve()
{
    struct Client
    {
        int fd;
        std::string in;
        std::uint64_t since_ms;
    };
    std::vector<Client> clients;
    const auto drop = [&](std::size_t i)
    {
        ::close(clients[i].fd);
        clients.erase(clients.begin() + static_cast<std::ptrdiff_t>(i));
    };
    while (running_.load())
    {
        if (rejected_.load() >= kMaxRejected)
            break; // something is hammering the server: stop answering
        std::vector<pollfd> fds;
        fds.push_back({listener_, POLLIN, 0});
        for (const Client &c : clients)
            fds.push_back({c.fd, POLLIN, 0});
        if (::poll(fds.data(), static_cast<nfds_t>(fds.size()), 200) < 0)
            continue;
        const std::uint64_t now = platform::monotonic_us() / 1000;
        for (std::size_t i = clients.size(); i-- > 0;)
        {
            const short revents = fds[i + 1].revents;
            if (!(revents & (POLLIN | POLLHUP | POLLERR)))
            {
                if (now - clients[i].since_ms > kIdleMs)
                    drop(i);
                continue;
            }
            char buffer[8192];
            const ssize_t got = ::recv(clients[i].fd, buffer, sizeof(buffer), 0);
            if (got <= 0)
            {
                drop(i);
                continue;
            }
            Client &c = clients[i];
            c.in.append(buffer, static_cast<std::size_t>(got));
            const std::size_t head_end = c.in.find("\r\n\r\n");
            if (head_end == std::string::npos)
            {
                if (c.in.size() > kMaxHead)
                {
                    rejected_.fetch_add(1);
                    drop(i);
                }
                continue;
            }
            const std::string length_text =
                header(std::string_view{c.in}.substr(0, head_end), "Content-Length");
            const std::size_t length =
                length_text.empty()
                    ? 0
                    : static_cast<std::size_t>(std::strtoull(length_text.c_str(), nullptr, 10));
            Reply reply;
            if (length > kMaxBody)
            {
                rejected_.fetch_add(1);
                reply.status = 413;
                reply.body = reason(413);
            }
            else if (c.in.size() < head_end + 4 + length)
            {
                continue; // the body is still arriving
            }
            else
            {
                reply = respond_to(std::string_view{c.in}.substr(0, head_end + 4 + length));
            }
            const bool head_only = c.in.starts_with("HEAD ");
            std::string out = "HTTP/1.1 " + std::to_string(reply.status) + " " +
                              reason(reply.status) + "\r\nContent-Type: " + reply.content_type +
                              "\r\nContent-Length: " + std::to_string(reply.body.size()) +
                              "\r\nCache-Control: no-store\r\nConnection: close\r\n";
            for (const std::string &h : reply.headers)
                out += h + "\r\n";
            out += "\r\n";
            if (!head_only)
                out += reply.body;
            timeval timeout{3, 0};
            (void)::setsockopt(c.fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
            send_all(c.fd, out.data(), out.size());
            drop(i);
        }
        if (fds[0].revents & POLLIN)
        {
            const int fd = ::accept(listener_, nullptr, nullptr);
            if (fd >= 0 && clients.size() < kMaxClients)
            {
#ifdef SO_NOSIGPIPE
                int yes = 1;
                (void)::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
                clients.push_back({fd, {}, now});
            }
            else if (fd >= 0)
            {
                ::close(fd);
            }
        }
    }
    for (const Client &c : clients)
        ::close(c.fd);
}
} // namespace akeno::web
