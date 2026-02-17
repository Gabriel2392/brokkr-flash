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

#include "single_instance.hpp"

#include <utility>

namespace brokkr::windows {

SingleInstanceLock::~SingleInstanceLock() {
  if (handle_) {
    // CloseHandle is sufficient for our single-instance lock semantics.
    // (ReleaseMutex is thread-affine and can fail if destroyed on a different thread.)
    CloseHandle(handle_);
    handle_ = nullptr;
  }
}

SingleInstanceLock::SingleInstanceLock(SingleInstanceLock&& o) noexcept { *this = std::move(o); }

SingleInstanceLock& SingleInstanceLock::operator=(SingleInstanceLock&& o) noexcept {
  if (this == &o) return *this;

  if (handle_) CloseHandle(handle_);

  handle_ = o.handle_;
  name_ = std::move(o.name_);
  o.handle_ = nullptr;
  return *this;
}

std::optional<SingleInstanceLock> SingleInstanceLock::try_acquire(std::string name) {
  if (name.empty()) return std::nullopt;

  const std::string kernel_name = "Local\\" + name;

  HANDLE h = CreateMutexA(nullptr, TRUE, kernel_name.c_str());
  if (!h) return std::nullopt;

  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(h);
    return std::nullopt;
  }

  return SingleInstanceLock{h, std::move(name)};
}

} // namespace brokkr::windows
