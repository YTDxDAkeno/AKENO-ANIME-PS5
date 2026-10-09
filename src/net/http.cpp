// AKENO STREAM PS5 - HTTP(S) client on libcurl.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/http.hpp"

#include "core/url.hpp"
#include "platform/platform.hpp"

#include <curl/curl.h>

#include <cstdio>
#include <cstring>
#include <mutex>

namespace akeno::net
{
namespace
{
std::mutex g_transport_lock;
Transport g_transport;

struct Transfer
{
    const Request *request;
    Response *response;
    bool overflow = false;
    bool sink_stopped = false;
};

std::size_t on_body(char *data, std::size_t size, std::size_t count, void *context)
{
    auto &t = *static_cast<Transfer *>(context);
    if (size != 0 && count > static_cast<std::size_t>(-1) / size)
        return 0;
    const std::size_t bytes = size * count;
    if (t.response->bytes + bytes > t.request->max_bytes)
    {
        t.overflow = true;
        return 0;
    }
    t.response->bytes += bytes;
    if (t.request->on_data)
    {
        if (!t.request->on_data(reinterpret_cast<const std::uint8_t *>(data), bytes))
        {
            t.sink_stopped = true;
            return 0;
        }
        return bytes;
    }
    t.response->body.append(data, bytes);
    return bytes;
}

int on_progress(void *context, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    const auto &t = *static_cast<Transfer *>(context);
    return t.request->cancel && t.request->cancel->load(std::memory_order_relaxed) ? 1 : 0;
}
} // namespace

const char *outcome_name(Outcome outcome) noexcept
{
    switch (outcome)
    {
    case Outcome::ok:
        return "ok";
    case Outcome::http_error:
        return "HTTP error";
    case Outcome::network_error:
        return "network error";
    case Outcome::too_large:
        return "response too large";
    case Outcome::cancelled:
        return "cancelled";
    case Outcome::invalid_url:
        return "invalid URL";
    }
    return "unknown";
}

std::string Response::describe() const
{
    char text[320];
    switch (outcome)
    {
    case Outcome::ok:
        std::snprintf(text, sizeof(text), "HTTP %ld, %zu bytes in %u ms", status, bytes,
                      elapsed_ms);
        break;
    case Outcome::http_error:
        std::snprintf(text, sizeof(text), "HTTP %ld", status);
        break;
    default:
        std::snprintf(text, sizeof(text), "%s%s%s (curl %d)", outcome_name(outcome),
                      error.empty() ? "" : ": ", error.c_str(), curl_code);
        break;
    }
    return text;
}

void Client::global_init()
{
    static bool done = false;
    if (!done)
    {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        done = true;
    }
}

std::string Client::user_agent()
{
    return "AkenoStream/1.0 (PlayStation 5 homebrew; "
           "+https://github.com/YTDxDAkeno/AKENO-ANIME-PS5)";
}

std::string Client::library_version()
{
    return curl_version();
}

Client::Client() : easy_{curl_easy_init()}
{
}

Client::~Client()
{
    if (easy_)
        curl_easy_cleanup(static_cast<CURL *>(easy_));
}

void set_test_transport(Transport transport)
{
    std::lock_guard<std::mutex> guard(g_transport_lock);
    g_transport = std::move(transport);
}

Response Client::perform(const Request &request)
{
    {
        Transport transport;
        {
            std::lock_guard<std::mutex> guard(g_transport_lock);
            transport = g_transport;
        }
        if (transport)
        {
            Response r = transport(request);
            r.bytes = r.body.size();
            if (request.on_data && r.outcome == Outcome::ok)
            {
                if (!request.on_data(reinterpret_cast<const std::uint8_t *>(r.body.data()),
                                     r.body.size()))
                    r.outcome = Outcome::cancelled;
                r.body.clear();
            }
            return r;
        }
    }
    Response response;
    const std::uint64_t started = platform::monotonic_us();
    const auto parsed = url::parse(request.url);
    if (!parsed)
    {
        response.outcome = Outcome::invalid_url;
        response.error = "only absolute http(s) URLs are allowed";
        return response;
    }
    CURL *easy = static_cast<CURL *>(easy_);
    if (!easy)
    {
        response.error = "curl_easy_init failed";
        return response;
    }
    curl_easy_reset(easy);
    platform::configure_curl(easy);

    Transfer transfer{&request, &response};
    char error_buffer[CURL_ERROR_SIZE] = {};
    curl_slist *headers = nullptr;
    for (const std::string &h : request.headers)
        headers = curl_slist_append(headers, h.c_str());

    const bool secure = parsed->is_https();
    curl_easy_setopt(easy, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS_STR, secure ? "https" : "http,https");
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, request.follow_redirects ? 1L : 0L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, request.connect_timeout_ms);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, request.total_timeout_ms);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, request.low_speed_bytes);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, request.low_speed_seconds);
    curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(easy, CURLOPT_USERAGENT, user_agent().c_str());
    curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, error_buffer);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, on_body);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, on_progress);
    curl_easy_setopt(easy, CURLOPT_XFERINFODATA, &transfer);
    if (headers)
        curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);
    if (request.method == "POST")
    {
        curl_easy_setopt(easy, CURLOPT_POST, 1L);
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
        curl_easy_setopt(easy, CURLOPT_COPYPOSTFIELDS, request.body.c_str());
    }

    const CURLcode code = curl_easy_perform(easy);
    long status = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
    char *content_type = nullptr;
    if (curl_easy_getinfo(easy, CURLINFO_CONTENT_TYPE, &content_type) == CURLE_OK && content_type)
        response.content_type = content_type;
    char *effective = nullptr;
    if (curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &effective) == CURLE_OK && effective)
        response.final_url = effective;
    curl_slist_free_all(headers);
    // The handle keeps pointers to these until the next reset.
    curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, nullptr);
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, nullptr);

    response.status = status;
    response.curl_code = static_cast<int>(code);
    response.elapsed_ms = static_cast<std::uint32_t>((platform::monotonic_us() - started) / 1000u);
    if (request.cancel && request.cancel->load())
    {
        response.outcome = Outcome::cancelled;
        response.error = "cancelled";
    }
    else if (transfer.overflow)
    {
        response.outcome = Outcome::too_large;
        response.error = "response exceeded " + std::to_string(request.max_bytes) + " bytes";
    }
    else if (transfer.sink_stopped)
    {
        response.outcome = Outcome::cancelled;
        response.error = "stopped by consumer";
    }
    else if (code != CURLE_OK)
    {
        response.outcome = Outcome::network_error;
        response.error = error_buffer[0] ? error_buffer : curl_easy_strerror(code);
    }
    else if (status < 200 || status > 299)
    {
        response.outcome = Outcome::http_error;
        response.error = "HTTP " + std::to_string(status);
    }
    else
    {
        response.outcome = Outcome::ok;
    }
    return response;
}

Response get(const std::string &url, const CancelFlag &cancel, std::size_t max_bytes,
             std::vector<std::string> headers)
{
    Client client;
    Request request;
    request.url = url;
    request.cancel = cancel;
    request.max_bytes = max_bytes;
    request.headers = std::move(headers);
    return client.perform(request);
}
} // namespace akeno::net
