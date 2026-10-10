// AKENO STREAM PS5 - Sidecar subtitles (WebVTT, SubRip) for the native player.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/subtitles.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace akeno::media
{
namespace
{
std::string_view trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

std::vector<std::string_view> lines_of(std::string_view text)
{
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t end = text.find('\n', start);
        std::string_view line = text.substr(
            start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        out.push_back(line);
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return out;
}

// Removes <i>, <c.yellow>, <00:01.000>, {\an8} and decodes basic entities.
std::string clean_line(std::string_view line)
{
    std::string out;
    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const char c = line[i];
        if (c == '<')
        {
            const std::size_t close = line.find('>', i);
            if (close == std::string_view::npos)
                break;
            i = close;
            continue;
        }
        if (c == '{' && i + 1 < line.size() && line[i + 1] == '\\')
        {
            const std::size_t close = line.find('}', i);
            if (close == std::string_view::npos)
                break;
            i = close;
            continue;
        }
        if (c == '&')
        {
            static constexpr std::pair<std::string_view, char> kEntities[] = {
                {"&amp;", '&'},  {"&lt;", '<'},   {"&gt;", '>'},
                {"&nbsp;", ' '}, {"&quot;", '"'}, {"&#39;", '\''}};
            bool matched = false;
            for (const auto &[name, value] : kEntities)
                if (line.substr(i, name.size()) == name)
                {
                    out += value;
                    i += name.size() - 1;
                    matched = true;
                    break;
                }
            if (matched)
                continue;
        }
        if (static_cast<unsigned char>(c) >= 0x20 || c == '\t')
            out += c;
    }
    return std::string{trim(out)};
}

bool parse_timing(std::string_view line, double *start, double *end)
{
    const std::size_t arrow = line.find("-->");
    if (arrow == std::string_view::npos)
        return false;
    std::string_view left = trim(line.substr(0, arrow));
    std::string_view right = trim(line.substr(arrow + 3));
    // WebVTT cue settings follow the end time ("00:05.000 line:90%").
    if (const std::size_t space = right.find_first_of(" \t"); space != std::string_view::npos)
        right = right.substr(0, space);
    *start = parse_cue_time(left);
    *end = parse_cue_time(right);
    return *start >= 0.0 && *end >= 0.0;
}
} // namespace

double parse_cue_time(std::string_view text)
{
    text = trim(text);
    if (text.empty() || text.size() > 20)
        return -1.0;
    double parts[3] = {0, 0, 0};
    int count = 0;
    std::string current;
    for (std::size_t i = 0; i <= text.size(); ++i)
    {
        const char c = i < text.size() ? text[i] : ':';
        if (c == ':')
        {
            if (current.empty() || count >= 3)
                return -1.0;
            for (char &d : current)
                if (d == ',')
                    d = '.';
            if (std::count(current.begin(), current.end(), '.') > 1)
                return -1.0;
            char *endp = nullptr;
            parts[count++] = std::strtod(current.c_str(), &endp);
            if (!endp || *endp != '\0')
                return -1.0;
            current.clear();
        }
        else if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == ',')
            current += c;
        else
            return -1.0;
    }
    if (count == 1)
        return parts[0];
    if (count == 2)
        return parts[0] * 60.0 + parts[1];
    return parts[0] * 3600.0 + parts[1] * 60.0 + parts[2];
}

SubtitleDocument parse_subtitles(std::string_view text, std::string_view format)
{
    SubtitleDocument doc;
    if (text.substr(0, 3) == "\xEF\xBB\xBF")
        text.remove_prefix(3);
    const bool vtt = format == "vtt" || (format.empty() && text.substr(0, 6) == "WEBVTT");
    if (vtt && text.substr(0, 6) != "WEBVTT")
    {
        doc.error = "not a WebVTT file (no WEBVTT header)";
        return doc;
    }
    const std::vector<std::string_view> lines = lines_of(text);
    std::size_t i = vtt ? 1 : 0;
    while (i < lines.size())
    {
        // Skip blank lines and, in WebVTT, NOTE / STYLE / REGION blocks.
        if (trim(lines[i]).empty())
        {
            ++i;
            continue;
        }
        const std::string_view first = trim(lines[i]);
        if (vtt && (first.rfind("NOTE", 0) == 0 || first == "STYLE" || first == "REGION"))
        {
            while (i < lines.size() && !trim(lines[i]).empty())
                ++i;
            continue;
        }
        // An optional cue identifier (WebVTT) or index (SubRip) precedes the timing.
        double start = 0, end = 0;
        if (!parse_timing(lines[i], &start, &end))
        {
            if (i + 1 < lines.size() && parse_timing(lines[i + 1], &start, &end))
                ++i;
            else
            {
                while (i < lines.size() && !trim(lines[i]).empty())
                    ++i;
                continue;
            }
        }
        ++i;
        std::string body;
        while (i < lines.size() && !trim(lines[i]).empty())
        {
            const std::string line = clean_line(lines[i]);
            if (!line.empty())
            {
                if (!body.empty())
                    body += '\n';
                body += line;
            }
            ++i;
        }
        if (end > start && !body.empty() && body.size() <= 1000)
            doc.cues.push_back({start, end, std::move(body)});
        if (doc.cues.size() >= kMaxCues)
            break;
    }
    std::stable_sort(doc.cues.begin(), doc.cues.end(),
                     [](const Cue &a, const Cue &b) { return a.start < b.start; });
    doc.ok = !doc.cues.empty();
    if (!doc.ok)
        doc.error = "no subtitle cues found";
    return doc;
}

std::string cue_text_at(const std::vector<Cue> &cues, double seconds)
{
    std::string out;
    // Cues are sorted by start; overlapping cues are joined.
    auto it = std::upper_bound(cues.begin(), cues.end(), seconds,
                               [](double t, const Cue &c) { return t < c.start; });
    // Look back over cues that started earlier and may still be showing.
    for (int back = 0; it != cues.begin() && back < 8; ++back)
    {
        --it;
        if (seconds >= it->start && seconds < it->end)
            out = out.empty() ? it->text : it->text + "\n" + out;
    }
    return out;
}
} // namespace akeno::media
