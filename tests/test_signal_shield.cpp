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

#include "platform/posix-common/signal_shield.hpp"

#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

using brokkr::core::SignalShield;

static int g_pass = 0;
static int g_fail = 0;

static std::atomic<int> g_seen{0};

static void check(const char* label, bool ok) {
  if (ok) {
    ++g_pass;
  } else {
    std::fprintf(stderr, "FAIL %s\n", label);
    ++g_fail;
  }
}

static bool wait_seen(int n) {
  for (int i = 0; i < 500 && g_seen.load() < n; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  return g_seen.load() >= n;
}

static bool sigint_is_default() {
  struct sigaction sa {};
  return ::sigaction(SIGINT, nullptr, &sa) == 0 && sa.sa_handler == SIG_DFL;
}

static bool sigint_is_blocked() {
  sigset_t mask{};
  return ::pthread_sigmask(SIG_SETMASK, nullptr, &mask) == 0 && sigismember(&mask, SIGINT) == 1;
}

static void on_seen(const char*, int count) { g_seen.store(count); }

int main() {
  ::signal(SIGINT, SIG_DFL);

  std::jthread stray([](std::stop_token st) {
    while (!st.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  });

  {
    auto shield = SignalShield::enable(on_seen);
    check("enable", shield.has_value());
    check("second_enable_refused", !SignalShield::enable(on_seen).has_value());
    check("caller_blocked", sigint_is_blocked());

    ::pthread_kill(stray.native_handle(), SIGINT);
    check("stray_thread_sigint_reported", wait_seen(1));

    ::kill(::getpid(), SIGTERM);
    check("process_sigterm_reported", wait_seen(2));
  }

  check("disposition_restored", sigint_is_default());
  check("mask_restored", !sigint_is_blocked());

  {
    g_seen.store(0);
    auto shield = SignalShield::enable(on_seen);
    check("re_enable", shield.has_value());

    ::kill(::getpid(), SIGINT);
    check("re_enable_sigint_reported", wait_seen(1));
  }

  std::fprintf(stdout, "signal_shield: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
