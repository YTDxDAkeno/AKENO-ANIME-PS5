// AKENO STREAM PS5 - Checks that an episode's resource is real, DRM-free media.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "anime/probe.hpp"

#include "core/fs.hpp"
#include "core/url.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include <fcntl.h>
#include <unistd.h>

namespace akeno::anime
{
namespace
{
constexpr std::size_t kProbeBytes = 64 * 1024;

std::string lower(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool contains(std::string_view hay, std::string_view needle)
{
    return hay.find(needle) != std::string_view::npos;
}

ProbeResult result(Availability a, std::string detail, std::string container = {})
{
    return {a, std::move(detail), std::move(container)};
}

// The playlist's encryption: plain AES-128 segments play; anything else is DRM.
ProbeResult judge_playlist(std::string_view text)
{
    const std::string l = lower(text.substr(0, kProbeBytes));
    if (contains(l, "method=sample-aes") ||
        contains(l, "keyformat=\"com.apple.streamingkeydelivery") || contains(l, "widevine") ||
        contains(l, "playready") || contains(l, "skd://"))
        return result(Availability::drm_unsupported,
                      "The playlist is protected with DRM (SAMPLE-AES / a DRM key system).", "HLS");
    if (contains(l, "#ext-x-stream-inf") || contains(l, "#extinf"))
        return result(Availability::playable, "HLS playlist without DRM.", "HLS");
    return result(Availability::provider_unsupported, "An empty or unusual HLS playlist.", "HLS");
}
} // namespace

ProbeResult judge_media(std::string_view b, std::string_view content_type)
{
    const auto byte = [&](std::size_t i) -> unsigned
    { return i < b.size() ? static_cast<unsigned char>(b[i]) : 0u; };
    std::string_view head = b;
    if (head.substr(0, 3) == "\xEF\xBB\xBF")
        head.remove_prefix(3);
    if (head.substr(0, 7) == "#EXTM3U")
        return judge_playlist(head);
    if (b.size() >= 8 && b.substr(4, 4) == "ftyp")
    {
        // CENC-protected MP4: encrypted sample entries or protection boxes.
        if (contains(b, "encv") || contains(b, "enca") || contains(b, "pssh") ||
            contains(b, "sinf"))
            return result(Availability::drm_unsupported,
                          "The MP4 file is encrypted (Common Encryption).", "MP4");
        return result(Availability::playable, "MP4 file without encryption.", "MP4");
    }
    if (b.size() >= 8 && (b.substr(4, 4) == "moov" || b.substr(4, 4) == "mdat" ||
                          b.substr(4, 4) == "free" || b.substr(4, 4) == "wide"))
        return result(Availability::playable, "MP4/MOV file.", "MP4");
    if (byte(0) == 0x1A && byte(1) == 0x45 && byte(2) == 0xDF && byte(3) == 0xA3)
        return result(Availability::playable, "Matroska file.", "Matroska");
    if (byte(0) == 0x47 && (b.size() <= 188 || byte(188) == 0x47))
        return result(Availability::playable, "MPEG transport stream.", "MPEG-TS");
    const std::string type = lower(content_type);
    const std::string start = lower(head.substr(0, 512));
    if (contains(type, "text/html") || contains(start, "<!doctype html") ||
        contains(start, "<html"))
        return result(Availability::provider_unsupported,
                      "The address leads to a web page, not to a video file.");
    if (contains(type, "json") ||
        (!start.empty() && (start.front() == '{' || start.front() == '[')))
        return result(Availability::provider_unsupported,
                      "The address answers with data (JSON), not with a video file.");
    if (b.empty())
        return result(Availability::missing, "The resource is empty.");
    return result(Availability::provider_unsupported,
                  "The resource is not a video format AKENO STREAM recognises.");
}

ProbeResult probe_media(const MediaResource &media, const net::CancelFlag &cancel)
{
    if (!media.drm.empty())
        return result(Availability::drm_unsupported, "DRM-protected (" + media.drm + ").");
    if (media.requires_auth)
        return result(Availability::auth_required, "The provider needs a sign-in.");
    if (!media.url.empty() && media.url.front() == '/')
    {
        if (!fs::safe_path(media.url) || !fs::exists(media.url))
            return result(Availability::missing,
                          "The file is not on the console: " + fs::file_name(media.url));
        // The start of the file only (read_bytes refuses files over its limit).
        std::string data(kProbeBytes, '\0');
        const int fd = ::open(media.url.c_str(), O_RDONLY);
        if (fd < 0)
            return result(Availability::missing, "The file could not be read.");
        std::size_t got = 0;
        while (got < data.size())
        {
            const ssize_t n = ::read(fd, data.data() + got, data.size() - got);
            if (n <= 0)
                break;
            got += static_cast<std::size_t>(n);
        }
        ::close(fd);
        data.resize(got);
        return judge_media(data, {});
    }
    if (!url::parse(media.url))
        return result(Availability::provider_unsupported, "Not a valid address.");
    std::string first;
    long status = 0;
    net::Request request;
    request.url = media.url;
    request.range = "0-" + std::to_string(kProbeBytes - 1);
    request.max_bytes = kProbeBytes + 1024;
    request.connect_timeout_ms = 8000;
    request.total_timeout_ms = 15000;
    request.cancel = cancel;
    request.on_head = [&status](const net::Head &h)
    {
        status = h.status;
        return true;
    };
    request.on_data = [&first](const std::uint8_t *data, std::size_t size)
    {
        const std::size_t take = std::min(size, kProbeBytes - first.size());
        first.append(reinterpret_cast<const char *>(data), take);
        return first.size() < kProbeBytes; // enough: stop the transfer
    };
    net::Client client;
    const net::Response r = client.perform(request);
    if (status == 0)
        status = r.status;
    if (r.outcome == net::Outcome::cancelled && cancel && cancel->load())
        return result(Availability::network_error, "Cancelled.");
    if (status == 401 || status == 403)
        return result(Availability::auth_required,
                      "The server refused access (HTTP " + std::to_string(status) + ").");
    if (status == 404 || status == 410)
        return result(Availability::missing,
                      "The server no longer has this video (HTTP " + std::to_string(status) + ").");
    if (status == 451)
        return result(Availability::region_unavailable,
                      "Not available for legal reasons in this region (HTTP 451).");
    const bool got_bytes = !first.empty() && status >= 200 && status < 300;
    if (!got_bytes)
        return result(Availability::network_error, "Could not reach the video (" +
                                                       (r.error.empty() ? r.describe() : r.error) +
                                                       ").");
    ProbeResult judged = judge_media(first, r.content_type);
    // A master playlist's variants carry the keys: look at the first one too.
    if (judged.availability == Availability::playable && judged.container == "HLS" &&
        contains(first, "#EXT-X-STREAM-INF"))
    {
        std::string variant;
        bool next = false;
        std::size_t pos = 0;
        while (pos < first.size())
        {
            std::size_t end = first.find('\n', pos);
            if (end == std::string::npos)
                end = first.size();
            std::string line = first.substr(pos, end - pos);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (next && !line.empty() && line.front() != '#')
            {
                variant = line;
                break;
            }
            if (line.rfind("#EXT-X-STREAM-INF", 0) == 0)
                next = true;
            pos = end + 1;
        }
        if (const auto address = url::resolve(media.url, variant); address && !variant.empty())
        {
            const net::Response v = net::get(*address, cancel, kProbeBytes);
            if (v.ok() && v.body.rfind("#EXTM3U", 0) == 0)
            {
                ProbeResult inner = judge_playlist(v.body);
                if (inner.availability != Availability::playable)
                    return inner;
            }
        }
    }
    return judged;
}
} // namespace akeno::anime
