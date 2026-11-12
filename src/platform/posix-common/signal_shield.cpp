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

#include "signal_shield.hpp"

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <utility>

namespace brokkr::core {

namespace {

constexpr std::array kSignals{SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGTSTP};

std::atomic_flag g_active;
std::array<struct sigaction, kSignals.size()> g_old_actions{};

int g_pipe_rd = -1;
std::atomic<int> g_pipe_wr{-1};

sigset_t make_set() {
  sigset_t set{};
  sigemptyset(&set);

  for (const int signo : kSignals) sigaddset(&set, signo);

  return set;
}

const char* sig_desc(int signo) {
  switch (signo) {
    case SIGINT: return "SIGINT";
    case SIGTERM: return "SIGTERM";
    case SIGHUP: return "SIGHUP";
    case SIGQUIT: return "SIGQUIT";
    case SIGTSTP: return "SIGTSTP";
    default: return "SIGNAL";
  }
}

bool open_pipe() {
  if (g_pipe_rd >= 0) return true;

  int fds[2];
  if (::pipe(fds) != 0) return false;
  for (const int fd : fds) (void)::fcntl(fd, F_SETFD, FD_CLOEXEC);
  (void)::fcntl(fds[1], F_SETFL, O_NONBLOCK);

  g_pipe_rd = fds[0];
  g_pipe_wr.store(fds[1]);
  return true;
}

void drain_pipe() {
  pollfd pfd{g_pipe_rd, POLLIN, 0};
  unsigned char b = 0;
  while (::poll(&pfd, 1, 0) > 0 && ::read(g_pipe_rd, &b, 1) == 1) {}
}

void notify(unsigned char b) noexcept {
  const int saved_errno = errno;
  while (::write(g_pipe_wr.load(), &b, 1) < 0 && errno == EINTR) {}
  errno = saved_errno;
}

void on_signal(int signo) { notify(static_cast<unsigned char>(signo)); }

} // namespace

SignalShield::SignalShield(Callback cb) : cb_(std::move(cb)) {}

SignalShield::~SignalShield() { stop_and_restore_(); }

SignalShield::SignalShield(SignalShield&& o) noexcept { *this = std::move(o); }

SignalShield& SignalShield::operator=(SignalShield&& o) noexcept {
  if (this == &o) return *this;

  stop_and_restore_();

  cb_ = std::move(o.cb_);
  watcher_ = std::move(o.watcher_);
  active_ = o.active_;
  old_mask_ = o.old_mask_;
  have_old_mask_ = o.have_old_mask_;

  o.active_ = false;
  o.have_old_mask_ = false;
  return *this;
}

void SignalShield::stop_and_restore_() noexcept {
  if (active_) {
    watcher_.request_stop();

    notify(0);

    if (watcher_.joinable()) watcher_.join();

    for (std::size_t i = 0; i < kSignals.size(); ++i) (void)::sigaction(kSignals[i], &g_old_actions[i], nullptr);
    g_active.clear();
    active_ = false;
  }

  if (have_old_mask_) {
    (void)::pthread_sigmask(SIG_SETMASK, &old_mask_, nullptr);
    have_old_mask_ = false;
  }
}

std::optional<SignalShield> SignalShield::enable(Callback cb) {
  ::signal(SIGPIPE, SIG_IGN);

  if (g_active.test_and_set()) return std::nullopt;

  const sigset_t set = make_set();

  sigset_t old{};
  if (!open_pipe() || ::pthread_sigmask(SIG_BLOCK, &set, &old) != 0) {
    g_active.clear();
    return std::nullopt;
  }
  drain_pipe();

  struct sigaction sa {};
  sa.sa_handler = on_signal;
  sa.sa_flags = SA_RESTART;
  sigfillset(&sa.sa_mask);
  for (std::size_t i = 0; i < kSignals.size(); ++i) (void)::sigaction(kSignals[i], &sa, &g_old_actions[i]);

  SignalShield sh(std::move(cb));
  sh.active_ = true;
  sh.old_mask_ = old;
  sh.have_old_mask_ = true;

  sh.watcher_ = std::jthread([cb2 = sh.cb_](std::stop_token st) mutable {
    const sigset_t waitset = make_set();
    (void)::pthread_sigmask(SIG_UNBLOCK, &waitset, nullptr);

    int count = 0;

    for (;;) {
      unsigned char signo = 0;
      const ssize_t r = ::read(g_pipe_rd, &signo, 1);
      if (r < 0 && errno == EINTR) continue;

      if (r != 1 || st.stop_requested()) break;

      ++count;
      if (cb2) cb2(sig_desc(signo), count);
    }
  });

  return std::optional<SignalShield>{std::move(sh)};
}

} // namespace brokkr::core
