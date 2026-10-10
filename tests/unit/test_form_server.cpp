// AKENO STREAM PS5 - The phone page for adding sources.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/form_server.hpp"
#include "net/http.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace akeno;

namespace
{
net::Response get(const std::string &address)
{
    net::Client client;
    net::Request r;
    r.url = address;
    r.total_timeout_ms = 5000;
    return client.perform(r);
}

net::Response post(const std::string &address, const std::string &body)
{
    net::Client client;
    net::Request r;
    r.url = address;
    r.method = "POST";
    r.body = body;
    r.headers = {"Content-Type: application/x-www-form-urlencoded"};
    r.total_timeout_ms = 5000;
    return client.perform(r);
}
} // namespace

TEST(FormServer, DecodesForms)
{
    const auto fields =
        net::parse_form("name=My+TV%21&url=https%3A%2F%2Fa.example%2Fl.m3u%3Fx%3D1&empty=&flag");
    ASSERT_EQ(fields.size(), 4u);
    EXPECT_EQ(fields[0].second, "My TV!");
    EXPECT_EQ(fields[1].second, "https://a.example/l.m3u?x=1");
    EXPECT_EQ(fields[2].second, "");
    EXPECT_EQ(fields[3].first, "flag");
}

TEST(FormServer, AcceptsAddressesOnlyWithThePin)
{
    net::Client::global_init();
    net::FormServer server;
    std::string error;
    ASSERT_TRUE(server.start(0, &error)) << error;
    ASSERT_GT(server.port(), 0);
    ASSERT_EQ(server.pin().size(), 8u);
    const std::string base = "http://127.0.0.1:" + std::to_string(server.port()) + "/";

    EXPECT_EQ(get(base).status, 404);
    EXPECT_EQ(get(base + "wrongpin").status, 404);
    const net::Response page = get(base + server.pin());
    EXPECT_EQ(page.status, 200);
    EXPECT_NE(page.body.find("<form method=\"post\">"), std::string::npos);
    EXPECT_NE(page.body.find("ships no sources"), std::string::npos);

    const net::Response added = post(
        base + server.pin(), "name=%3Cb%3EMy+TV%3C%2Fb%3E&url=https%3A%2F%2Ftv.example%2Flist.m3u");
    EXPECT_EQ(added.status, 200);
    EXPECT_NE(added.body.find("&lt;b&gt;My TV&lt;/b&gt;"), std::string::npos) << "escaped";
    EXPECT_EQ(added.body.find("<b>My TV"), std::string::npos);

    const net::Response rejected = post(base + server.pin(), "name=x&url=javascript%3Aalert(1)");
    EXPECT_NE(rejected.body.find("http:// or https://"), std::string::npos);
    const net::Response wrong_pin = post(base + "nope", "url=https%3A%2F%2Fevil.example%2Fx.m3u");
    EXPECT_EQ(wrong_pin.status, 404);

    const auto got = server.take();
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0].name, "<b>My TV</b>");
    EXPECT_EQ(got[0].url, "https://tv.example/list.m3u");
    EXPECT_TRUE(server.take().empty());
    server.stop();
    EXPECT_FALSE(server.running());
}

TEST(FormServer, StopsAfterTooManyWrongRequests)
{
    net::Client::global_init();
    net::FormServer server;
    std::string error;
    ASSERT_TRUE(server.start(0, &error)) << error;
    const std::string base = "http://127.0.0.1:" + std::to_string(server.port()) + "/";
    for (int i = 0; i < net::FormServer::kMaxBadRequests; ++i)
        (void)get(base + "guess" + std::to_string(i));
    for (int i = 0; i < 50 && server.running(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(server.running());
    EXPECT_FALSE(get(base + server.pin()).ok());
}

TEST(FormServer, FindsTheLocalAddressShape)
{
    const std::string ip = net::local_ipv4();
    if (ip.empty())
        GTEST_SKIP() << "no network route on this machine";
    int dots = 0;
    for (char c : ip)
    {
        EXPECT_TRUE(std::isdigit(static_cast<unsigned char>(c)) || c == '.') << ip;
        dots += c == '.';
    }
    EXPECT_EQ(dots, 3) << ip;
}
