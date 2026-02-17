/*
 *
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

#include "platform/posix-common/tcp_transport.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif

namespace brokkr::posix_common {

TcpConnection::TcpConnection(int fd, std::string peer_ip, std::uint16_t peer_port)
  : fd_(fd)
  , peer_ip_(std::move(peer_ip))
  , peer_port_(peer_port)
{
  set_sock_timeouts_();

  int one = 1;
  (void)do_setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

TcpConnection::~TcpConnection() { close_(); }

TcpConnection::TcpConnection(TcpConnection&& o) noexcept { *this = std::move(o); }

TcpConnection& TcpConnection::operator=(TcpConnection&& o) noexcept {
  if (this == &o) return *this;
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
    spdlog::debug("TcpConnection: close {}", peer_label());
    fd_.close();
  }
}

bool TcpConnection::connected() const noexcept {
  if (!fd_.valid()) return false;

  unsigned char c = 0;
  const ssize_t n = ::recv(fd_.fd, &c, 1, MSG_PEEK | MSG_DONTWAIT);
  if (n > 0) return true;
  if (n == 0) return false;

  const int e = errno;
  if (e == EAGAIN || e == EWOULDBLOCK) return true;
  return false;
}

void TcpConnection::set_sock_timeouts_() noexcept {
  if (!fd_.valid()) return;

  timeval tv{};
  tv.tv_sec  = timeout_ms_ / 1000;
  tv.tv_usec = (timeout_ms_ % 1000) * 1000;

  (void)do_setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  (void)do_setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  int one = 1;
  (void)do_setsockopt(fd_, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));

#ifdef TCP_KEEPIDLE
  int idle = 2;
  (void)do_setsockopt(fd_, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
#endif
#ifdef TCP_KEEPINTVL
  int intvl = 1;
  (void)do_setsockopt(fd_, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
#endif
#ifdef TCP_KEEPCNT
  int cnt = 2;
  (void)do_setsockopt(fd_, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
#endif

#ifdef TCP_USER_TIMEOUT
  int uto = timeout_ms_;
  (void)do_setsockopt(fd_, IPPROTO_TCP, TCP_USER_TIMEOUT, &uto, sizeof(uto));
#endif
}

void TcpConnection::set_timeout_ms(int ms) noexcept {
  timeout_ms_ = (ms <= 0) ? 1 : ms;
  set_sock_timeouts_();
}

std::string TcpConnection::peer_label() const {
  return fmt::format("{}:{}", peer_ip_, peer_port_);
}

int TcpConnection::send(std::span<const std::uint8_t> data, unsigned /*retries*/) {
  if (!connected()) return -1;

  const std::uint8_t* p = data.data();
  std::size_t left = data.size();

  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);

  while (left) {
    int ms_left = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count()
    );
    if (ms_left <= 0) {
      spdlog::warn("TcpConnection::send: timeout (giving up)");
      return -1;
    }

    pollfd pfd{};
    pfd.fd = fd_.fd;
    pfd.events = POLLOUT;

    const int pr = ::poll(&pfd, 1, ms_left);
    if (pr == 0) {
      spdlog::warn("TcpConnection::send: timeout (giving up)");
      return -1;
    }
    if (pr < 0) {
      const int e = errno;
      if (e == EINTR) continue;
      spdlog::error("TcpConnection::send: poll: {}", std::strerror(e));
      return -1;
    }
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
      spdlog::warn("TcpConnection::send: peer closed");
      return -1;
    }

    const ssize_t n = do_send(fd_, p, left, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (n > 0) {
      p += static_cast<std::size_t>(n);
      left -= static_cast<std::size_t>(n);

      deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);

      if (spdlog::should_log(spdlog::level::debug)) {
        spdlog::debug("TcpConnection::send {} bytes ({} left)", n, left);
      }
      continue;
    }

    if (n == 0) {
      spdlog::warn("TcpConnection::send: peer closed");
      return -1;
    }

    const int e = errno;
    if (e == EINTR) continue;
    if (e == EAGAIN || e == EWOULDBLOCK) continue;

    spdlog::error("TcpConnection::send: {}", std::strerror(e));
    return -1;
  }

  return static_cast<int>(data.size());
}

