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
#include "filehandle.hpp"

#include <cerrno>
#include <cstring>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

// Apple doesn't have SOCK_CLOEXEC, but accept4 is not
// available either, so this is fine.
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif

namespace brokkr::posix_common {

TcpConnection::TcpConnection(int fd, std::string peer_ip,
                             std::uint16_t peer_port)
    : fd_(fd), peer_ip_(std::move(peer_ip)), peer_port_(peer_port) {
  set_sock_timeouts_();

  int one = 1;
  do_setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

TcpConnection::~TcpConnection() { close_(); }

TcpConnection::TcpConnection(TcpConnection &&o) noexcept {
  *this = std::move(o);
}

TcpConnection &TcpConnection::operator=(TcpConnection &&o) noexcept {
  if (this == &o)
    return *this;
  close_();
  fd_ = std::move(o.fd_);
  timeout_ms_ = o.timeout_ms_;
  peer_ip_ = std::move(o.peer_ip_);
  peer_port_ = o.peer_port_;
  o.peer_port_ = 0;
  return *this;
}

void TcpConnection::close_() noexcept {
  if (fd_.valid()) {
    spdlog::info("TcpConnection: closing connection to {}", peer_label());
    fd_.close();
  }
}

bool TcpConnection::connected() const noexcept { return fd_.valid(); }

void TcpConnection::set_sock_timeouts_() noexcept {
  timeval tv{};
  tv.tv_sec = timeout_ms_ / 1000;
  tv.tv_usec = (timeout_ms_ % 1000) * 1000;

  do_setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  do_setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
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
    const ssize_t n = do_send(fd_, p, left, MSG_NOSIGNAL);
    if (n > 0) {
      p += static_cast<std::size_t>(n);
      left -= static_cast<std::size_t>(n);
	  spdlog::debug("TcpConnection::send: sent {} bytes, {} bytes left", n, left);
      continue;
    }

    if (n == 0) {
      spdlog::warn("TcpConnection::send: peer closed connection");
      return 0;
    }

    if (errno == EINTR)
      continue;

    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      if (retries-- > 0) {
        spdlog::warn("TcpConnection::send: Timeout");
        ::usleep(10'000);
        continue;
      }
      spdlog::warn("TcpConnection::send: Timeout, giving up");
      return -1;
    }

    spdlog::error("TcpConnection::send: send error: {}", std::strerror(errno));
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
    spdlog::warn("TcpConnection::recv: empty buffer");
    return 0;
  }

  for (;;) {
    const ssize_t n = do_recv(fd_, data.data(), data.size(), 0);
    if (n > 0) {
		spdlog::debug("TcpConnection::recv: received {} bytes", n);
        return static_cast<int>(n);
    }
    if (n == 0) {
      spdlog::warn("TcpConnection::recv: peer closed connection");
      return 0;
    }

    if (errno == EINTR)
      continue;

    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      if (retries-- > 0) {
        spdlog::warn("TcpConnection::recv: Timeout");
        ::usleep(10'000);
        continue;
      }
      spdlog::warn("TcpConnection::recv: Timeout, giving up");
      return -1;
    }
    spdlog::error("TcpConnection::recv: recv error: {}", std::strerror(errno));
    return -1;
  }
}

TcpListener::~TcpListener() {
  if (fd_.valid()) {
    spdlog::info("TcpListener: closing listener on {}:{}", bind_ip_, port_);
    fd_.close();
  }
}

bool TcpListener::bind_and_listen(std::string bind_ip, std::uint16_t port,
                                  int backlog) {
  if (fd_.valid()) {
    spdlog::warn(
        "TcpListener: already listening on {}:{}, closing old listener",
        bind_ip_, port_);
  }

  bind_ip_ = std::move(bind_ip);
  port_ = port;

  fd_.take(do_socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
  if (!fd_.valid()) {
    spdlog::error("TcpListener::bind_and_listen: socket error: {}",
                  std::strerror(errno));
    return false;
  }

  int one = 1;
  do_setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  if (::inet_pton(AF_INET, bind_ip_.c_str(), &addr.sin_addr) != 1) {
    spdlog::error("TcpListener::bind_and_listen: invalid bind IP: {}",
                  bind_ip_);
    fd_.close();
    return false;
  }

  if (do_bind(fd_, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) !=
      0) {
    spdlog::error("TcpListener::bind_and_listen: bind error: {}",
                  std::strerror(errno));
    fd_.close();
    return false;
  }
  if (do_listen(fd_, backlog) != 0) {
    spdlog::error("TcpListener::bind_and_listen: listen error: {}",
                  std::strerror(errno));
    fd_.close();
    return false;
  }
  spdlog::info("TcpListener: listening on {}:{}", bind_ip_, port_);
  return true;
}

std::optional<TcpConnection> TcpListener::accept_one() {
  if (!fd_.valid()) {
    spdlog::error("TcpListener::accept_one: not listening");
    return std::nullopt;
  }

  sockaddr_in peer{};
  socklen_t peer_len = sizeof(peer);

  const int cfd = do_accept(fd_, reinterpret_cast<sockaddr *>(&peer), &peer_len,
                            SOCK_CLOEXEC);
  if (cfd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return std::nullopt;
    }
    spdlog::error("TcpListener::accept_one: accept error: {}",
                  std::strerror(errno));
    return std::nullopt;
  }

  char ipbuf[INET_ADDRSTRLEN]{};
  const char *ip = ::inet_ntop(AF_INET, &peer.sin_addr, ipbuf, sizeof(ipbuf));
  if (!ip) {
      spdlog::error("TcpListener::accept_one: inet_ntop error: {}",
                  std::strerror(errno));
    ::close(cfd);
	return std::nullopt;
  }

  const std::uint16_t p = ntohs(peer.sin_port);

  spdlog::info("TcpListener: accepted connection from {}:{}", ip, p);

  return TcpConnection(cfd, std::string(ip), p);
}

} // namespace brokkr::posix_common
