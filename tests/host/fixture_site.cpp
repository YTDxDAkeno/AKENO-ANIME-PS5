// AKENO STREAM PS5 - The offline fixture website for website-provider tests and screenshots.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture_site.hpp"

#include "core/fs.hpp"
#include "core/url.hpp"

#include <cstdlib>
#include <filesystem>
#include <mutex>

namespace akeno::test
{
namespace
{
std::mutex g_lock;
std::vector<std::string> g_requests;

std::string content_type(const std::string &path)
{
    const auto ends = [&path](const char *tail)
    {
        const std::string t{tail};
        return path.size() >= t.size() && path.compare(path.size() - t.size(), t.size(), t) == 0;
    };
    if (ends(".html"))
        return "text/html; charset=utf-8";
    if (ends(".jpg"))
        return "image/jpeg";
    if (ends(".vtt"))
        return "text/vtt";
    if (ends(".mp4") || ends(".m4s"))
        return "video/mp4";
    if (ends(".ts"))
        return "video/mp2t";
    return "application/octet-stream";
}
} // namespace

std::string make_fixture_site(const std::string &source_root)
{
    namespace sfs = std::filesystem;
    char path[] = "/tmp/akeno-fixture-site-XXXXXX";
    const std::string root = mkdtemp(path) ? path : "/tmp/akeno-fixture-site";
    const sfs::path source{source_root};
    const auto options = sfs::copy_options::recursive | sfs::copy_options::overwrite_existing;
    sfs::copy(source / "tests/fixtures/website-schema", root, options);
    for (const char *dir : {"media", "subs", "art"})
        sfs::create_directories(sfs::path{root} / dir);
    for (const char *clip : {"h264-aac-360p.mp4", "h264-aac-360p.ts", "h264-aac-360p-cenc.m4s",
                             "h264-aac-360p-frag.mp4"})
        sfs::copy_file(source / "assets/selftest" / clip, sfs::path{root} / "media" / clip,
                       sfs::copy_options::overwrite_existing);
    sfs::copy_file(source / "assets/anime/sample/subs/s1e1.en.vtt",
                   sfs::path{root} / "subs/s1e1.en.vtt", sfs::copy_options::overwrite_existing);
    sfs::copy_file(source / "assets/anime/sample/art/poster.jpg",
                   sfs::path{root} / "art/signal.jpg", sfs::copy_options::overwrite_existing);
    sfs::copy_file(source / "assets/anime/sample/thumbs/s1e2.jpg", sfs::path{root} / "art/s1e2.jpg",
                   sfs::copy_options::overwrite_existing);
    return root;
}

std::optional<net::Response> serve_fixture_site(const std::string &site_root,
                                                const net::Request &request)
{
    if (request.url.rfind(kFixtureSite, 0) != 0)
        return std::nullopt;
    const auto u = url::parse(request.url);
    net::Response r;
    r.final_url = request.url;
    if (!u)
    {
        r.outcome = net::Outcome::invalid_url;
        return r;
    }
    {
        std::lock_guard<std::mutex> guard(g_lock);
        g_requests.push_back(u->path + (u->has_query ? "?" + u->query : std::string{}));
    }
    std::string path = u->path == "/" ? "/index.html" : u->path;
    const auto bytes =
        path.find("..") == std::string::npos ? fs::read_bytes(site_root + path) : std::nullopt;
    if (!bytes)
    {
        r.outcome = net::Outcome::http_error;
        r.status = 404;
        return r;
    }
    r.outcome = net::Outcome::ok;
    r.status = 200;
    r.content_type = content_type(path);
    r.body.assign(bytes->begin(), bytes->end());
    if (request.max_bytes && r.body.size() > request.max_bytes && !request.on_data)
    {
        r.outcome = net::Outcome::too_large;
        r.body.clear();
    }
    return r;
}

std::vector<std::string> fixture_site_requests()
{
    std::lock_guard<std::mutex> guard(g_lock);
    return g_requests;
}

void clear_fixture_site_requests()
{
    std::lock_guard<std::mutex> guard(g_lock);
    g_requests.clear();
}
} // namespace akeno::test
