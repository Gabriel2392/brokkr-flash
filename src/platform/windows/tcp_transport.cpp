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

#include <spdlog/spdlog.h>

#pragma comment(lib, "Ws2_32.lib")

#define do_setsocketopt(sock, optlevel, optname, optval, optlen)               \
  do {                                                                         \
    if (::setsockopt(sock, optlevel, optname, optval, optlen) != 0) {          \
      spdlog::error("setsockopt({}, {}, {}, <ptr>, {}): {}", sock, #optlevel,  \
                    #optname, optlen, WSAGetLastError());                      \
    }                                                                          \
  } while (0)

namespace {
void usleep(unsigned int usec) {
  HANDLE timer{};
  LARGE_INTEGER ft{};

  ft.QuadPart = -(10 * (__int64)usec);

  timer = CreateWaitableTimer(NULL, TRUE, NULL);
  if (!timer) {
      spdlog::error("CreateWaitableTimer failed with error code {}", GetLastError());
	  return;
  }
  spdlog::debug("Sleeping for {} microseconds", usec);
  SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
  WaitForSingleObject(timer, INFINITE);
  spdlog::debug("Woke up after sleeping for {} microseconds", usec);
  CloseHandle(timer);
}
} // namespace

namespace brokkr::windows {

using ssize_t = std::make_signed_t<std::size_t>;

TcpConnection::TcpConnection(int fd, std::string peer_ip,
                             std::uint16_t peer_port)
    : fd_(fd), peer_ip_(std::move(peer_ip)), peer_port_(peer_port) {

  if (int err = WSAStartup(MAKEWORD(2, 2), &ws_); err) {
    spdlog::error("WSAStartup failed with error code {}", err);
    throw std::runtime_error("Cannot initialize WSA: code " +
                             std::to_string(err));
  }

  set_sock_timeouts_();

  int one = 1;
  do_setsocketopt(fd_, IPPROTO_TCP, TCP_NODELAY, (const char *)&one,
                  sizeof(one));
}

TcpConnection::~TcpConnection() {
  close_();
  WSACleanup();
}

TcpConnection::TcpConnection(TcpConnection &&o) noexcept {
  *this = std::move(o);
}

TcpConnection &TcpConnection::operator=(TcpConnection &&o) noexcept {
  if (this == &o)
    return *this;
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
  if (!connected()) {
 	spdlog::error("TcpConnection::set_sock_timeouts_: not connected");
	return;
  }

  DWORD tv = static_cast<DWORD>(timeout_ms_);

  spdlog::info("Timeout is {}ms", timeout_ms_);

  do_setsocketopt(fd_, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
  do_setsocketopt(fd_, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
}

void TcpConnection::set_timeout_ms(int ms) noexcept {
  timeout_ms_ = (ms <= 0) ? 1 : ms;
  set_sock_timeouts_();
}

std::string TcpConnection::peer_label() const {
  return fmt::format("{}:{}", peer_ip_, peer_port_);
}

int TcpConnection::send(std::span<const std::uint8_t> data, unsigned retries) {
  if (!connected()) {
    spdlog::error("TcpConnection::send: not connected");
    return -1;
  }

  const std::uint8_t *p = data.data();
  std::size_t left = data.size();

  while (left) {
    const ssize_t n = ::send(fd_, (const char *)p, left, 0);
    if (n > 0) {
      p += static_cast<std::size_t>(n);
      left -= static_cast<std::size_t>(n);
	  spdlog::debug("TcpConnection::send: sent {} bytes, {} bytes left", n, left);
      continue;
    }

    if (n == 0) {
      spdlog::warn("TcpConnection::send: connection closed by peer");
      return 0;
    }

    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) {
      if (retries-- > 0) {
        spdlog::warn("TcpConnection::send: send would block, retrying... ({} "
                     "retries left)",
                     retries);
        ::usleep(10'000);
        continue;
      }
      spdlog::error("TcpConnection::send: send would block, no retries left");
      return -1;
    }

    spdlog::error("TcpConnection::send: send failed with error code {}", err);
    return -1;
  }

  return static_cast<int>(data.size());
}

int TcpConnection::recv(std::span<std::uint8_t> data, unsigned retries) {
  if (!connected()) {
    spdlog::error("TcpConnection::recv: not connected");
    return -1;
  }
  if (data.empty()) {
    spdlog::warn("TcpConnection::recv: empty buffer provided");
  }

  for (;;) {
    const ssize_t n =
        ::recv(fd_, reinterpret_cast<char *>(data.data()), data.size(), 0);
    if (n > 0) {
		spdlog::debug("TcpConnection::recv: received {} bytes", n);
		return static_cast<int>(n);
    }
    if (n == 0) {
      spdlog::warn("TcpConnection::recv: connection closed by peer");
      return 0;
    }

    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) {
      if (retries-- > 0) {
        spdlog::warn("TcpConnection::recv: recv would block, retrying... ({} "
                     "retries left)",
                     retries);
        ::usleep(10'000);
        continue;
      }
      spdlog::error("TcpConnection::recv: recv would block, no retries left");
      return -1;
    }

    spdlog::error("TcpConnection::recv: recv failed with error code {}", err);
    return -1;
  }
}

TcpListener::TcpListener() {
  if (int err = WSAStartup(MAKEWORD(2, 2), &ws_); err) {
    spdlog::error("WSAStartup failed with error code {}", err);
    throw std::runtime_error("Cannot initialize WSA: code " +
                             std::to_string(err));
  }
}

TcpListener::~TcpListener() {
  if (fd_ != INVALID_SOCKET)
    ::closesocket(fd_);
  fd_ = INVALID_SOCKET;
  WSACleanup();
}

bool TcpListener::bind_and_listen(std::string bind_ip, std::uint16_t port,
                                  int backlog) {
  if (fd_ != INVALID_SOCKET) {
    ::closesocket(fd_);
    fd_ = INVALID_SOCKET;
  }

  bind_ip_ = std::move(bind_ip);
  port_ = port;

  fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd_ == INVALID_SOCKET) {
    spdlog::error("socket() failed with error code {}", WSAGetLastError());
    return false;
  }

  int one = 1;
  do_setsocketopt(fd_, SOL_SOCKET, SO_REUSEADDR, (const char *)&one,
                  sizeof(one));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  if (::InetPton(AF_INET, bind_ip_.c_str(), &addr.sin_addr) != 1) {
    spdlog::error("Invalid bind IP address '{}'", bind_ip_);
	return false;
  }

  if (::bind(fd_, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    spdlog::error("bind() failed with error code {}", WSAGetLastError());
    return false;
  }

  if (::listen(fd_, backlog) == SOCKET_ERROR) {
    spdlog::error("listen() failed with error code {}", WSAGetLastError());
    return false;
  }
  return true;
}

std::optional<TcpConnection> TcpListener::accept_one() {
  if (fd_ == INVALID_SOCKET) {
    spdlog::error("TcpListener::accept_one: not listening");
	return std::nullopt;
  }

  sockaddr_in peer{};
  socklen_t peer_len = sizeof(peer);

  const int cfd = ::accept(fd_, reinterpret_cast<sockaddr *>(&peer), &peer_len);
  if (cfd == INVALID_SOCKET) {
    spdlog::error("accept() failed with error code {}", WSAGetLastError());
    return std::nullopt;
  }

  char ipbuf[INET_ADDRSTRLEN]{};
  const char *ip = ::inet_ntop(AF_INET, &peer.sin_addr, ipbuf, sizeof(ipbuf));
  if (!ip) {
    spdlog::error("inet_ntop() failed with error code {}", WSAGetLastError());
    return std::nullopt;
  }
  const std::uint16_t p = ntohs(peer.sin_port);

  spdlog::info("Accepted connection from {}:{}", ip, p);

  return TcpConnection(cfd, std::string(ip), p);
}

} // namespace brokkr::windows
