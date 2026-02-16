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
#include <windows.h>

#include <spdlog/spdlog.h>

#include <utility>

namespace brokkr::core {

namespace {

BOOL WINAPI CtrlHandler(DWORD fdwCtrlType) {
  switch (fdwCtrlType) {
  case CTRL_C_EVENT:
  case CTRL_CLOSE_EVENT:
  case CTRL_BREAK_EVENT:
  case CTRL_LOGOFF_EVENT:
  case CTRL_SHUTDOWN_EVENT:
    return TRUE;
  default:
    return FALSE;
  }
}

} // namespace

SignalShield::SignalShield(Callback cb) : cb_(std::move(cb)) {}

SignalShield::~SignalShield() { stop_and_restore_(); }

SignalShield::SignalShield(SignalShield &&o) noexcept { *this = std::move(o); }

SignalShield &SignalShield::operator=(SignalShield &&o) noexcept {
  if (this == &o)
    return *this;

  stop_and_restore_();

  cb_ = std::move(o.cb_);
  watcher_ = std::move(o.watcher_);
  active_ = o.active_;

  o.active_ = false;
  return *this;
}

void SignalShield::stop_and_restore_() noexcept {
  if (active_) {
    watcher_.request_stop();

    TerminateProcess(GetCurrentProcess(), 0);

    if (watcher_.joinable())
      watcher_.join();
    active_ = false;
  }
}

std::optional<SignalShield> SignalShield::enable(Callback cb) {
  SignalShield sh(std::move(cb));
  sh.active_ = true;

  sh.watcher_ = std::jthread([cb2 = sh.cb_](std::stop_token st) mutable {
    if (!SetConsoleCtrlHandler(CtrlHandler, TRUE)) {
      return;
    }
    int count = 0;
    while (!st.stop_requested()) {
      Sleep(100);
      if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
        if (GetAsyncKeyState('C') & 0x8000) {
          spdlog::warn("Ctrl+C pressed - ignoring");
          cb2("SIGINT", ++count);
        }
        if (GetAsyncKeyState(VK_PAUSE) & 0x8000) {
          spdlog::warn("Ctrl+Break pressed - ignoring");
          cb2("SIGBREAK", ++count);
        }
      }
    }
    SetConsoleCtrlHandler(CtrlHandler, FALSE);
  });

  return std::optional<SignalShield>{std::move(sh)};
}

} // namespace brokkr::core
