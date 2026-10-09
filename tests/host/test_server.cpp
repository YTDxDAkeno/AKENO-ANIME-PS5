// AKENO STREAM PS5 - Minimal local HTTP server for integration tests.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_server.hpp"

#include <arpa/inet.h>
#include <fstream>
#include <iterator>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace akeno::test
{
TestServer::TestServer(std::string root) : root_{std::move(root)}
{
    listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    ::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    ::bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    ::listen(listener_, 16);
    socklen_t length = sizeof(address);
    ::getsockname(listener_, reinterpret_cast<sockaddr *>(&address), &length);
    port_ = ntohs(address.sin_port);
    thread_ = std::thread([this] { serve(); });
}

TestServer::~TestServer()
{
    running_.store(false);
    if (thread_.joinable())
        thread_.join();
    ::close(listener_);
}

std::string TestServer::url(const std::string &path) const
{
    return "http://127.0.0.1:" + std::to_string(port_) + "/" + path;
}

void TestServer::fail(const std::string &path, int status)
{
    std::lock_guard<std::mutex> guard(lock_);
    failures_["/" + path] = status;
}

void TestServer::override_body(const std::string &path, std::string body)
{
    std::lock_guard<std::mutex> guard(lock_);
    bodies_["/" + path] = std::move(body);
}

void TestServer::serve()
{
    while (running_.load())
    {
        pollfd p{listener_, POLLIN, 0};
        if (::poll(&p, 1, 50) <= 0)
            continue;
        const int client = ::accept(listener_, nullptr, nullptr);
        if (client >= 0)
        {
            handle(client);
            ::close(client);
        }
    }
}

void TestServer::handle(int client)
{
    std::string request;
    char buffer[4096];
    while (request.find("\r\n\r\n") == std::string::npos)
    {
        const ssize_t got = ::recv(client, buffer, sizeof(buffer), 0);
        if (got <= 0)
            return;
        request.append(buffer, static_cast<std::size_t>(got));
        if (request.size() > 65536)
            return;
    }
    ++requests_;
    const std::size_t first_space = request.find(' ');
    const std::size_t second_space = request.find(' ', first_space + 1);
    std::string path = request.substr(first_space + 1, second_space - first_space - 1);
    if (const std::size_t q = path.find('?'); q != std::string::npos)
        path.resize(q);
    int status = 200;
    std::string body;
    {
        std::lock_guard<std::mutex> guard(lock_);
        if (const auto f = failures_.find(path); f != failures_.end())
        {
            status = f->second;
            body = "<html>error</html>";
        }
        else if (const auto b = bodies_.find(path); b != bodies_.end())
        {
            body = b->second;
        }
        else if (path.find("..") != std::string::npos)
        {
            status = 403;
        }
        else
        {
            std::ifstream in(root_ + path, std::ios::binary);
            if (!in)
            {
                status = 404;
                body = "not found";
            }
            else
            {
                body.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            }
        }
    }
    std::string header = "HTTP/1.1 " + std::to_string(status) +
                         " X\r\nContent-Length: " + std::to_string(body.size()) +
                         "\r\nConnection: close\r\n\r\n";
    std::string response = header + body;
    std::size_t sent = 0;
    while (sent < response.size())
    {
        const ssize_t n =
            ::send(client, response.data() + sent, response.size() - sent, MSG_NOSIGNAL);
        if (n <= 0)
            return;
        sent += static_cast<std::size_t>(n);
    }
}
} // namespace akeno::test
