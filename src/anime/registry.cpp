// AKENO STREAM PS5 - The anime episode providers on this console and what they offer.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "anime/registry.hpp"

#include "core/fs.hpp"
#include "core/json.hpp"
#include "core/url.hpp"
#include "platform/platform.hpp"
#include "providers/provider.hpp"

#include <algorithm>
#include <cctype>

namespace akeno::anime
{
namespace
{
constexpr char kFile[] = "anime-providers.json";

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
} // namespace

Registry::Registry(std::string data_dir, std::string app_dir)
    : data_dir_{std::move(data_dir)}, app_dir_{std::move(app_dir)}
{
}

void Registry::load()
{
    configs_.clear();
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
    const std::string path = fs::join(data_dir_, kFile);
    const auto text = fs::read_text(path, 1024 * 1024);
    if (!text)
        return;
    const auto parsed = json::parse(*text);
    if (!parsed.ok || !parsed.value["providers"].is_array())
    {
        last_error_ = std::string{kFile} + " is damaged; kept as " + kFile + ".corrupt";
        (void)fs::write_atomic(path + ".corrupt", *text);
        return;
    }
    for (const auto &p : parsed.value["providers"].items())
    {
        ProviderConfig c;
        c.id = p["id"].str();
        c.name = p["name"].str();
        c.type = provider_type_from_id(p["type"].str());
        c.address = p["address"].str();
        c.added = static_cast<std::uint64_t>(p["added"].integer());
        c.anilist_id = static_cast<int>(p["anilistId"].integer());
        if (c.id.empty() || c.address.empty() || c.type == ProviderType::bundled || config(c.id) ||
            configs_.size() >= kMaxProviders)
            continue;
        // Added providers come from the web only; files are the built-in ones.
        const auto u = url::parse(c.address);
        if (!u || !u->is_http())
            continue;
        configs_.push_back(std::move(c));
    }
}

void Registry::save()
{
    json::Value list = json::Value::array();
    for (const ProviderConfig &c : configs_)
    {
        if (c.builtin)
            continue;
        json::Value p = json::Value::object();
        p.set("id", c.id);
        p.set("name", c.name);
        p.set("type", provider_type_id(c.type));
        p.set("address", c.address);
        p.set("added", static_cast<long long>(c.added));
        if (c.anilist_id)
            p.set("anilistId", c.anilist_id);
        list.push(std::move(p));
    }
    json::Value root = json::Value::object();
    root.set("version", 1);
    root.set("providers", std::move(list));
    std::string error;
    if (!fs::write_atomic(fs::join(data_dir_, kFile), root.dump(true), &error))
        last_error_ = "Could not save " + std::string{kFile} + ": " + error;
}

const ProviderConfig *Registry::config(std::string_view id) const
{
    for (const ProviderConfig &c : configs_)
        if (c.id == id)
            return &c;
    return nullptr;
}

bool Registry::add(ProviderConfig c, std::string *why)
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
    if (configs_.size() >= kMaxProviders)
        return fail("At most " + std::to_string(kMaxProviders) + " providers can be added.");
    for (const ProviderConfig &x : configs_)
        if (x.address == c.address)
            return fail("This address is already a provider: " + x.name);
    if (c.name.empty())
        c.name = u->host;
    if (c.name.size() > 60)
        c.name.resize(60);
    std::string base = slug(c.name);
    if (base.empty())
        base = "provider";
    c.id = base;
    for (int n = 2; config(c.id) || c.id == kSampleId || c.id == kPersonalId; ++n)
        c.id = base + "-" + std::to_string(n);
    c.builtin = false;
    if (c.type == ProviderType::bundled)
        c.type = ProviderType::catalog;
    if (!c.added)
        c.added = platform::wall_clock_seconds();
    configs_.push_back(std::move(c));
    save();
    return true;
}

bool Registry::remove(std::string_view id)
{
    const auto it = std::find_if(configs_.begin(), configs_.end(),
                                 [id](const ProviderConfig &c) { return c.id == id; });
    if (it == configs_.end() || it->builtin)
        return false;
    configs_.erase(it);
    states_.erase(std::remove_if(states_.begin(), states_.end(),
                                 [id](const ProviderState &s) { return s.config.id == id; }),
                  states_.end());
    save();
    return true;
}

void Registry::set_anilist_id(std::string_view id, int anilist_id)
{
    for (ProviderConfig &c : configs_)
        if (c.id == id && !c.builtin)
        {
            c.anilist_id = anilist_id;
            save();
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
    state.checked = probe_episodes(*provider, state.series, cancel, max_probes, nullptr);
    state.loaded = true;
    return state;
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
        const Fetched f = fetch_text(c.address, cancel, u->host, 8u * 1024u * 1024u);
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
            else if (media.detail.find("web page") != std::string::npos)
            {
                report.source = SourceClass::web_page;
                report.format = "Web page (HTML)";
                report.verdict =
                    "This is a website, not a catalogue or documented API. It opens in the "
                    "browser (Websites); AKENO STREAM does not read video out of web pages.";
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
