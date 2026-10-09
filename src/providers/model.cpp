// AKENO STREAM PS5 - Content model shared by providers and screens.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "providers/model.hpp"

namespace akeno
{
namespace
{
const char *kind_name(ItemKind kind)
{
    switch (kind)
    {
    case ItemKind::video:
        return "video";
    case ItemKind::series:
        return "series";
    case ItemKind::channel:
        return "channel";
    case ItemKind::folder:
        return "folder";
    case ItemKind::file:
        return "file";
    case ItemKind::info:
        return "info";
    }
    return "video";
}

ItemKind kind_from(const std::string &name)
{
    if (name == "series")
        return ItemKind::series;
    if (name == "channel")
        return ItemKind::channel;
    if (name == "folder")
        return ItemKind::folder;
    if (name == "file")
        return ItemKind::file;
    if (name == "info")
        return ItemKind::info;
    return ItemKind::video;
}

const char *source_name(media::SourceKind kind)
{
    switch (kind)
    {
    case media::SourceKind::hls:
        return "hls";
    case media::SourceKind::http_ts:
        return "http_ts";
    case media::SourceKind::local_file:
        return "file";
    }
    return "hls";
}

media::SourceKind source_from(const std::string &name)
{
    if (name == "http_ts")
        return media::SourceKind::http_ts;
    if (name == "file")
        return media::SourceKind::local_file;
    return media::SourceKind::hls;
}
} // namespace

json::Value MediaItem::to_json() const
{
    json::Value v = json::Value::object();
    v.set("provider", provider);
    v.set("id", id);
    v.set("kind", kind_name(kind));
    v.set("title", title);
    if (!subtitle.empty())
        v.set("subtitle", subtitle);
    if (!description.empty())
        v.set("description", description);
    if (!image_url.empty())
        v.set("image", image_url);
    if (!banner_url.empty())
        v.set("banner", banner_url);
    if (portrait)
        v.set("portrait", true);
    if (!badge.empty())
        v.set("badge", badge);
    if (!meta.empty())
        v.set("meta", meta);
    if (duration > 0.0)
        v.set("duration", duration);
    if (playable)
    {
        json::Value p = json::Value::object();
        p.set("kind", source_name(playable->kind));
        p.set("url", playable->url);
        v.set("playable", p);
    }
    if (!external_url.empty())
        v.set("external_url", external_url);
    if (!external_label.empty())
        v.set("external_label", external_label);
    if (!attribution.empty())
        v.set("attribution", attribution);
    if (accent)
        v.set("accent", static_cast<long long>(accent));
    if (!genres.empty())
    {
        json::Value g = json::Value::array();
        for (const auto &genre : genres)
            g.push(genre);
        v.set("genres", g);
    }
    return v;
}

MediaItem MediaItem::from_json(const json::Value &v)
{
    MediaItem item;
    item.provider = v["provider"].str();
    item.id = v["id"].str();
    item.kind = kind_from(v["kind"].str());
    item.title = v["title"].str();
    item.subtitle = v["subtitle"].str();
    item.description = v["description"].str();
    item.image_url = v["image"].str();
    item.banner_url = v["banner"].str();
    item.portrait = v["portrait"].boolean();
    item.badge = v["badge"].str();
    item.meta = v["meta"].str();
    item.duration = v["duration"].num();
    if (v["playable"].is_object())
        item.playable =
            Playable{source_from(v["playable"]["kind"].str()), v["playable"]["url"].str()};
    item.external_url = v["external_url"].str();
    item.external_label = v["external_label"].str();
    item.attribution = v["attribution"].str();
    item.accent = static_cast<std::uint32_t>(v["accent"].integer());
    for (const auto &g : v["genres"].items())
        item.genres.push_back(g.str());
    return item;
}
} // namespace akeno
