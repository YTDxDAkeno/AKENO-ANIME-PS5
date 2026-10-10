// AKENO STREAM PS5 - HLS playlist parsing and variant selection.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/hls.hpp"

#include "core/url.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace akeno::hls
{
namespace
{
std::string_view trim(std::string_view s) noexcept
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

std::string upper(std::string_view s)
{
    std::string out{s};
    for (char &c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string lower(std::string_view s)
{
    std::string out{s};
    for (char &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

double to_double(std::string_view s) noexcept
{
    std::string copy{trim(s)};
    if (copy.empty() || copy.size() > 32)
        return 0.0;
    char *end = nullptr;
    const double v = std::strtod(copy.c_str(), &end);
    return end && end != copy.c_str() && v >= 0.0 && v < 1e9 ? v : 0.0;
}

std::uint64_t to_u64(std::string_view s) noexcept
{
    s = trim(s);
    std::uint64_t v = 0;
    for (char c : s)
    {
        if (c < '0' || c > '9')
            return v;
        if (v > (UINT64_MAX - 9) / 10)
            return UINT64_MAX;
        v = v * 10 + static_cast<unsigned>(c - '0');
    }
    return v;
}

std::string attribute(const std::vector<std::pair<std::string, std::string>> &attrs,
                      std::string_view key)
{
    for (const auto &[k, v] : attrs)
        if (k == key)
            return v;
    return {};
}

bool video_codec_ok(std::string_view codec)
{
    return codec.starts_with("avc1") || codec.starts_with("avc3") || codec.starts_with("hvc1") ||
           codec.starts_with("hev1");
}

bool is_video_codec(std::string_view codec)
{
    static constexpr std::string_view video[] = {"avc", "hvc",  "hev", "vp0", "vp8",
                                                 "vp9", "av01", "dvh", "dva", "mp4v"};
    return std::any_of(std::begin(video), std::end(video),
                       [&](std::string_view p) { return codec.starts_with(p); });
}

bool audio_codec_ok(std::string_view codec)
{
    // MPEG-1/2 Layer III (mp4a.40.34, mp4a.69, mp4a.6b) is not framed by the
    // native demuxer, which handles Layer II only.
    if (codec == "mp4a.40.34" || codec == "mp4a.69" || codec == "mp4a.6b" || codec == "mp3")
        return false;
    return codec.starts_with("mp4a") || codec == "ac-3" || codec == "ec-3";
}

void classify_codecs(Variant &variant)
{
    std::string_view list = variant.codecs;
    while (!list.empty())
    {
        const std::size_t comma = list.find(',');
        const std::string codec = lower(trim(list.substr(0, comma)));
        if (!codec.empty())
        {
            if (is_video_codec(codec))
            {
                if (!video_codec_ok(codec))
                    variant.video_codec_supported = false;
            }
            else if (!audio_codec_ok(codec) && codec != "stpp.ttml.im1t" && codec != "wvtt")
            {
                variant.audio_codec_supported = false;
            }
        }
        if (comma == std::string_view::npos)
            break;
        list.remove_prefix(comma + 1);
    }
}
} // namespace

std::vector<std::pair<std::string, std::string>> parse_attributes(std::string_view list)
{
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t i = 0;
    while (i < list.size())
    {
        while (i < list.size() && (list[i] == ' ' || list[i] == ','))
            ++i;
        const std::size_t key_start = i;
        while (i < list.size() && list[i] != '=' && list[i] != ',')
            ++i;
        std::string key = upper(trim(list.substr(key_start, i - key_start)));
        if (i >= list.size() || list[i] != '=')
        {
            if (!key.empty())
                out.emplace_back(std::move(key), std::string{});
            continue;
        }
        ++i; // '='
        std::string value;
        if (i < list.size() && list[i] == '"')
        {
            ++i;
            const std::size_t end = list.find('"', i);
            value = std::string{
                list.substr(i, end == std::string_view::npos ? std::string_view::npos : end - i)};
            i = end == std::string_view::npos ? list.size() : end + 1;
        }
        else
        {
            const std::size_t end = list.find(',', i);
            value = std::string{trim(
                list.substr(i, end == std::string_view::npos ? std::string_view::npos : end - i))};
            i = end == std::string_view::npos ? list.size() : end;
        }
        if (!key.empty())
            out.emplace_back(std::move(key), std::move(value));
    }
    return out;
}

ParseResult parse(std::string_view text, std::string_view base_url, const Limits &limits)
{
    ParseResult result;
    Playlist &pl = result.playlist;
    const auto fail = [&](std::string why, std::size_t line)
    {
        result.ok = false;
        result.error = std::move(why);
        result.line = line;
        return result;
    };
    if (text.size() > limits.max_bytes)
        return fail("playlist exceeds size limit", 0);
    if (text.starts_with("\xEF\xBB\xBF"))
        text.remove_prefix(3);

    bool header = false;
    bool pending_variant = false;
    Variant variant;
    bool pending_segment = false;
    double segment_duration = 0.0;
    bool next_discontinuity = false;
    bool saw_media_tags = false;
    int current_map = -1, current_key = -1;
    std::int64_t next_length = -1, next_offset = -1, last_range_end = 0;
    std::string last_range_uri;
    std::uint64_t sequence = 0;
    std::size_t line_number = 0;

    while (!text.empty())
    {
        const std::size_t newline = text.find('\n');
        std::string_view line = text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
        ++line_number;
        if (line.size() > limits.max_line)
            return fail("playlist line too long", line_number);
        line = trim(line);
        if (line.empty())
            continue;
        if (!header)
        {
            if (line != "#EXTM3U")
                return fail("missing #EXTM3U header (not an HLS playlist)", line_number);
            header = true;
            continue;
        }
        if (line.front() == '#')
        {
            if (!line.starts_with("#EXT"))
                continue; // comment
            const std::size_t colon = line.find(':');
            const std::string_view tag = line.substr(0, colon);
            const std::string_view value =
                colon == std::string_view::npos ? std::string_view{} : line.substr(colon + 1);
            if (tag == "#EXT-X-STREAM-INF")
            {
                const auto attrs = parse_attributes(value);
                variant = Variant{};
                variant.bandwidth = to_u64(attribute(attrs, "BANDWIDTH"));
                variant.average_bandwidth = to_u64(attribute(attrs, "AVERAGE-BANDWIDTH"));
                const std::string resolution = attribute(attrs, "RESOLUTION");
                if (const std::size_t x = resolution.find_first_of("xX"); x != std::string::npos)
                {
                    variant.width =
                        static_cast<int>(to_u64(std::string_view{resolution}.substr(0, x)));
                    variant.height =
                        static_cast<int>(to_u64(std::string_view{resolution}.substr(x + 1)));
                }
                variant.frame_rate = to_double(attribute(attrs, "FRAME-RATE"));
                variant.codecs = attribute(attrs, "CODECS");
                variant.audio_group = attribute(attrs, "AUDIO");
                classify_codecs(variant);
                pending_variant = true;
            }
            else if (tag == "#EXT-X-MEDIA")
            {
                const auto attrs = parse_attributes(value);
                Rendition r;
                r.type = upper(attribute(attrs, "TYPE"));
                r.group = attribute(attrs, "GROUP-ID");
                r.name = attribute(attrs, "NAME");
                r.language = attribute(attrs, "LANGUAGE");
                r.is_default = upper(attribute(attrs, "DEFAULT")) == "YES";
                r.autoselect = upper(attribute(attrs, "AUTOSELECT")) == "YES";
                const std::string uri = attribute(attrs, "URI");
                if (!uri.empty())
                {
                    const auto resolved = url::resolve(base_url, uri);
                    if (resolved)
                        r.uri = *resolved;
                }
                pl.renditions.push_back(std::move(r));
            }
            else if (tag == "#EXTINF")
            {
                saw_media_tags = true;
                const std::size_t comma = value.find(',');
                segment_duration = to_double(value.substr(0, comma));
                pending_segment = true;
            }
            else if (tag == "#EXT-X-TARGETDURATION")
            {
                saw_media_tags = true;
                pl.target_duration = to_double(value);
            }
            else if (tag == "#EXT-X-MEDIA-SEQUENCE")
            {
                pl.media_sequence = to_u64(value);
                sequence = pl.media_sequence;
            }
            else if (tag == "#EXT-X-DISCONTINUITY")
            {
                next_discontinuity = true;
            }
            else if (tag == "#EXT-X-ENDLIST")
            {
                pl.endlist = true;
            }
            else if (tag == "#EXT-X-PLAYLIST-TYPE")
            {
                pl.playlist_type = upper(trim(value));
                if (pl.playlist_type == "VOD")
                    saw_media_tags = true;
            }
            else if (tag == "#EXT-X-KEY")
            {
                const auto attrs = parse_attributes(value);
                const std::string method = upper(attribute(attrs, "METHOD"));
                const std::string format = attribute(attrs, "KEYFORMAT");
                if (method == "NONE")
                    current_key = -1;
                else if (method == "AES-128" && (format.empty() || format == "identity"))
                {
                    Key key;
                    if (const auto resolved = url::resolve(base_url, attribute(attrs, "URI")))
                        key.uri = *resolved;
                    std::string iv = attribute(attrs, "IV");
                    if (iv.size() == 34 && (iv[1] == 'x' || iv[1] == 'X'))
                    {
                        key.has_iv = true;
                        for (int i = 0; i < 16; ++i)
                            key.iv[i] = static_cast<std::uint8_t>(
                                std::strtoul(iv.substr(2 + 2 * i, 2).c_str(), nullptr, 16));
                    }
                    if (key.uri.empty() && pl.unsupported.empty())
                        pl.unsupported = "an AES-128 key without a usable URI";
                    pl.keys.push_back(std::move(key));
                    current_key = static_cast<int>(pl.keys.size()) - 1;
                }
                else if (pl.unsupported.empty())
                {
                    pl.unsupported = "DRM-protected segments (METHOD=" + method +
                                     (format.empty() ? std::string{} : ", KEYFORMAT=" + format) +
                                     "); DRM-protected HLS is not supported";
                }
            }
            else if (tag == "#EXT-X-MAP")
            {
                const auto attrs = parse_attributes(value);
                Map map;
                if (const auto resolved = url::resolve(base_url, attribute(attrs, "URI")))
                    map.uri = *resolved;
                const std::string range = attribute(attrs, "BYTERANGE");
                if (!range.empty())
                {
                    const std::size_t at = range.find('@');
                    map.length = static_cast<std::int64_t>(to_u64(range.substr(0, at)));
                    map.offset = at == std::string::npos
                                     ? 0
                                     : static_cast<std::int64_t>(to_u64(range.substr(at + 1)));
                }
                if (map.uri.empty())
                    return fail("EXT-X-MAP without a usable URI", line_number);
                pl.maps.push_back(std::move(map));
                current_map = static_cast<int>(pl.maps.size()) - 1;
            }
            else if (tag == "#EXT-X-BYTERANGE")
            {
                const std::size_t at = value.find('@');
                next_length = static_cast<std::int64_t>(to_u64(value.substr(0, at)));
                next_offset = at == std::string_view::npos
                                  ? -2 // continues after the previous range of the same URI
                                  : static_cast<std::int64_t>(to_u64(value.substr(at + 1)));
            }
            continue;
        }

        // URI line.
        if (pending_variant)
        {
            const auto resolved = url::resolve(base_url, line);
            pending_variant = false;
            if (!resolved)
                return fail("invalid variant URI", line_number);
            if (pl.variants.size() >= limits.max_variants)
                continue;
            variant.uri = *resolved;
            pl.variants.push_back(std::move(variant));
            variant = Variant{};
            continue;
        }
        if (!pending_segment && !saw_media_tags)
            return fail("URI without #EXTINF or #EXT-X-STREAM-INF", line_number);
        const auto resolved = url::resolve(base_url, line);
        if (!resolved)
            return fail("invalid segment URI", line_number);
        if (pl.segments.size() >= limits.max_segments)
            return fail("playlist has too many segments", line_number);
        Segment segment;
        segment.uri = *resolved;
        segment.duration = segment_duration;
        segment.start = pl.total_duration;
        segment.sequence = sequence++;
        segment.discontinuity = next_discontinuity;
        segment.map = current_map;
        segment.key = current_key;
        if (next_length > 0)
        {
            segment.length = next_length;
            segment.offset = next_offset >= 0                ? next_offset
                             : last_range_uri == segment.uri ? last_range_end
                                                             : 0;
            last_range_end = segment.offset + segment.length;
            last_range_uri = segment.uri;
            next_length = next_offset = -1;
        }
        pl.total_duration += segment_duration;
        pl.segments.push_back(std::move(segment));
        pending_segment = false;
        next_discontinuity = false;
        segment_duration = 0.0;
    }

    if (!header)
        return fail("empty playlist", 0);
    if (!pl.variants.empty() && saw_media_tags)
        return fail("playlist mixes master and media tags", 0);
    if (!pl.variants.empty())
        pl.kind = Kind::master;
    else if (saw_media_tags || !pl.segments.empty())
        pl.kind = Kind::media;
    else
        return fail("playlist has no variants or segments", 0);
    if (pl.kind == Kind::media && pl.segments.empty() && pl.endlist)
        return fail("media playlist has no segments", 0);
    result.ok = true;
    return result;
}

const Rendition *audio_rendition(const Playlist &playlist, const Variant &variant)
{
    if (variant.audio_group.empty())
        return nullptr;
    const Rendition *chosen = nullptr;
    int chosen_rank = -1;
    for (const Rendition &r : playlist.renditions)
    {
        if (r.type != "AUDIO" || r.group != variant.audio_group)
            continue;
        if (r.uri.empty())
            return nullptr; // a member without URI: the audio is in the variant itself
        const int rank = r.is_default ? 2 : (r.autoselect ? 1 : 0);
        if (rank > chosen_rank)
        {
            chosen = &r;
            chosen_rank = rank;
        }
    }
    return chosen;
}

int select_variant(const Playlist &playlist, const Selection &selection, std::string *why)
{
    const auto set_why = [&](const char *reason)
    {
        if (why)
            *why = reason;
    };
    if (playlist.kind != Kind::master || playlist.variants.empty())
    {
        set_why("not a master playlist");
        return -1;
    }
    const auto within = [&](const Variant &v)
    {
        if (v.width > 0 && (v.width > selection.max_width || v.height > selection.max_height))
            return false;
        return selection.max_bandwidth == 0 || v.bandwidth <= selection.max_bandwidth;
    };
    int best = -1;
    int best_tier = -1;
    for (std::size_t i = 0; i < playlist.variants.size(); ++i)
    {
        const Variant &v = playlist.variants[i];
        if (!v.video_codec_supported)
            continue;
        // Within limits beats outside; playable audio beats video only.
        const bool fits = within(v);
        const int tier = (fits ? 2 : 0) + (v.audio_codec_supported ? 1 : 0);
        if (best < 0 || tier > best_tier)
        {
            best = static_cast<int>(i);
            best_tier = tier;
            continue;
        }
        if (tier < best_tier)
            continue;
        const Variant &b = playlist.variants[static_cast<std::size_t>(best)];
        // Within limits prefer the highest bandwidth; outside limits the lowest.
        const bool better = fits ? (v.bandwidth > b.bandwidth ||
                                    (v.bandwidth == b.bandwidth && v.height > b.height))
                                 : (v.bandwidth < b.bandwidth);
        if (better)
            best = static_cast<int>(i);
    }
    if (best < 0)
        set_why("no variant uses a supported video codec (H.264 or HEVC)");
    else if ((best_tier & 1) == 0)
        set_why("the stream's audio codec is not supported; playing video only");
    return best;
}

std::size_t segment_at(const Playlist &playlist, double t) noexcept
{
    if (playlist.segments.empty() || t <= 0.0)
        return 0;
    const auto it =
        std::upper_bound(playlist.segments.begin(), playlist.segments.end(), t,
                         [](double value, const Segment &s) { return value < s.start; });
    const std::size_t index = static_cast<std::size_t>(it - playlist.segments.begin());
    return index == 0 ? 0 : index - 1;
}
} // namespace akeno::hls
