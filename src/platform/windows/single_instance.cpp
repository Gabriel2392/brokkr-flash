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

#include <cerrno>
#include <cstring>
#include <utility>

namespace brokkr::windows {

SingleInstanceLock::~SingleInstanceLock() {
  if (handle_ != INVALID_HANDLE_VALUE) {
    // Releasing the mutex is not strictly necessary before closing
    // if we are the owner, but it's good practice.
    ReleaseMutex(handle_);
    CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
  }
}

SingleInstanceLock::SingleInstanceLock(SingleInstanceLock &&o) noexcept {
  *this = std::move(o);
}

SingleInstanceLock &
SingleInstanceLock::operator=(SingleInstanceLock &&o) noexcept {
  if (this == &o)
    return *this;

  if (handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(handle_);
  }

  handle_ = o.handle_;
  name_ = std::move(o.name_);

  o.handle_ = INVALID_HANDLE_VALUE;
  return *this;
}

std::optional<SingleInstanceLock>
SingleInstanceLock::try_acquire(std::string name) {
  if (name.empty())
    return std::nullopt;

  // Use "Local\" prefix to scope this lock to the current user session.
  // Use "Global\" if the lock must be system-wide (requires admin).
  std::string kernel_name = "Local\\" + name;

  // CreateMutexA returns a handle even if the mutex already exists.
  HANDLE hMutex = CreateMutexA(nullptr, TRUE, kernel_name.c_str());

  if (hMutex == INVALID_HANDLE_VALUE || hMutex == nullptr) {
    return std::nullopt;
  }

  // If the mutex already existed, GetLastError will return
  // ERROR_ALREADY_EXISTS. In that case, another instance has the lock.
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(hMutex);
    return std::nullopt;
  }

  // We successfully created and acquired the mutex.
  return SingleInstanceLock{hMutex, std::move(name)};
}

} // namespace brokkr::windows
