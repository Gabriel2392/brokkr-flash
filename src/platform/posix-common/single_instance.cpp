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

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace brokkr::posix_common {

SingleInstanceLock::~SingleInstanceLock() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

SingleInstanceLock::SingleInstanceLock(SingleInstanceLock &&o) noexcept {
  *this = std::move(o);
}

SingleInstanceLock &
SingleInstanceLock::operator=(SingleInstanceLock &&o) noexcept {
  if (this == &o)
    return *this;
  if (fd_ >= 0)
    ::close(fd_);
  fd_ = o.fd_;
  o.fd_ = -1;
  name_ = std::move(o.name_);
  return *this;
}

std::optional<SingleInstanceLock>
SingleInstanceLock::try_acquire(std::string name) {
  std::string path = "/tmp/" + name + ".lock";

  const int fd = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  if (fd < 0)
    return std::nullopt;

  if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
    ::close(fd);
    return std::nullopt;
  }

  return SingleInstanceLock{fd, std::move(name)};
}

} // namespace brokkr::posix_common
