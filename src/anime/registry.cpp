// AKENO STREAM PS5 - The anime episode providers on this console and what they offer.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "anime/registry.hpp"

#include "anime/address.hpp"
#include "anime/site_policy.hpp"
#include "anime/website.hpp"
#include "core/fs.hpp"
#include "core/json.hpp"
#include "core/url.hpp"
#include "platform/platform.hpp"
#include "providers/provider.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace akeno::anime
{
namespace
{
std::string lower(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool has_genre(const Series &s, const std::string &genre)
{
    const std::string g = lower(genre);
    return std::any_of(s.genres.begin(), s.genres.end(),
                       [&g](const std::string &x) { return lower(x) == g; });
}

bool has_year(const Series &s, int year)
{
    if (s.year == year)
        return true;
    return std::any_of(s.seasons.begin(), s.seasons.end(),
                       [year](const Season &x) { return x.year == year; });
}

bool checked_playable(const Series &s)
{
    for (const Season &season : s.seasons)
        for (const Episode &e : season.episodes)
            if (e.availability == Availability::playable)
                return true;
    return false;
}

// Probes episodes in this order: the first of each season, then the rest,
// until max_probes are done. Returns how many were checked.
int probe_episodes(SourceProvider &provider, std::vector<Series> &series,
                   const net::CancelFlag &cancel, int max_probes, ProviderReport *report)
{
    std::vector<std::tuple<std::size_t, std::size_t, std::size_t>> order;
    for (std::size_t s = 0; s < series.size(); ++s)
        for (std::size_t n = 0; n < series[s].seasons.size(); ++n)
            if (!series[s].seasons[n].episodes.empty())
                order.emplace_back(s, n, 0);
    for (std::size_t s = 0; s < series.size(); ++s)
        for (std::size_t n = 0; n < series[s].seasons.size(); ++n)
            for (std::size_t e = 1; e < series[s].seasons[n].episodes.size(); ++e)
                order.emplace_back(s, n, e);
    int checked = 0;
    for (const auto &[s, n, e] : order)
    {
        if (checked >= max_probes || (cancel && cancel->load()))
            break;
        Series &sr = series[s];
        Season &season = sr.seasons[n];
        Episode &episode = season.episodes[e];
        if (episode.availability != Availability::unverified)
            continue;
        ++checked;
        const Resolution r = provider.resolve(sr, season, episode, cancel);
        ProbeResult p;
        if (!r.ok)
            p = {r.availability, r.detail, {}};
        else
            p = probe_media(r.media, cancel);
        episode.availability = p.availability;
        episode.availability_detail = p.detail;
        if (report)
        {
            ++report->probed;
            switch (p.availability)
            {
            case Availability::playable:
                ++report->playable;
                break;
            case Availability::drm_unsupported:
                ++report->drm;
                break;
            case Availability::auth_required:
                ++report->auth;
                break;
            case Availability::region_unavailable:
                ++report->region;
                break;
            case Availability::missing:
                ++report->missing;
                break;
            case Availability::network_error:
                ++report->network;
                break;
            default:
                ++report->unsupported;
                break;
            }
        }
    }
    return checked;
}

void count(const std::vector<Series> &series, ProviderReport &r)
{
    r.series = static_cast<int>(series.size());
    for (const Series &s : series)
    {
        r.seasons += static_cast<int>(s.seasons.size());
        for (const Season &season : s.seasons)
            for (const Episode &e : season.episodes)
            {
                ++r.episodes;
                if (e.media || !e.source_ref.empty())
                    ++r.with_media;
            }
    }
}

void add_unique(std::vector<std::string> &list, const std::string &value)
{
    if (!value.empty() && std::find(list.begin(), list.end(), value) == list.end())
        list.push_back(value);
}

std::string joined(const std::vector<std::string> &list, std::size_t limit)
{
    std::string out;
    for (std::size_t i = 0; i < list.size() && i < limit; ++i)
        out += (i ? ", " : "") + list[i];
    if (list.size() > limit)
        out += ", ...";
    return out;
}

// What the loaded series actually carry; nothing is assumed from the source's type.
void describe(const std::vector<Series> &series, ProviderReport &r)
{
    bool seasons = false, artwork = false, thumbnails = false, descriptions = false;
    bool alt_titles = false, anilist = false;
    std::vector<std::string> subtitles, audio;
    for (const Series &s : series)
    {
        seasons = seasons || s.seasons.size() > 1;
        artwork = artwork || !s.poster.empty() || !s.banner.empty();
        alt_titles = alt_titles || !s.alt_titles.empty();
        anilist = anilist || s.anilist_id > 0;
        int episodes = 0;
        for (const Season &season : s.seasons)
        {
            anilist = anilist || season.anilist_id > 0;
            for (const Episode &e : season.episodes)
            {
                ++episodes;
                thumbnails = thumbnails || !e.thumbnail.empty();
                descriptions = descriptions || !e.description.empty();
                if (!e.media)
                    continue;
                for (const SubtitleTrack &t : e.media->subtitles)
                    add_unique(subtitles, t.language.empty() ? t.label : t.language);
                for (const AudioTrack &a : e.media->audio)
                    add_unique(audio, a.language.empty() ? a.label : a.language);
            }
        }
        if (r.preview.size() < 5)
            r.preview_art.push_back(s.poster);
        if (r.preview.size() < 5 && s.details_pending)
            r.preview.push_back(s.year ? s.title + " (" + std::to_string(s.year) + ")" : s.title);
        else if (r.preview.size() < 5)
            r.preview.push_back(s.title + " - " + std::to_string(s.seasons.size()) +
                                (s.seasons.size() == 1 ? " season, " : " seasons, ") +
                                std::to_string(episodes) +
                                (episodes == 1 ? " episode" : " episodes"));
    }
    if (series.empty())
        return;
    r.capabilities.push_back(alt_titles ? "Search (titles and alternative titles)"
                                        : "Search (titles)");
    r.capabilities.push_back(seasons ? "Seasons" : "One season per series");
    if (r.episodes)
        r.capabilities.push_back("Episode lists");
    if (descriptions)
        r.capabilities.push_back("Episode descriptions");
    if (artwork || thumbnails)
        r.capabilities.push_back(artwork && thumbnails ? "Posters and episode thumbnails"
                                 : artwork             ? "Posters"
                                                       : "Episode thumbnails");
    if (!audio.empty())
        r.capabilities.push_back("Audio: " + joined(audio, 6));
    if (!subtitles.empty())
        r.capabilities.push_back("Subtitles: " + joined(subtitles, 6));
    if (anilist)
        r.capabilities.push_back("AniList links");
    if (r.playable)
        r.capabilities.push_back("Native playback (checked)");
}
} // namespace

Registry::Registry(std::string data_dir, std::string app_dir)
    : data_dir_{std::move(data_dir)}, app_dir_{std::move(app_dir)},
      catalogs_file_{data_dir_, ProviderFile::Kind::catalogs},
      websites_file_{data_dir_, ProviderFile::Kind::websites}
{
}

void Registry::load()
{
    configs_.clear();
    problems_.clear();
    last_error_.clear();
    ProviderConfig sample;
    sample.id = kSampleId;
    sample.name = "AKENO Sample Series";
    sample.type = ProviderType::bundled;
    sample.address = fs::join(app_dir_, "assets/anime/sample/catalog.json");
    sample.builtin = true;
    configs_.push_back(sample);
    const std::string personal_path = fs::join(app_dir_, "anime-catalog.json");
    if (fs::exists(personal_path))
    {
        ProviderConfig personal;
        personal.id = kPersonalId;
        personal.name = "My Anime Library";
        personal.type = ProviderType::catalog;
        personal.address = personal_path;
        personal.builtin = true;
        configs_.push_back(personal);
    }
    std::set<std::string> taken;
    for (const ProviderConfig &c : configs_)
        taken.insert(c.id);
    // Catalogues and PeerTube, as 1.0.0 keeps them (at most kMaxProviders in all).
    StoredProviders catalogs =
        catalogs_file_.load(taken, kMaxProviders - std::min(kMaxProviders, configs_.size()));
    for (ProviderConfig &c : catalogs.providers)
    {
        taken.insert(c.id);
        configs_.push_back(std::move(c));
    }
    for (const std::string &id : catalogs_file_.reserved_ids())
        taken.insert(id);
    StoredProviders websites =
        websites_file_.load(taken, kMaxWebsiteProviders, [](const std::string &adapter)
                            { return website_adapter_version(adapter); });
    for (ProviderConfig &c : websites.providers)
        configs_.push_back(std::move(c));
    for (StoredProviders *stored : {&catalogs, &websites})
        for (std::string &problem : stored->problems)
            problems_.push_back(std::move(problem));
    if (!problems_.empty())
        last_error_ = problems_.front();
}

bool Registry::save(ProviderType type, std::string *why)
{
    const bool websites = type == ProviderType::website;
    std::vector<ProviderConfig> list;
    for (const ProviderConfig &c : configs_)
        if (!c.builtin && (c.type == ProviderType::website) == websites)
            list.push_back(c);
    std::string error;
    if ((websites ? websites_file_ : catalogs_file_).save(list, &error))
        return true;
    last_error_ = error;
    if (why)
        *why = error;
    return false;
}

const ProviderConfig *Registry::config(std::string_view id) const
{
    for (const ProviderConfig &c : configs_)
        if (c.id == id)
            return &c;
    return nullptr;
}

const ProviderConfig *Registry::find_by_address(std::string_view address) const
{
    if (address.empty())
        return nullptr;
    for (const ProviderConfig &c : configs_)
        if (!c.builtin && (same_site_address(c.address, address) ||
                           (!c.original_url.empty() && same_site_address(c.original_url, address))))
            return &c;
    return nullptr;
}

bool Registry::id_taken(const std::string &id) const
{
    return config(id) || id == kSampleId || id == kPersonalId ||
           catalogs_file_.reserved_ids().count(id) || websites_file_.reserved_ids().count(id);
}

std::size_t Registry::count_of(bool websites) const
{
    return static_cast<std::size_t>(
        std::count_if(configs_.begin(), configs_.end(), [websites](const ProviderConfig &c)
                      { return (c.type == ProviderType::website) == websites; }));
}

bool Registry::add(ProviderConfig c, std::string *why, std::string *existing, std::string *added)
{
    const auto fail = [why](std::string text)
    {
        if (why)
            *why = std::move(text);
        return false;
    };
    const auto u = url::parse(c.address);
    if (!u || !u->is_http())
        return fail("A provider needs an http(s) address.");
    for (const std::string &address : {c.address, c.original_url})
        if (const auto policy = unsupported_site(address))
            return fail(policy->verdict);
    if (c.type == ProviderType::bundled)
        c.type = ProviderType::catalog;
    const bool website = c.type == ProviderType::website;
    if (website && c.adapter.empty())
        return fail("A website provider needs a website adapter.");
    for (const std::string &address : {c.address, c.original_url})
        if (const ProviderConfig *x = find_by_address(address))
        {
            if (existing)
                *existing = x->id;
            return fail("This address is already a provider: " + x->name);
        }
    ProviderFile &file = website ? websites_file_ : catalogs_file_;
    if (file.read_only())
        return fail(file.read_only_reason());
    if (website ? count_of(true) >= kMaxWebsiteProviders
                : configs_.size() - count_of(true) >= kMaxProviders)
        return fail("At most " + std::to_string(website ? kMaxWebsiteProviders : kMaxProviders) +
                    (website ? " website providers" : " providers") + " can be added.");
    if (c.name.empty())
        c.name = u->host;
    if (c.name.size() > 60)
        c.name.resize(60);
    std::string base = slug(c.name);
    if (base.empty())
        base = website ? "site" : "provider";
    // Website ids carry a prefix 1.0.0 never creates (it uses letters, digits
    // and '-' only), so the two files can never hand out the same id.
    if (website)
        base = kWebsiteIdPrefix + base;
    c.id = base;
    for (int n = 2; id_taken(c.id); ++n)
        c.id = base + "-" + std::to_string(n);
    c.builtin = false;
    if (website)
    {
        if (c.original_url.empty())
            c.original_url = c.address;
        c.adapter_version = std::max(1, website_adapter_version(c.adapter));
    }
    if (!c.added)
        c.added = platform::wall_clock_seconds();
    const std::string id = c.id;
    const ProviderType type = c.type;
    configs_.push_back(std::move(c));
    if (!save(type, why))
    {
        configs_.pop_back();
        return false;
    }
    if (added)
        *added = id;
    return true;
}

bool Registry::remove(std::string_view id)
{
    const auto it = std::find_if(configs_.begin(), configs_.end(),
                                 [id](const ProviderConfig &c) { return c.id == id; });
    if (it == configs_.end() || it->builtin)
        return false;
    const ProviderConfig removed = *it;
    const auto index = it - configs_.begin();
    configs_.erase(it);
    if (!save(removed.type))
    {
        // The file could not be written (or may not be): keep the provider.
        configs_.insert(configs_.begin() + index, removed);
        return false;
    }
    states_.erase(std::remove_if(states_.begin(), states_.end(),
                                 [id](const ProviderState &s) { return s.config.id == id; }),
                  states_.end());
    return true;
}

bool Registry::set_enabled(std::string_view id, bool enabled)
{
    for (ProviderConfig &c : configs_)
        if (c.id == id && c.type == ProviderType::website && c.enabled != enabled)
        {
            c.enabled = enabled;
            if (!save(ProviderType::website))
            {
                c.enabled = !enabled;
                return false;
            }
            if (!enabled)
                states_.erase(std::remove_if(states_.begin(), states_.end(),
                                             [id](const ProviderState &s)
                                             { return s.config.id == id; }),
                              states_.end());
            return true;
        }
    return false;
}

void Registry::set_summary(std::string_view id, int series, int episodes)
{
    for (ProviderConfig &c : configs_)
        if (c.id == id && c.type == ProviderType::website &&
            (c.known_series != series || c.known_episodes != episodes))
        {
            c.known_series = series;
            c.known_episodes = episodes;
            c.checked = platform::wall_clock_seconds();
            (void)save(ProviderType::website);
        }
}

void Registry::set_anilist_id(std::string_view id, int anilist_id)
{
    for (ProviderConfig &c : configs_)
        if (c.id == id && !c.builtin && c.type != ProviderType::website)
        {
            c.anilist_id = anilist_id;
            (void)save(c.type);
        }
    for (ProviderState &s : states_)
        if (s.config.id == id && s.config.type == ProviderType::peertube)
            for (Series &series : s.series)
                series.anilist_id = anilist_id;
}

ProviderState Registry::load_provider(const ProviderConfig &config, const ProviderContext &context,
                                      const net::CancelFlag &cancel, int max_probes)
{
    ProviderState state;
    state.config = config;
    // A provider entry for a site that cannot be one (written by hand): its
    // site is never contacted.
    for (const std::string &address : {config.address, config.original_url})
        if (const auto policy = unsupported_site(address))
        {
            state.error = policy->verdict;
            return state;
        }
    const auto provider = make_provider(config, context);
    LoadedCatalog loaded = provider->load(cancel);
    state.errors = std::move(loaded.errors);
    state.warnings = std::move(loaded.warnings);
    if (!loaded.ok || loaded.series.empty())
    {
        state.error = loaded.error.empty() ? "The provider offers no series." : loaded.error;
        state.loaded = false;
        return state;
    }
    state.series = std::move(loaded.series);
    state.next_page = std::move(loaded.next_page);
    state.total = loaded.total;
    state.checked = probe_episodes(*provider, state.series, cancel, max_probes, nullptr);
    state.loaded = true;
    return state;
}

int Registry::check_series(const ProviderConfig &config, const ProviderContext &context,
                           Series &series, const net::CancelFlag &cancel, int max_probes)
{
    const auto provider = make_provider(config, context);
    std::vector<Series> one{std::move(series)};
    const int checked = probe_episodes(*provider, one, cancel, max_probes, nullptr);
    series = std::move(one.front());
    return checked;
}

void Registry::install(ProviderState state)
{
    // A provider removed while it was loading stays removed.
    if (!config(state.config.id))
        return;
    for (ProviderState &s : states_)
        if (s.config.id == state.config.id)
        {
            // Keep what earlier checks and playback found for the same episodes.
            for (Series &series : state.series)
                if (const Series *old = find_series(s.config.id, series.id))
                    for (Season &season : series.seasons)
                        for (Episode &e : season.episodes)
                            if (e.availability == Availability::unverified)
                                if (const auto ref = find_episode(*old, season.id, e.id))
                                {
                                    const Episode &before =
                                        old->seasons[ref->season].episodes[ref->episode];
                                    if (before.availability != Availability::unverified)
                                    {
                                        e.availability = before.availability;
                                        e.availability_detail = before.availability_detail;
                                    }
                                }
            // A website provider's catalogue page lists entries again: the
            // seasons and episodes already loaded stay, and so do entries
            // found on later pages or by search.
            if (state.config.type == ProviderType::website && state.loaded)
            {
                for (Series &series : state.series)
                    if (series.details_pending)
                        if (const Series *old = find_series(s.config.id, series.id);
                            old && !old->details_pending)
                            series = *old;
                for (const Series &old : s.series)
                    if (std::none_of(state.series.begin(), state.series.end(),
                                     [&old](const Series &x) { return x.id == old.id; }) &&
                        state.series.size() < kMaxCatalogSeries)
                        state.series.push_back(old);
            }
            s = std::move(state);
            return;
        }
    states_.push_back(std::move(state));
    // Keep the order of the configuration list.
    std::stable_sort(states_.begin(), states_.end(),
                     [this](const ProviderState &a, const ProviderState &b)
                     {
                         const auto index = [this](const std::string &id)
                         {
                             for (std::size_t i = 0; i < configs_.size(); ++i)
                                 if (configs_[i].id == id)
                                     return i;
                             return configs_.size();
                         };
                         return index(a.config.id) < index(b.config.id);
                     });
}

void Registry::merge_series(std::string_view provider, std::vector<Series> series,
                            const std::string *next_page)
{
    for (ProviderState &s : states_)
        if (s.config.id == provider)
        {
            for (Series &x : series)
                if (std::none_of(s.series.begin(), s.series.end(),
                                 [&x](const Series &old) { return old.id == x.id; }) &&
                    s.series.size() < kMaxCatalogSeries)
                    s.series.push_back(std::move(x));
            if (next_page)
                s.next_page = *next_page;
            return;
        }
}

void Registry::install_series(std::string_view provider, Series series)
{
    for (ProviderState &s : states_)
        if (s.config.id == provider)
        {
            for (Series &old : s.series)
                if (old.id == series.id)
                {
                    old = std::move(series);
                    return;
                }
            if (s.series.size() < kMaxCatalogSeries)
                s.series.push_back(std::move(series));
            return;
        }
}

const ProviderState *Registry::state(std::string_view id) const
{
    for (const ProviderState &s : states_)
        if (s.config.id == id)
            return &s;
    return nullptr;
}

const Series *Registry::find_series(std::string_view provider, std::string_view series) const
{
    for (const ProviderState &s : states_)
        if (s.config.id == provider)
            for (const Series &x : s.series)
                if (x.id == series)
                    return &x;
    return nullptr;
}

Series *Registry::find_mutable(std::string_view provider, std::string_view series)
{
    return const_cast<Series *>(find_series(provider, series));
}

std::vector<const Series *> Registry::series_for_anilist(int anilist_id) const
{
    std::vector<const Series *> out;
    if (anilist_id <= 0)
        return out;
    for (const Series *s : all_series())
    {
        bool mapped = s->anilist_id == anilist_id;
        for (const Season &season : s->seasons)
            mapped = mapped || season.anilist_id == anilist_id;
        if (mapped)
            out.push_back(s);
    }
    return out;
}

std::vector<const Series *> Registry::all_series() const
{
    std::vector<const Series *> out;
    for (const ProviderState &s : states_)
        for (const Series &x : s.series)
            out.push_back(&x);
    return out;
}

std::vector<const Series *> Registry::available() const
{
    std::vector<const Series *> out;
    for (const Series *s : all_series())
        if (checked_playable(*s))
            out.push_back(s);
    return out;
}

std::vector<const Series *> Registry::recently_updated(std::size_t limit) const
{
    std::vector<const Series *> out = all_series();
    std::stable_sort(out.begin(), out.end(),
                     [](const Series *a, const Series *b) { return a->updated > b->updated; });
    out.erase(
        std::remove_if(out.begin(), out.end(), [](const Series *s) { return s->updated.empty(); }),
        out.end());
    if (out.size() > limit)
        out.resize(limit);
    return out;
}

std::vector<SeriesHit> Registry::search(std::string_view query, const SearchFilters &f) const
{
    std::vector<SeriesHit> out;
    for (const Series *s : all_series())
    {
        const int score = query.empty() ? 1 : title_match(*s, query);
        if (score <= 0)
            continue;
        if (f.year && !has_year(*s, f.year))
            continue;
        if (!f.genre.empty() && !has_genre(*s, f.genre))
            continue;
        if (!f.status.empty() && lower(s->status) != lower(f.status))
            continue;
        const bool playable = playable_episodes(*s) > 0;
        if (f.availability == SearchFilters::Availability::playable && !playable)
            continue;
        if (f.availability == SearchFilters::Availability::catalogue && playable)
            continue;
        out.push_back({s, score});
    }
    std::stable_sort(
        out.begin(), out.end(), [](const SeriesHit &a, const SeriesHit &b)
        { return a.score != b.score ? a.score > b.score : a.series->title < b.series->title; });
    return out;
}

std::vector<std::string> Registry::suggestions(std::string_view prefix, std::size_t limit) const
{
    std::vector<std::string> out;
    const std::string p = normalize_title(prefix);
    if (p.empty())
        return out;
    for (const Series *s : all_series())
    {
        const auto consider = [&](const std::string &title)
        {
            const std::string t = normalize_title(title);
            if ((t.rfind(p, 0) == 0 || t.find(" " + p) != std::string::npos) &&
                std::find(out.begin(), out.end(), title) == out.end() && out.size() < limit)
                out.push_back(title);
        };
        consider(s->title);
        for (const std::string &alt : s->alt_titles)
            consider(alt);
    }
    return out;
}

void Registry::set_availability(std::string_view provider, std::string_view series,
                                std::string_view season, std::string_view episode, Availability a,
                                const std::string &detail)
{
    Series *s = find_mutable(provider, series);
    if (!s)
        return;
    if (const auto ref = find_episode(*s, season, episode))
    {
        Episode &e = s->seasons[ref->season].episodes[ref->episode];
        e.availability = a;
        e.availability_detail = detail;
    }
}

ProviderReport report_for(const ProviderState &state, SourceClass base)
{
    ProviderReport r;
    r.source = base;
    r.errors = state.errors;
    r.warnings = state.warnings;
    count(state.series, r);
    for (const Series &s : state.series)
        for (const Season &season : s.seasons)
            for (const Episode &e : season.episodes)
                switch (e.availability)
                {
                case Availability::playable:
                    ++r.probed;
                    ++r.playable;
                    break;
                case Availability::drm_unsupported:
                    ++r.drm;
                    break;
                case Availability::auth_required:
                    ++r.auth;
                    break;
                case Availability::region_unavailable:
                    ++r.region;
                    break;
                case Availability::missing:
                    ++r.probed;
                    ++r.missing;
                    break;
                case Availability::network_error:
                    ++r.probed;
                    ++r.network;
                    break;
                case Availability::provider_unsupported:
                    ++r.unsupported;
                    break;
                default:
                    break;
                }
    describe(state.series, r);
    if (!state.loaded)
    {
        r.importable = false;
        r.verdict = state.error.empty() ? "The provider could not be loaded." : state.error;
        return r;
    }
    const int blocked = r.drm + r.auth + r.region;
    if (r.playable > 0)
    {
        r.source = SourceClass::native_provider;
        r.importable = true;
        r.verdict = std::to_string(r.playable) + " of " + std::to_string(r.probed) +
                    " checked episodes play in AKENO STREAM's own player. " +
                    std::to_string(r.episodes) + " episodes in " + std::to_string(r.series) +
                    (r.series == 1 ? " series." : " series.");
    }
    else if (r.with_media == 0)
    {
        r.source = SourceClass::metadata;
        r.importable = false;
        r.verdict = "The source lists series and episodes but no video for any of them: it is a "
                    "catalogue, not a streaming provider.";
    }
    else if (blocked > 0 && r.network == 0 && r.missing == 0)
    {
        r.source = SourceClass::protected_source;
        r.importable = false;
        r.verdict = "Every checked episode is protected (DRM), needs a sign-in or is not "
                    "offered in your region. AKENO STREAM plays only DRM-free video.";
    }
    else if (r.network > 0 && r.network == r.probed)
    {
        r.importable = true;
        r.verdict = "The episodes could not be reached just now, so playback is not confirmed. "
                    "You can import it and AKENO STREAM checks again when you open a series.";
    }
    else
    {
        r.importable = false;
        r.verdict = "None of the checked episodes is a video AKENO STREAM can play.";
    }
    if (!r.errors.empty() && r.importable)
        r.verdict += " " + std::to_string(r.errors.size()) +
                     (r.errors.size() == 1 ? " entry was" : " entries were") +
                     " skipped because of errors.";
    return r;
}

namespace
{
// A website an adapter recognised: its first catalogue page, the seasons and
// episodes of up to three entries (trying at most six: a site's newest
// entries may be empty), and up to max_probes of their videos.
ProviderReport check_website(const SiteDetection &site, const std::string &entered,
                             const std::string &name, const ProviderContext &context,
                             const net::CancelFlag &cancel, ProviderConfig *config, int max_probes)
{
    const WebsiteAdapter *adapter = find_website_adapter(site.adapter);
    ProviderConfig c;
    c.type = ProviderType::website;
    c.adapter = site.adapter;
    c.adapter_version = adapter ? adapter->version() : 1;
    c.settings = site.settings;
    c.address = site.homepage;
    c.original_url = entered;
    c.name = !name.empty() ? name : site.site_name;
    if (c.name.empty())
        if (const auto u = url::parse(site.homepage))
            c.name = u->host;
    c.id = kWebsiteIdPrefix + slug(c.name);
    ProviderState state = Registry::load_provider(c, context, cancel, 0);
    int opened = 0, tried = 0;
    for (Series &s : state.series)
    {
        if (opened >= 3 || tried >= 6 || (cancel && cancel->load()))
            break;
        if (!s.details_pending)
            continue;
        ++tried;
        SiteSeries full = load_website_series(c, s, context, cancel);
        if (!full.ok)
            continue;
        ++opened;
        s = std::move(full.series);
        bool media = false;
        for (const Season &season : s.seasons)
            for (const Episode &e : season.episodes)
                media = media || e.media || !e.source_ref.empty();
        if (media)
            break;
    }
    ProviderReport counts;
    if (state.loaded)
    {
        const auto provider = make_provider(c, context);
        probe_episodes(*provider, state.series, cancel, max_probes, &counts);
    }
    ProviderReport report = report_for(state, SourceClass::native_provider);
    report.format = site.format;
    report.adapter = site.adapter;
    report.adapter_label = adapter ? adapter->label() : site.adapter;
    report.evidence = site.evidence;
    report.total_series = state.total;
    report.name = c.name;
    SiteCapabilities capabilities = site.capabilities;
    capabilities.native_playback = report.playable > 0;
    std::vector<std::string> lines = capability_lines(capabilities);
    for (const std::string &line : report.capabilities)
        if (line.starts_with("Audio") || line.starts_with("Subtitles") ||
            line.starts_with("Posters") || line.starts_with("Episode thumbnails"))
            lines.push_back(line);
    report.capabilities = std::move(lines);
    if (!state.loaded)
        report.verdict = "Recognised as a " + site.format +
                         ", but its catalogue could not be "
                         "read: " +
                         state.error;
    else if (report.source == SourceClass::metadata)
        report.verdict = "Recognised as a " + site.format + ". Its catalogue lists " +
                         std::to_string(report.series) +
                         (report.series == 1 ? " entry" : " entries") +
                         ", but none of the checked ones names a video file AKENO STREAM may play "
                         "(only a web player): the site stays in Websites (browser).";
    else if (report.importable && report.playable > 0)
        report.verdict = "Recognised as a " + site.format + ". " + std::to_string(report.playable) +
                         " of " + std::to_string(report.probed) +
                         " checked episodes play in AKENO STREAM's own player.";
    if (report.importable && config)
    {
        c.known_series = state.total > 0 ? state.total : static_cast<int>(state.series.size());
        c.known_episodes = report.episodes;
        *config = c;
    }
    return report;
}
} // namespace

ProviderReport check_source(const std::string &address, const std::string &name,
                            const ProviderContext &context, const net::CancelFlag &cancel,
                            ProviderConfig *config, int max_probes)
{
    ProviderReport report;
    const auto u = url::parse(address);
    if (!u || !u->is_http() || u->host.empty())
    {
        report.source = SourceClass::unreachable;
        report.verdict = "Enter an http(s) address of a catalogue, channel or playlist.";
        return report;
    }
    // A site investigated and found to offer no permitted way in: the reason,
    // without a single request to it (anime/site_policy.hpp).
    if (const auto policy = unsupported_site(address))
    {
        report.source = SourceClass::web_page;
        report.format = policy->site + " (website)";
        report.name = policy->site;
        report.verdict = policy->verdict;
        for (const std::string &reason : policy->reasons)
            report.warnings.push_back({"", reason});
        report.warnings.push_back({"", "Investigation: " + policy->document});
        return report;
    }
    ProviderConfig c;
    c.name = name;
    c.address = u->str();
    c.id = slug(name.empty() ? u->host : name);
    if (c.id.empty())
        c.id = "provider";
    SourceClass base = SourceClass::native_provider;
    if (const PeerTubeAddress pt = parse_peertube_address(c.address); pt.ok)
    {
        c.type = ProviderType::peertube;
        report.format = pt.channel.empty() ? "PeerTube playlist" : "PeerTube channel";
        base = SourceClass::documented_api;
    }
    else
    {
        const SitePage f = fetch_site_page(
            c.address, cancel,
            "application/json,text/html;q=0.9,application/xhtml+xml;q=0.9,*/*;q=0.5",
            8u * 1024u * 1024u);
        if (!f.ok)
        {
            report.source = (f.status == 401 || f.status == 403) ? SourceClass::protected_source
                                                                 : SourceClass::unreachable;
            report.format = f.status ? "HTTP " + std::to_string(f.status) : "No answer";
            report.verdict = report.source == SourceClass::protected_source
                                 ? "The address needs a sign-in. AKENO STREAM does not take "
                                   "browser sessions or cookies into its player."
                                 : f.error;
            return report;
        }
        if (looks_like_catalog(f.body))
        {
            c.type = ProviderType::catalog;
            report.format = "AKENO catalogue v" + std::to_string(kCatalogVersion);
        }
        else
        {
            const std::string head = f.body.substr(0, 4096);
            const ProbeResult media = judge_media(head, {});
            if (head.rfind("#EXTM3U", 0) == 0 && head.find("#EXT-X-") == std::string::npos)
            {
                report.source = SourceClass::playlist;
                report.format = "M3U list";
                report.verdict = "An M3U list names streams but has no series, seasons or "
                                 "episode data. Add it in Sources instead.";
            }
            else if (media.availability == Availability::playable ||
                     media.availability == Availability::drm_unsupported)
            {
                report.source = SourceClass::media_file;
                report.format = media.container;
                report.verdict = "This is one video, not a catalogue. Play it with "
                                 "Websites > Play a Video Link or add it in Sources.";
            }
            else if (SiteDetection site = detect_website(f, cancel); site.recognised)
                return check_website(site, c.address, name, context, cancel, config, max_probes);
            else if (media.detail.find("web page") != std::string::npos)
            {
                report.source = SourceClass::web_page;
                report.format = "Web page (HTML)";
                report.verdict =
                    "None of AKENO STREAM's website adapters recognises this website: " +
                    site.why_not +
                    " It opens in the browser (Websites); AKENO STREAM does not read video out "
                    "of web pages it cannot identify.";
                report.evidence = site.evidence;
            }
            else
            {
                report.source = SourceClass::metadata;
                report.format = "Data without the AKENO catalogue format";
                report.verdict = "The address answers with data that is not an AKENO catalogue "
                                 "(see docs/ANIME_PROVIDERS.md for the format).";
            }
            return report;
        }
    }
    const ProviderState state = Registry::load_provider(c, context, cancel, 0);
    ProviderState probed = state;
    ProviderReport counts;
    if (state.loaded)
    {
        const auto provider = make_provider(c, context);
        probe_episodes(*provider, probed.series, cancel, max_probes, &counts);
    }
    const std::string format = report.format;
    report = report_for(probed, base);
    report.format = format;
    if (!probed.series.empty())
        report.name = probed.series.size() == 1 ? probed.series.front().title : name;
    if (report.importable && config)
    {
        if (c.name.empty())
            c.name = report.name.empty() ? u->host : report.name;
        *config = c;
    }
    return report;
}
} // namespace akeno::anime
