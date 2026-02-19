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

#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <utility>

namespace brokkr::core {

namespace {

sigset_t make_set() {
  sigset_t set{};
  sigemptyset(&set);

  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGTERM);
  sigaddset(&set, SIGHUP);
  sigaddset(&set, SIGQUIT);
  sigaddset(&set, SIGTSTP);

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

    ::kill(::getpid(), SIGTERM);

    if (watcher_.joinable()) watcher_.join();
    active_ = false;
  }

  if (have_old_mask_) {
    (void)::pthread_sigmask(SIG_SETMASK, &old_mask_, nullptr);
    have_old_mask_ = false;
  }
}

std::optional<SignalShield> SignalShield::enable(Callback cb) {
  ::signal(SIGPIPE, SIG_IGN);

  const sigset_t set = make_set();

  sigset_t old{};
  if (::pthread_sigmask(SIG_BLOCK, &set, &old) != 0) {
    return std::nullopt;
  }

  SignalShield sh(std::move(cb));
  sh.active_ = true;
  sh.old_mask_ = old;
  sh.have_old_mask_ = true;

  sh.watcher_ = std::jthread([cb2 = sh.cb_](std::stop_token st) mutable {
    sigset_t waitset = make_set();
    int count = 0;

    for (;;) {
      int signo = 0;
      const int r = ::sigwait(&waitset, &signo);
      if (r != 0) continue;

      if (st.stop_requested()) break;

      ++count;
      if (cb2) cb2(sig_desc(signo), count);
    }
  });

  return std::optional<SignalShield>{std::move(sh)};
}

} // namespace brokkr::core