int TcpConnection::recv(std::span<std::uint8_t> data, unsigned /*retries*/) {
  if (!connected()) return -1;
  if (data.empty()) return 0;

  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);

  for (;;) {
    int ms_left = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count()
    );
    if (ms_left <= 0) {
      spdlog::warn("TcpConnection::recv: timeout (giving up)");
      return -1;
    }

    pollfd pfd{};
    pfd.fd = fd_.fd;
    pfd.events = POLLIN;

    const int pr = ::poll(&pfd, 1, ms_left);
    if (pr == 0) {
      spdlog::warn("TcpConnection::recv: timeout (giving up)");
      return -1;
    }
    if (pr < 0) {
      const int e = errno;
      if (e == EINTR) continue;
      spdlog::error("TcpConnection::recv: poll: {}", std::strerror(e));
      return -1;
    }
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
      spdlog::warn("TcpConnection::recv: peer closed");
      return -1;
    }

    const ssize_t n = do_recv(fd_, data.data(), data.size(), MSG_DONTWAIT);
    if (n > 0) {
      if (spdlog::should_log(spdlog::level::debug)) {
        spdlog::debug("TcpConnection::recv {} bytes", n);
      }
      return static_cast<int>(n);
    }
    if (n == 0) {
      spdlog::warn("TcpConnection::recv: peer closed");
      return -1;
    }

    const int e = errno;
    if (e == EINTR) continue;
    if (e == EAGAIN || e == EWOULDBLOCK) continue;

    spdlog::error("TcpConnection::recv: {}", std::strerror(e));
    return -1;
  }
}

TcpListener::~TcpListener() {
  if (fd_.valid()) {
    spdlog::debug("TcpListener: close {}:{}", bind_ip_, port_);
    fd_.close();
  }
}

brokkr::core::Status TcpListener::bind_and_listen(std::string bind_ip, std::uint16_t port, int backlog) noexcept {
  if (fd_.valid()) fd_.close();

  bind_ip_ = std::move(bind_ip);
  port_ = port;

  fd_.take(do_socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
  if (!fd_.valid()) {
    const int e = errno;
    return brokkr::core::Status::Failf("socket: {}", std::strerror(e));
  }

  int one = 1;
  (void)do_setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  if (::inet_pton(AF_INET, bind_ip_.c_str(), &addr.sin_addr) != 1) {
    fd_.close();
    return brokkr::core::Status::Failf("Invalid bind ip: {}", bind_ip_);
  }

  if (do_bind(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
    const int e = errno;
    fd_.close();
    return brokkr::core::Status::Failf("bind: {}", std::strerror(e));
  }
  if (do_listen(fd_, backlog) != 0) {
    const int e = errno;
    fd_.close();
    return brokkr::core::Status::Failf("listen: {}", std::strerror(e));
  }

  spdlog::debug("TcpListener: listening on {}:{}", bind_ip_, port_);
  return brokkr::core::Status::Ok();
}

brokkr::core::Result<TcpConnection> TcpListener::accept_one() noexcept {
  if (!fd_.valid()) return brokkr::core::Result<TcpConnection>::Fail("TcpListener: not listening");

  pollfd pfd{};
  pfd.fd = fd_.fd;
  pfd.events = POLLIN;

  for (;;) {
    int pr = ::poll(&pfd, 1, 100);
    if (pr == 0) return brokkr::core::Result<TcpConnection>::Fail("accept: timeout");
    if (pr < 0) {
      const int e = errno;
      if (e == EINTR) continue;
      if (e == EBADF) return brokkr::core::Result<TcpConnection>::Fail("accept: listener closed");
      return brokkr::core::Result<TcpConnection>::Failf("poll: {}", std::strerror(e));
    }

    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
      return brokkr::core::Result<TcpConnection>::Fail("accept: listener closed");
    }

    break;
  }

  sockaddr_in peer{};
  socklen_t peer_len = sizeof(peer);

  for (;;) {
    const int cfd = do_accept(fd_, reinterpret_cast<sockaddr*>(&peer), &peer_len, SOCK_CLOEXEC);
    if (cfd >= 0) {
      char ipbuf[INET_ADDRSTRLEN]{};
      const char* ip = ::inet_ntop(AF_INET, &peer.sin_addr, ipbuf, sizeof(ipbuf));
      if (!ip) {
        const int e = errno;
        ::close(cfd);
        return brokkr::core::Result<TcpConnection>::Failf("inet_ntop: {}", std::strerror(e));
      }

      const std::uint16_t p = ntohs(peer.sin_port);
      spdlog::debug("TcpListener: accepted {}:{}", ip, p);
      return brokkr::core::Result<TcpConnection>::Ok(TcpConnection(cfd, std::string(ip), p));
    }

    const int e = errno;
    if (e == EINTR) continue;
    if (e == EAGAIN || e == EWOULDBLOCK) return brokkr::core::Result<TcpConnection>::Fail("accept: timeout");
    if (e == EBADF || e == EINVAL) return brokkr::core::Result<TcpConnection>::Fail("accept: listener closed");
    return brokkr::core::Result<TcpConnection>::Failf("accept: {}", std::strerror(e));
  }
}

} // namespace brokkr::posix_common
