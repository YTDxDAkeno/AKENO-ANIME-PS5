// AKENO STREAM PS5 - Anime series, seasons and episodes, independent of any service.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "anime/model.hpp"

#include <algorithm>
#include <cctype>

namespace akeno::anime
{
namespace
{
struct AvailabilityName
{
    Availability value;
    const char *id;
    const char *label;
    int rank; // higher is better for the viewer
};
constexpr AvailabilityName kNames[] = {
    {Availability::playable, "playable", "Playable", 100},
    {Availability::unverified, "unverified", "Not checked yet", 80},
    {Availability::network_error, "network-error", "Network error", 60},
    {Availability::auth_required, "auth-required", "Sign-in required", 50},
    {Availability::region_unavailable, "region-unavailable", "Not offered in your region", 45},
    {Availability::drm_unsupported, "drm-unsupported", "DRM-protected: not supported", 40},
    {Availability::provider_unsupported, "provider-unsupported", "Format not supported", 35},
    {Availability::missing, "missing", "Video missing", 30},
    {Availability::episodes_indexed, "episodes-indexed", "Episode listed, no video source", 20},
    {Availability::catalogue_only, "catalogue-only", "Catalogue only", 10},
};

const AvailabilityName &name_of(Availability a)
{
    for (const auto &n : kNames)
        if (n.value == a)
            return n;
    return kNames[std::size(kNames) - 1];
}

std::vector<std::string> words(std::string_view text)
{
    std::vector<std::string> out;
    std::string current;
    for (const char c : normalize_title(text))
    {
        if (c == ' ')
        {
            if (!current.empty())
                out.push_back(std::move(current));
            current.clear();
        }
        else
            current += c;
    }
    if (!current.empty())
        out.push_back(std::move(current));
    return out;
}

int match_one(const std::string &title, std::string_view query)
{
    const std::string t = normalize_title(title);
    const std::string q = normalize_title(query);
    if (q.empty() || t.empty())
        return 0;
    if (t == q)
        return 100;
    if (t.rfind(q, 0) == 0)
        return 90;
    const std::vector<std::string> tw = words(title), qw = words(query);
    // Every query word must begin some title word, in any order.
    for (const std::string &w : qw)
        if (std::none_of(tw.begin(), tw.end(),
                         [&w](const std::string &x) { return x.rfind(w, 0) == 0; }))
            return t.find(q) != std::string::npos ? 50 : 0;
    return 70;
}
} // namespace

const char *availability_id(Availability a) noexcept
{
    return name_of(a).id;
}

const char *availability_label(Availability a) noexcept
{
    return name_of(a).label;
}

Availability availability_from_id(std::string_view id) noexcept
{
    for (const auto &n : kNames)
        if (id == n.id)
            return n.value;
    return Availability::episodes_indexed;
}

int availability_rank(Availability a) noexcept
{
    return name_of(a).rank;
}

std::string episode_key(std::string_view provider, std::string_view series, std::string_view season,
                        std::string_view episode)
{
    std::string out;
    out.reserve(provider.size() + series.size() + season.size() + episode.size() + 3);
    out.append(provider).append("/").append(series).append("/").append(season).append("/").append(
        episode);
    return out;
}

std::string series_key(std::string_view provider, std::string_view series)
{
    return std::string{provider} + "/" + std::string{series};
}

std::string normalize_title(std::string_view text)
{
    std::string out;
    bool space = false;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const auto c = static_cast<unsigned char>(text[i]);
        if (std::isalnum(c))
        {
            if (space && !out.empty())
                out += ' ';
            space = false;
            out += static_cast<char>(std::tolower(c));
        }
        else if (c >= 0x80)
        {
            // Common Latin letters with accents (UTF-8 C3 xx) fold to ASCII.
            if (c == 0xC3 && i + 1 < text.size())
            {
                static constexpr char kFold[] = "aaaaaaaceeeeiiiidnooooo/ouuuuyty";
                const auto next = static_cast<unsigned char>(text[i + 1]);
                const unsigned index = (next & 0x1F);
                if (next >= 0x80 && next <= 0xBF && index < sizeof(kFold) - 1 &&
                    kFold[index] != '/')
                {
                    if (space && !out.empty())
                        out += ' ';
                    space = false;
                    out += kFold[index];
                    ++i;
                    continue;
                }
            }
            // Romaji macrons: ā ē ī ō ū (and capitals) fold to plain vowels.
            if ((c == 0xC4 || c == 0xC5) && i + 1 < text.size())
            {
                const auto next = static_cast<unsigned char>(text[i + 1]);
                char folded = 0;
                if (c == 0xC4 && (next == 0x80 || next == 0x81))
                    folded = 'a';
                else if (c == 0xC4 && (next == 0x92 || next == 0x93))
                    folded = 'e';
                else if (c == 0xC4 && (next == 0xAA || next == 0xAB))
                    folded = 'i';
                else if (c == 0xC5 && (next == 0x8C || next == 0x8D))
                    folded = 'o';
                else if (c == 0xC5 && (next == 0xAA || next == 0xAB))
                    folded = 'u';
                if (folded)
                {
                    if (space && !out.empty())
                        out += ' ';
                    space = false;
                    out += folded;
                    ++i;
                    continue;
                }
            }
            space = true; // other characters separate words
        }
        else
            space = true;
    }
    return out;
}

