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

// A peer that sends data and then closes must still have that data read.
// These tests use a connected socket pair, so they run on POSIX systems only.

#include "platform/platform_all.hpp"

#include <spdlog/spdlog.h>

#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

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

static void test_data_then_close_is_read() {
  int fds[2];
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
    check("socketpair", false);
    return;
  }

  brokkr::platform::TcpConnection conn(fds[0], "test", 0);
  const char msg[] = "hello";
  (void)::write(fds[1], msg, 5);
  ::close(fds[1]);  // the data is already queued, then the peer is gone

  std::array<std::uint8_t, 16> buf{};
  const int n = conn.recv(std::span<std::uint8_t>(buf.data(), buf.size()));
  check("buffered_data_returned", n == 5);
  if (n == 5) check("buffered_data_contents", std::memcmp(buf.data(), msg, 5) == 0);
}

static void test_close_without_data_reports_closed() {
  int fds[2];
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
    check("socketpair", false);
    return;
  }

  brokkr::platform::TcpConnection conn(fds[0], "test", 0);
  ::close(fds[1]);

  std::array<std::uint8_t, 16> buf{};
  const int n = conn.recv(std::span<std::uint8_t>(buf.data(), buf.size()));
  check("close_without_data_reports_closed", n < 0);
}

int main() {
  // The socket pair is AF_UNIX, so TcpConnection's TCP socket options log
  // expected errors. Silence the logger for these tests.
  spdlog::set_level(spdlog::level::off);

  test_data_then_close_is_read();
  test_close_without_data_reports_closed();

  std::fprintf(stdout, "tcp_recv_buffered: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
