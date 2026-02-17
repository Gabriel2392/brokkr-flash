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

#include "platform/windows/tcp_transport.hpp"

#include <utility>

// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
// clang-format on

#include <spdlog/spdlog.h>

#pragma comment(lib, "Ws2_32.lib")

namespace brokkr::windows {

static void backoff_10ms() noexcept { ::Sleep(10); }

TcpConnection::TcpConnection(SOCKET fd, std::string peer_ip, std::uint16_t peer_port)
  : fd_(fd)
  , peer_ip_(std::move(peer_ip))
  , peer_port_(peer_port)
{
  set_sock_timeouts_();
  int one = 1;
  (void)::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
}

TcpConnection::~TcpConnection() { close_(); }

TcpConnection::TcpConnection(TcpConnection&& o) noexcept { *this = std::move(o); }

TcpConnection& TcpConnection::operator=(TcpConnection&& o) noexcept {
  if (this == &o) return *this;
  close_();
  fd_ = o.fd_;
  o.fd_ = INVALID_SOCKET;
  timeout_ms_ = o.timeout_ms_;
  peer_ip_ = std::move(o.peer_ip_);
  peer_port_ = o.peer_port_;
  o.peer_port_ = 0;
  return *this;
}

void TcpConnection::close_() noexcept {
  if (fd_ != INVALID_SOCKET) {
    ::closesocket(fd_);
    fd_ = INVALID_SOCKET;
  }
}

bool TcpConnection::connected() const noexcept { return fd_ != INVALID_SOCKET; }

void TcpConnection::set_sock_timeouts_() noexcept {
  if (!connected()) return;

  DWORD tv = static_cast<DWORD>(timeout_ms_);
  (void)::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
  (void)::setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
}

void TcpConnection::set_timeout_ms(int ms) noexcept {
  timeout_ms_ = (ms <= 0) ? 1 : ms;
  set_sock_timeouts_();
}

std::string TcpConnection::peer_label() const {
  return fmt::format("{}:{}", peer_ip_, peer_port_);
}

int TcpConnection::send(std::span<const std::uint8_t> data, unsigned retries) {
  if (!connected()) return -1;

  const std::uint8_t* p = data.data();
  std::size_t left = data.size();

  while (left) {
    const int n = ::send(fd_, reinterpret_cast<const char*>(p), static_cast<int>(left), 0);
    if (n > 0) {
      p += static_cast<std::size_t>(n);
      left -= static_cast<std::size_t>(n);
      continue;
    }

    if (n == 0) return -1;

    const int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT) {
      if (retries-- > 0) { backoff_10ms(); continue; }
      return -1;
    }
    return -1;
  }

  return static_cast<int>(data.size());
}

int TcpConnection::recv(std::span<std::uint8_t> data, unsigned retries) {
  if (!connected()) return -1;
  if (data.empty()) return 0;

  for (;;) {
    const int n = ::recv(fd_, reinterpret_cast<char*>(data.data()), static_cast<int>(data.size()), 0);
    if (n > 0) return n;
    if (n == 0) return -1;

    const int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT) {
      if (retries-- > 0) { backoff_10ms(); continue; }
      return -1;
    }
    return -1;
  }
}

TcpListener::~TcpListener() {
  if (fd_ != INVALID_SOCKET) ::closesocket(fd_);
  fd_ = INVALID_SOCKET;
  if (wsa_init_) WSACleanup();
}

brokkr::core::Status TcpListener::bind_and_listen(std::string bind_ip, std::uint16_t port, int backlog) noexcept {
  if (!wsa_init_) {
    const int err = WSAStartup(MAKEWORD(2, 2), &ws_);
    if (err) return brokkr::core::Status::Failf("WSAStartup failed: {}", err);
    wsa_init_ = true;
  }

  if (fd_ != INVALID_SOCKET) {
    ::closesocket(fd_);
    fd_ = INVALID_SOCKET;
  }

  bind_ip_ = std::move(bind_ip);
  port_ = port;

  fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd_ == INVALID_SOCKET) return brokkr::core::Status::Failf("socket failed: {}", WSAGetLastError());

  int one = 1;
  (void)::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  if (::InetPton(AF_INET, bind_ip_.c_str(), &addr.sin_addr) != 1) {
    return brokkr::core::Status::Failf("Invalid bind ip: {}", bind_ip_);
  }

  if (::bind(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    return brokkr::core::Status::Failf("bind failed: {}", WSAGetLastError());
  }

  if (::listen(fd_, backlog) == SOCKET_ERROR) {
    return brokkr::core::Status::Failf("listen failed: {}", WSAGetLastError());
  }

  spdlog::info("TcpListener: listening on {}:{}", bind_ip_, port_);
  return brokkr::core::Status::Ok();
}

brokkr::core::Result<TcpConnection> TcpListener::accept_one() noexcept {
  if (fd_ == INVALID_SOCKET) return brokkr::core::Result<TcpConnection>::Fail("TcpListener: not listening");

  sockaddr_in peer{};
  int peer_len = sizeof(peer);

  SOCKET cfd = ::accept(fd_, reinterpret_cast<sockaddr*>(&peer), &peer_len);
  if (cfd == INVALID_SOCKET) return brokkr::core::Result<TcpConnection>::Failf("accept failed: {}", WSAGetLastError());

  char ipbuf[INET_ADDRSTRLEN]{};
  const char* ip = ::inet_ntop(AF_INET, &peer.sin_addr, ipbuf, sizeof(ipbuf));
  if (!ip) {
    ::closesocket(cfd);
    return brokkr::core::Result<TcpConnection>::Failf("inet_ntop failed: {}", WSAGetLastError());
  }

  const std::uint16_t p = ntohs(peer.sin_port);
  spdlog::info("TcpListener: accepted {}:{}", ip, p);
  return brokkr::core::Result<TcpConnection>::Ok(TcpConnection(cfd, std::string(ip), p));
}

} // namespace brokkr::windows
