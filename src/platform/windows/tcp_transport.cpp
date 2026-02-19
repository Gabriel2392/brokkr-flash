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

#include <chrono>
#include <cstring>
#include <utility>

// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
// clang-format on

#include <spdlog/spdlog.h>

#pragma comment(lib, "Ws2_32.lib")

namespace brokkr::windows {

static void set_nonblocking_(SOCKET s, bool on) noexcept {
  u_long mode = on ? 1UL : 0UL;
  (void)::ioctlsocket(s, FIONBIO, &mode);
}

TcpConnection::TcpConnection(SOCKET fd, std::string peer_ip, std::uint16_t peer_port)
  : fd_(fd), peer_ip_(std::move(peer_ip)), peer_port_(peer_port)
{
  if (fd_ != INVALID_SOCKET) set_nonblocking_(fd_, true);

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

bool TcpConnection::connected() const noexcept {
  if (fd_ == INVALID_SOCKET) return false;

  WSAPOLLFD pfd{};
  pfd.fd = fd_;
  pfd.events = POLLRDNORM;

  const int r = WSAPoll(&pfd, 1, 0);
  if (r == SOCKET_ERROR) {
	spdlog::warn("TcpConnection::connected: WSAPoll failed: {}", WSAGetLastError());
	return false;
  }

  if (pfd.revents & POLLNVAL) return false;

  if (r == 0) return true;

  if (pfd.revents & POLLHUP) return false;

  if (pfd.revents & POLLRDNORM) {
	char c = 0;
	const int n = ::recv(fd_, &c, 1, MSG_PEEK);
	if (n == 0) return false;
	if (n < 0) {
	  const int err = WSAGetLastError();
	  if (err == WSAEWOULDBLOCK) return true;
	  return false;
	}
  }

  return true;
}

void TcpConnection::set_sock_timeouts_() noexcept {
  if (fd_ == INVALID_SOCKET) return;

  DWORD tv = static_cast<DWORD>(timeout_ms_);
  (void)::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
  (void)::setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

  int one = 1;
  (void)::setsockopt(fd_, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&one), sizeof(one));

  tcp_keepalive ka{};
  ka.onoff = 1;
  ka.keepalivetime = 2000;
  ka.keepaliveinterval = 1000;

  DWORD bytes = 0;
  (void)::WSAIoctl(fd_, SIO_KEEPALIVE_VALS, &ka, sizeof(ka), nullptr, 0, &bytes, nullptr, nullptr);
}

void TcpConnection::set_timeout_ms(int ms) noexcept {
  timeout_ms_ = (ms <= 0) ? 1 : ms;
  set_sock_timeouts_();
}

std::string TcpConnection::peer_label() const { return fmt::format("{}:{}", peer_ip_, peer_port_); }

int TcpConnection::send(std::span<const std::uint8_t> data, unsigned /*retries*/) {
  if (fd_ == INVALID_SOCKET) return -1;
  if (!connected()) return -1;

  const std::uint8_t* p = data.data();
  std::size_t left = data.size();

  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);

  while (left) {
    int ms_left = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count()
    );
    if (ms_left <= 0) return -1;

    WSAPOLLFD pfd{};
    pfd.fd = fd_;
    pfd.events = POLLWRNORM;

    const int pr = WSAPoll(&pfd, 1, ms_left);
    if (pr == 0) return -1;
    if (pr == SOCKET_ERROR) {
      const int err = WSAGetLastError();
      if (err == WSAEINTR) continue;
      return -1;
    }
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return -1;

    const int n = ::send(fd_, reinterpret_cast<const char*>(p), static_cast<int>(left), 0);
    if (n > 0) {
      p += static_cast<std::size_t>(n);
      left -= static_cast<std::size_t>(n);
      deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);
      continue;
    }

    if (n == 0) return -1;

    const int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) continue;
    return -1;
  }

  return static_cast<int>(data.size());
}

int TcpConnection::recv(std::span<std::uint8_t> data, unsigned /*retries*/) {
  if (fd_ == INVALID_SOCKET) return -1;
  if (!connected()) return -1;
  if (data.empty()) return 0;

  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);

  for (;;) {
    int ms_left = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count()
    );
    if (ms_left <= 0) return -1;

    WSAPOLLFD pfd{};
    pfd.fd = fd_;
    pfd.events = POLLRDNORM;

    const int pr = WSAPoll(&pfd, 1, ms_left);
    if (pr == 0) return -1;
    if (pr == SOCKET_ERROR) {
      const int err = WSAGetLastError();
      if (err == WSAEINTR) continue;
      return -1;
    }
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return -1;

    const int n = ::recv(fd_, reinterpret_cast<char*>(data.data()), static_cast<int>(data.size()), 0);
    if (n > 0) return n;
    if (n == 0) return -1;

    const int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) continue;
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
    if (err) return brokkr::core::failf("WSAStartup failed: {}", err);
    wsa_init_ = true;
  }

  if (fd_ != INVALID_SOCKET) {
    ::closesocket(fd_);
    fd_ = INVALID_SOCKET;
  }

  bind_ip_ = std::move(bind_ip);
  port_ = port;

  fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd_ == INVALID_SOCKET) return brokkr::core::failf("socket failed: {}", WSAGetLastError());

  set_nonblocking_(fd_, true);

  int one = 1;
  (void)::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  if (::InetPtonA(AF_INET, bind_ip_.c_str(), &addr.sin_addr) != 1) return brokkr::core::failf("Invalid bind ip: {}", bind_ip_);

  if (::bind(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) return brokkr::core::failf("bind failed: {}", WSAGetLastError());
  if (::listen(fd_, backlog) == SOCKET_ERROR) return brokkr::core::failf("listen failed: {}", WSAGetLastError());

  spdlog::debug("TcpListener: listening on {}:{}", bind_ip_, port_);
  return {};
}

brokkr::core::Result<TcpConnection> TcpListener::accept_one() noexcept {
  if (fd_ == INVALID_SOCKET) return brokkr::core::fail("TcpListener: not listening");

  WSAPOLLFD pfd{};
  pfd.fd = fd_;
  pfd.events = POLLRDNORM;

  const int pr = WSAPoll(&pfd, 1, 1000);
  if (pr == 0) return brokkr::core::fail("accept: timeout");
  if (pr == SOCKET_ERROR) {
    const int err = WSAGetLastError();
    if (err == WSAEINTR) return brokkr::core::fail("accept: timeout");
    return brokkr::core::failf("poll: {}", err);
  }
  if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return brokkr::core::fail("accept: listener closed");

  sockaddr_in peer{};
  int peer_len = sizeof(peer);

  SOCKET cfd = ::accept(fd_, reinterpret_cast<sockaddr*>(&peer), &peer_len);
  if (cfd == INVALID_SOCKET) {
    const int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) return brokkr::core::fail("accept: timeout");
    return brokkr::core::failf("accept failed: {}", err);
  }

  set_nonblocking_(cfd, true);

  char ipbuf[INET_ADDRSTRLEN]{};
  const char* ip = ::inet_ntop(AF_INET, &peer.sin_addr, ipbuf, sizeof(ipbuf));
  if (!ip) { ::closesocket(cfd); return brokkr::core::failf("inet_ntop failed: {}", WSAGetLastError()); }

  const std::uint16_t p = ntohs(peer.sin_port);
  spdlog::debug("TcpListener: accepted {}:{}", ip, p);
  return TcpConnection(cfd, std::string(ip), p);
}

} // namespace brokkr::windows
