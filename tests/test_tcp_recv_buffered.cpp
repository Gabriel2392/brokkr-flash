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

#include "platform/platform_all.hpp"

#include <spdlog/spdlog.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <sys/socket.h>
  #include <unistd.h>
#endif

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <span>
#include <thread>

using brokkr::platform::TcpConnection;

#if defined(_WIN32)
using PeerSocket = SOCKET;

static void close_peer(PeerSocket s) { ::closesocket(s); }

static void write_peer(PeerSocket s, const char* p, int n) { (void)::send(s, p, n, 0); }

static int read_peer(PeerSocket s, char* p, int n) { return ::recv(s, p, n, 0); }

static std::optional<TcpConnection> make_pair(PeerSocket& peer) {
  const SOCKET ls = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (ls == INVALID_SOCKET) return std::nullopt;

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int len = sizeof(addr);
  if (::bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(ls, 1) != 0 ||
      ::getsockname(ls, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
    ::closesocket(ls);
    return std::nullopt;
  }

  peer = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (peer == INVALID_SOCKET || ::connect(peer, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    if (peer != INVALID_SOCKET) ::closesocket(peer);
    ::closesocket(ls);
    return std::nullopt;
  }

  const SOCKET s = ::accept(ls, nullptr, nullptr);
  ::closesocket(ls);
  if (s == INVALID_SOCKET) {
    ::closesocket(peer);
    return std::nullopt;
  }
  return TcpConnection(s, "test", 0);
}
#else
using PeerSocket = int;

static void close_peer(PeerSocket s) { ::close(s); }

static void write_peer(PeerSocket s, const char* p, int n) { (void)::write(s, p, static_cast<std::size_t>(n)); }

static int read_peer(PeerSocket s, char* p, int n) {
  return static_cast<int>(::read(s, p, static_cast<std::size_t>(n)));
}

static std::optional<TcpConnection> make_pair(PeerSocket& peer) {
  int fds[2];
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return std::nullopt;
  peer = fds[1];
  return TcpConnection(fds[0], "test", 0);
}
#endif

static int g_pass = 0;
static int g_fail = 0;

static void check(const char* label, bool ok) {
  if (ok) {
    ++g_pass;
  } else {
    std::fprintf(stderr, "FAIL %s\n", label);
    ++g_fail;
  }
}

static void settle() { std::this_thread::sleep_for(std::chrono::milliseconds(50)); }

static void test_data_then_close_is_read() {
  PeerSocket peer{};
  auto conn = make_pair(peer);
  if (!conn) {
    check("socket_pair", false);
    return;
  }

  write_peer(peer, "hello", 5);
  close_peer(peer);
  settle();

  std::array<std::uint8_t, 16> buf{};
  const int n = conn->recv(std::span<std::uint8_t>(buf.data(), buf.size()));
  check("buffered_data_returned", n == 5);
  if (n == 5) check("buffered_data_contents", std::memcmp(buf.data(), "hello", 5) == 0);
  check("then_reports_closed", conn->recv(std::span<std::uint8_t>(buf.data(), buf.size())) < 0);
}

static void test_close_without_data_reports_closed() {
  PeerSocket peer{};
  auto conn = make_pair(peer);
  if (!conn) {
    check("socket_pair", false);
    return;
  }

  close_peer(peer);
  settle();

  std::array<std::uint8_t, 16> buf{};
  check("close_without_data_reports_closed", conn->recv(std::span<std::uint8_t>(buf.data(), buf.size())) < 0);
}

static void test_open_connection_keeps_working() {
  PeerSocket peer{};
  auto conn = make_pair(peer);
  if (!conn) {
    check("socket_pair", false);
    return;
  }

  write_peer(peer, "abc", 3);
  std::array<std::uint8_t, 16> buf{};
  check("open_recv", conn->recv(std::span<std::uint8_t>(buf.data(), buf.size())) == 3);
  check("open_still_connected", conn->connected());

  const std::uint8_t out[4] = {1, 2, 3, 4};
  check("open_send", conn->send(std::span<const std::uint8_t>(out, sizeof(out))) == 4);
  char in[8];
  check("open_peer_received", read_peer(peer, in, sizeof(in)) == 4);
  close_peer(peer);
}

int main() {
#if defined(_WIN32)
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
#endif
  spdlog::set_level(spdlog::level::off);

  test_data_then_close_is_read();
  test_close_without_data_reports_closed();
  test_open_connection_keeps_working();

  std::fprintf(stdout, "tcp_recv_buffered: %d passed, %d failed\n", g_pass, g_fail);
#if defined(_WIN32)
  WSACleanup();
#endif
  return g_fail ? 1 : 0;
}
