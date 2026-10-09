// AKENO STREAM PS5 - Bundled clips, public test streams and user streams.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "providers/open_catalog.hpp"

#include "core/fs.hpp"
#include "core/url.hpp"

namespace akeno
{
namespace
{
MediaItem stream(std::string id, std::string title, std::string subtitle, std::string url,
                 std::string description, std::string attribution, std::string badge,
                 std::uint32_t accent)
{
    MediaItem m;
    m.provider = "open";
    m.id = std::move(id);
    m.kind = ItemKind::video;
    m.title = std::move(title);
    m.subtitle = std::move(subtitle);
    m.description = std::move(description);
    m.attribution = std::move(attribution);
    m.badge = std::move(badge);
    m.accent = accent;
    m.playable = Playable{media::SourceKind::hls, std::move(url)};
    return m;
}
} // namespace

OpenCatalog::OpenCatalog(std::string app_dir, std::string data_dir)
    : app_dir_{std::move(app_dir)}, data_dir_{std::move(data_dir)}
{
}

ProviderInfo OpenCatalog::info() const
{
    ProviderInfo info;
    info.id = "open";
    info.name = "Open Streams";
    info.tagline = "DRM-free public test streams, bundled clips and your own stream list.";
    info.capabilities = {
        {"Browse", Support::available,
         "Curated public HLS test streams and clips packaged with the app."},
        {"Playback", Support::available,
         "MPEG-TS HLS with H.264/HEVC video and AAC/AC-3/MP2 audio, decoded by the PS5 hardware "
         "decoder."},
        {"Your streams", Support::available,
         "Add DRM-free HLS URLs you are allowed to watch to streams.json."},
    };
    return info;
}

std::vector<MediaItem> OpenCatalog::bundled() const
{
    std::vector<MediaItem> out;
    MediaItem av;
    av.provider = "open";
    av.id = "bundled-av-sync";
    av.kind = ItemKind::video;
    av.title = "A/V Sync Test Clip";
    av.subtitle = "Bundled - 12 s - 720p H.264 + AAC";
    av.description =
        "A test pattern with a seconds counter and a beep every second. Plays without a network "
        "connection and checks hardware video decoding, audio output and their timing.";
    av.badge = "OFFLINE";
    av.accent = 0x7c5cff;
    av.attribution = "Synthetic test signal generated with FFmpeg.";
    av.playable = Playable{media::SourceKind::local_file,
                           fs::join(app_dir_, "assets/selftest/av-sync-720p.ts")};
    out.push_back(av);
    MediaItem small = av;
    small.id = "bundled-selftest-360p";
    small.title = "Decoder Self-Test Clip";
    small.subtitle = "Bundled - 2 s - 360p H.264 + AAC";
    small.description = "The short clip used by Diagnostics > Media self-test.";
    small.accent = 0x2bb5a8;
    small.playable = Playable{media::SourceKind::local_file,
                              fs::join(app_dir_, "assets/selftest/h264-aac-360p.ts")};
    out.push_back(small);
    return out;
}

std::vector<MediaItem> OpenCatalog::public_streams()
{
    const std::string note = "Public test stream operated by a third party for player testing; "
                             "it may change or go offline at any time.";
    return {
        stream("mux-bbb", "Big Buck Bunny", "Blender Foundation - adaptive HLS",
               "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8",
               "A giant rabbit takes on three bullying rodents in the Blender Institute's open "
               "animated short. "
               "Five quality levels up to 1080p. " +
                   note,
               "(c) copyright 2008, Blender Foundation / www.bigbuckbunny.org - CC BY 3.0", "HD",
               0xf2a33a),
        stream("mux-tos", "Tears of Steel", "Blender Foundation - HLS",
               "https://test-streams.mux.dev/tos_ismc/main.m3u8",
               "Blender's open science-fiction short set in a future Amsterdam. " + note,
               "(c) Blender Foundation / mango.blender.org - CC BY 3.0", "HD", 0x3a7bd5),
        stream("bitmovin-sintel", "Sintel", "Blender Foundation - HLS",
               "https://bitdash-a.akamaihd.net/content/sintel/hls/playlist.m3u8",
               "A young woman searches for her lost dragon companion in Blender's open fantasy "
               "film. " +
                   note,
               "(c) copyright Blender Foundation / www.sintel.org - CC BY 3.0", "HD", 0xc0392b),
        stream("apple-bipbop-16x9", "BipBop 16:9", "Apple HLS example stream",
               "https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/"
               "bipbop_16x9_variant.m3u8",
               "Apple's classic HLS reference stream with several bit rates. " + note,
               "Apple HTTP Live Streaming example content.", "TEST", 0x1abc9c),
        stream("apple-bipbop-4x3", "BipBop 4:3", "Apple HLS example stream",
               "https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_4x3/"
               "bipbop_4x3_variant.m3u8",
               "Apple's original 4:3 HLS reference stream. " + note,
               "Apple HTTP Live Streaming example content.", "TEST", 0x16a085),
        stream(
            "akamai-live", "Live Test Channel", "Akamai live HLS test stream",
            "https://cph-p2p-msl.akamaized.net/hls/live/2000341/test/master.m3u8",
            "A continuously running live test channel, useful to check live playlist reloading. " +
                note,
            "Akamai test stream.", "LIVE", 0xe74c3c),
    };
}

std::vector<MediaItem> parse_user_streams(const std::string &text, std::string *error)
{
    std::vector<MediaItem> out;
    const auto parsed = json::parse(text);
    const json::Value &list = parsed.value.is_array() ? parsed.value : parsed.value["streams"];
    if (!parsed.ok || !list.is_array())
    {
        if (error)
            *error = parsed.ok ? "streams.json must contain a list of streams"
                               : "streams.json: " + parsed.error;
        return out;
    }
    int skipped = 0;
    for (const auto &entry : list.items())
    {
        const std::string url = entry["url"].str();
        const auto parsed_url = url::parse(url);
        if (entry["title"].str().empty() || !parsed_url)
        {
            ++skipped;
            continue;
        }
        MediaItem m;
        m.provider = "user";
        m.id = url;
        m.kind = ItemKind::video;
        m.title = entry["title"].str();
        m.subtitle = entry["subtitle"].str(parsed_url->host);
        m.description = entry["description"].str();
        m.image_url = entry["image"].str();
        const std::string type = entry["type"].str("hls");
        m.playable =
            Playable{type == "ts" ? media::SourceKind::http_ts : media::SourceKind::hls, url};
        m.badge = entry["live"].boolean() ? "LIVE" : "";
        m.accent = 0x8e44ad;
        out.push_back(std::move(m));
        if (out.size() >= 500)
            break;
    }
    if (skipped && error)
        *error = std::to_string(skipped) + " entr" + (skipped == 1 ? "y" : "ies") +
                 " skipped (title and http(s) url required)";
    return out;
}

std::string OpenCatalog::user_streams_path() const
{
    // The install folder (/app0 in the app, /data/homebrew/<title> from a PC)
    // is where a PC can put files; the app's own data folder is read-only to FTP.
    const std::string installed = fs::join(app_dir_, "streams.json");
    return fs::exists(installed) ? installed : fs::join(data_dir_, "streams.json");
}

std::vector<MediaItem> OpenCatalog::user_streams(std::string *error) const
{
    const auto text = fs::read_text(user_streams_path(), 1024 * 1024);
    if (!text)
        return {};
    return parse_user_streams(*text, error);
}

ShelvesResult OpenCatalog::home(const net::CancelFlag &)
{
    ShelvesResult result;
    result.ok = true;
    std::string error;
    auto mine = user_streams(&error);
    if (!mine.empty())
        result.shelves.push_back({"Your Streams", std::move(mine), false});
    result.shelves.push_back({"Open Movies & Test Streams", public_streams(), false});
    result.shelves.push_back({"Offline Test Clips", bundled(), false});
    result.error = error;
    return result;
}
} // namespace akeno
