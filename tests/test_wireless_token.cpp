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

// The wireless listener's optional shared-secret check. These tests use a
// connected socket pair, so they run on POSIX systems only.

#include "app/wireless_token.hpp"

#include <spdlog/spdlog.h>

#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <string>

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

// Runs the check against bytes the peer sends. The peer stays open during the
// check, so a short token waits for the timeout instead of reporting a close.
static bool run_check(const std::string& sent, const std::string& expected) {
  int fds[2];
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return false;

  brokkr::platform::TcpConnection conn(fds[0], "test", 0);
  if (!sent.empty()) (void)::write(fds[1], sent.data(), sent.size());
  const bool ok = brokkr::app::wireless_token_matches(conn, expected);
  ::close(fds[1]);
  return ok;
}

static void test_matching_token() {
  check("matching_token_accepted", run_check("secret-token", "secret-token"));
}

static void test_wrong_token() {
  check("wrong_token_rejected", !run_check("wrong-token!", "secret-token"));
}

static void test_short_token_then_close() {
  check("short_token_rejected", !run_check("sec", "secret-token"));
}

static void test_no_token_configured() {
  // With no token set, the check returns true without reading from the connection.
  brokkr::platform::TcpConnection conn;
  check("empty_expected_skips_check", brokkr::app::wireless_token_matches(conn, ""));
}

int main() {
  // The socket pair is AF_UNIX, so TcpConnection's TCP socket options log
  // expected errors. Silence the logger for these tests.
  spdlog::set_level(spdlog::level::off);

  test_matching_token();
  test_wrong_token();
  test_short_token_then_close();
  test_no_token_configured();

  std::fprintf(stdout, "wireless_token: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
