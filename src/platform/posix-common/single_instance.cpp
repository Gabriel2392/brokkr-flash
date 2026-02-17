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
#include "platform/posix-common/filehandle.hpp"

#include <utility>

#include <sys/socket.h>
#include <sys/un.h>

namespace brokkr::posix_common {

SingleInstanceLock::~SingleInstanceLock() = default;

SingleInstanceLock::SingleInstanceLock(SingleInstanceLock &&o) noexcept {
  *this = std::move(o);
}

SingleInstanceLock &
SingleInstanceLock::operator=(SingleInstanceLock &&o) noexcept {
  if (this == &o)
    return *this;
  fd_ = std::move(o.fd_);
  name_ = std::move(o.name_);
  return *this;
}

std::optional<SingleInstanceLock>
SingleInstanceLock::try_acquire(std::string name) {
  FileHandle fd{do_socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0)};
  if (!fd.valid())
    return std::nullopt;

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;

  if (name.size() + 1 > sizeof(addr.sun_path)) {
    fd.close();
    return std::nullopt;
  }

  addr.sun_path[0] = '\0';
  std::memcpy(addr.sun_path + 1, name.data(), name.size());

  const socklen_t len =
      static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + name.size());

  if (do_bind(fd, reinterpret_cast<const sockaddr *>(&addr), len) != 0) {
    fd.close();
    return std::nullopt;
  }

  return SingleInstanceLock{std::move(fd), std::move(name)};
}
} // namespace brokkr::posix_common