int title_match(const Series &series, std::string_view query)
{
    int best = match_one(series.title, query);
    for (const std::string &alt : series.alt_titles)
        best = std::max(best, match_one(alt, query) - 5);
    return std::max(best, 0);
}

Availability series_availability(const Series &series)
{
    Availability best = Availability::catalogue_only;
    for (const Season &s : series.seasons)
        for (const Episode &e : s.episodes)
            if (availability_rank(e.availability) > availability_rank(best))
                best = e.availability;
    return best;
}

int playable_episodes(const Series &series)
{
    int n = 0;
    for (const Season &s : series.seasons)
        for (const Episode &e : s.episodes)
            if ((e.media || !e.source_ref.empty()) && (e.availability == Availability::playable ||
                                                       e.availability == Availability::unverified))
                ++n;
    return n;
}

int episode_total(const Series &series)
{
    int n = 0;
    for (const Season &s : series.seasons)
        n += static_cast<int>(s.episodes.size());
    return n;
}

std::optional<EpisodeRef> next_episode(const Series &series, EpisodeRef from)
{
    if (from.season >= series.seasons.size())
        return std::nullopt;
    if (from.episode + 1 < series.seasons[from.season].episodes.size())
        return EpisodeRef{from.season, from.episode + 1};
    for (std::size_t s = from.season + 1; s < series.seasons.size(); ++s)
        if (!series.seasons[s].episodes.empty())
            return EpisodeRef{s, 0};
    return std::nullopt;
}

std::optional<EpisodeRef> previous_episode(const Series &series, EpisodeRef from)
{
    if (from.season >= series.seasons.size())
        return std::nullopt;
    if (from.episode > 0)
        return EpisodeRef{from.season, from.episode - 1};
    for (std::size_t s = from.season; s-- > 0;)
        if (!series.seasons[s].episodes.empty())
            return EpisodeRef{s, series.seasons[s].episodes.size() - 1};
    return std::nullopt;
}

std::optional<EpisodeRef> find_episode(const Series &series, std::string_view season_id,
                                       std::string_view episode_id)
{
    for (std::size_t s = 0; s < series.seasons.size(); ++s)
    {
        if (series.seasons[s].id != season_id)
            continue;
        const auto &eps = series.seasons[s].episodes;
        for (std::size_t e = 0; e < eps.size(); ++e)
            if (eps[e].id == episode_id)
                return EpisodeRef{s, e};
    }
    return std::nullopt;
}

std::string episode_label(const Season &season, const Episode &episode)
{
    if (episode.special || season.number == 0)
        return episode.number > 0 ? "Special " + std::to_string(episode.number) : "Special";
    std::string out;
    if (season.number > 0)
        out = "S" + std::to_string(season.number) + " ";
    out += episode.number > 0 ? "E" + std::to_string(episode.number) : "Extra";
    return out;
}
} // namespace akeno::anime
