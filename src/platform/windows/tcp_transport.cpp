/*
 * Copyright (c) 2026 Gabriel2392
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "tcp_transport.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>

// clang-format off
#include <winsock.h>
#include <afunix.h>
#include <ws2tcpip.h>
// clang-format on

#pragma comment(lib, "Ws2_32.lib")

namespace {
    void usleep(unsigned int usec)
    {
        HANDLE timer{};
        LARGE_INTEGER ft{};

        ft.QuadPart = -(10 * (__int64)usec);

        timer = CreateWaitableTimer(NULL, TRUE, NULL);
		if (!timer) return;
        SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
        WaitForSingleObject(timer, INFINITE);
        CloseHandle(timer);
    }
}

namespace brokkr::windows {

using ssize_t = std::make_signed_t<std::size_t>;

[[noreturn]] static void throw_winsockerr(const char* what) {
    throw std::runtime_error(std::string(what) + ": " + std::to_string(WSAGetLastError()));
}

TcpConnection::TcpConnection(int fd, std::string peer_ip, std::uint16_t peer_port)
    : fd_(fd), peer_ip_(std::move(peer_ip)), peer_port_(peer_port)
{

    if (int err = WSAStartup(MAKEWORD(2, 2), &ws_); err) {
        throw std::runtime_error("Cannot initialize WSA: code " + std::to_string(err));
    }

    (void)set_sock_timeouts_();

    int one = 1;
    (void)::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, (const char*) & one, sizeof(one));
}

TcpConnection::~TcpConnection() { 
    close_();
    WSACleanup();
}

TcpConnection::TcpConnection(TcpConnection&& o) noexcept { *this = std::move(o); }

TcpConnection& TcpConnection::operator=(TcpConnection&& o) noexcept {
    if (this == &o) return *this;
    close_();
    fd_ = o.fd_; o.fd_ = -1;
    timeout_ms_ = o.timeout_ms_;
    peer_ip_ = std::move(o.peer_ip_);
    peer_port_ = o.peer_port_;
    o.peer_port_ = 0;
    return *this;
}

void TcpConnection::close_() noexcept {
    if (fd_ >= 0) {
        ::closesocket(fd_);
        fd_ = -1;
    }
}

bool TcpConnection::connected() const noexcept { return fd_ >= 0; }

bool TcpConnection::set_sock_timeouts_() noexcept {
    if (fd_ < 0) return false;

    timeval tv{};
    tv.tv_sec  = timeout_ms_ / 1000;
    tv.tv_usec = (timeout_ms_ % 1000) * 1000;

    (void)::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, (const char*) &tv, sizeof(tv));
    (void)::setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, (const char*) &tv, sizeof(tv));
    return true;
}

void TcpConnection::set_timeout_ms(int ms) noexcept {
    timeout_ms_ = (ms <= 0) ? 1 : ms;
    (void)set_sock_timeouts_();
}

std::string TcpConnection::peer_label() const {
    return peer_ip_ + ":" + std::to_string(peer_port_);
}

int TcpConnection::send(std::span<const std::uint8_t> data, unsigned retries) {
    if (fd_ == INVALID_SOCKET) return -1;

    const std::uint8_t* p = data.data();
    std::size_t left = data.size();

    while (left) {
        const ssize_t n = ::send(fd_, (const char *)p, left, 0);
        if (n > 0) {
            p += static_cast<std::size_t>(n);
            left -= static_cast<std::size_t>(n);
            continue;
        }

        if (n == 0) return -1;

        if (errno == EINTR) continue;

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (retries-- > 0) {
                ::usleep(10'000);
                continue;
            }
            return -1;
        }

        return -1;
    }

    return static_cast<int>(data.size());
}

int TcpConnection::recv(std::span<std::uint8_t> data, unsigned retries) {
    if (fd_ == INVALID_SOCKET) return -1;
    if (data.empty()) return 0;

    for (;;) {
        const ssize_t n = ::recv(fd_, reinterpret_cast<char*>(data.data()), data.size(), 0);
        if (n > 0) return static_cast<int>(n);
        if (n == 0) return -1;

        if (errno == EINTR) continue;

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (retries-- > 0) {
                ::usleep(10'000);
                continue;
            }
            return -1;
        }

        return -1;
    }
}

TcpListener::TcpListener() {
    if (int err = WSAStartup(MAKEWORD(2, 2), &ws_); err) {
        throw std::runtime_error("Cannot initialize WSA: code " + std::to_string(err));
    }
}

TcpListener::~TcpListener() {
    if (fd_ != INVALID_SOCKET) ::closesocket(fd_);
    fd_ = INVALID_SOCKET;
    WSACleanup();
}

void TcpListener::bind_and_listen(std::string bind_ip, std::uint16_t port, int backlog) {
    if (fd_ != INVALID_SOCKET) { ::closesocket(fd_); fd_ = -1; }

    bind_ip_ = std::move(bind_ip);
    port_ = port;

    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) throw_winsockerr("socket");

    int one = 1;
    (void)::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, (const char* ) & one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (::InetPton(AF_INET, bind_ip_.c_str(), &addr.sin_addr) != 1) {
        throw std::runtime_error("Invalid bind IP: " + bind_ip_);
    }

    if (::bind(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        throw_winsockerr("bind");
    }
    if (::listen(fd_, backlog) != 0) {
        throw_winsockerr("listen");
    }
}

TcpConnection TcpListener::accept_one() {
    if (fd_ == INVALID_SOCKET) throw std::runtime_error("TcpListener: not listening");

    sockaddr_in peer{};
    socklen_t peer_len = sizeof(peer);

    const int cfd = ::accept(fd_, reinterpret_cast<sockaddr*>(&peer), &peer_len);
    if (cfd < 0) throw_winsockerr("accept");

    char ipbuf[INET_ADDRSTRLEN]{};
    const char* ip = ::inet_ntop(AF_INET, &peer.sin_addr, ipbuf, sizeof(ipbuf));
    if (!ip) ip = "unknown";

    const std::uint16_t p = ntohs(peer.sin_port);
    return TcpConnection(cfd, std::string(ip), p);
}

} // namespace brokkr::linux
