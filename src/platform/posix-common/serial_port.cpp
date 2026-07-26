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

#include "platform/serial_port.hpp"

#include "platform/posix-common/filehandle.hpp"

#include <cerrno>
#include <cstddef>
#include <cstring>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace brokkr::platform {

namespace {

brokkr::core::Error open_error(const std::string& node) {
  const int err = errno;
#if defined(BROKKR_PLATFORM_LINUX)
  if (err == EACCES)
    return fmt::format("open failed: {}: {} (add your user to the 'dialout' group)", node, std::strerror(err));
#endif
  return fmt::format("open failed: {}: {}", node, std::strerror(err));
}

brokkr::core::Status configure(int fd) noexcept {
  termios tio{};
  if (::tcgetattr(fd, &tio) != 0) return brokkr::core::failf("tcgetattr failed: {}", std::strerror(errno));

  ::cfmakeraw(&tio);
  if (::cfsetispeed(&tio, B115200) != 0 || ::cfsetospeed(&tio, B115200) != 0)
    return brokkr::core::failf("cfsetspeed failed: {}", std::strerror(errno));

  tio.c_cflag |= static_cast<tcflag_t>(CLOCAL | CREAD);
  tio.c_cflag &= ~static_cast<tcflag_t>(CSTOPB | CRTSCTS);
  tio.c_iflag &= ~static_cast<tcflag_t>(IXON | IXOFF | IXANY);
  tio.c_cc[VMIN] = 0;
  tio.c_cc[VTIME] = 0;

  if (::tcsetattr(fd, TCSANOW, &tio) != 0) return brokkr::core::failf("tcsetattr failed: {}", std::strerror(errno));

  int rts = TIOCM_RTS;
  int dtr = TIOCM_DTR;
  (void)::ioctl(fd, TIOCMBIC, &rts);
  (void)::ioctl(fd, TIOCMBIS, &dtr);

  return {};
}

} // namespace

brokkr::core::Status write_serial_port(const std::string& node, std::string_view data) noexcept {
  if (node.empty()) return brokkr::core::fail("invalid port name");

  const brokkr::FileHandle fd(::open(node.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC));
  if (!fd.valid()) return brokkr::core::fail(open_error(node));

  BRK_TRY(configure(fd.fd));

  if (::fcntl(fd.fd, F_SETFL, 0) < 0)
    return brokkr::core::failf("fcntl(F_SETFL) failed: {}", std::strerror(errno));

  for (std::size_t off = 0; off < data.size();) {
    const ssize_t n = ::write(fd.fd, data.data() + off, data.size() - off);
    if (n < 0) {
      if (errno == EINTR) continue;
      return brokkr::core::failf("write failed: {}", std::strerror(errno));
    }
    if (n == 0) return brokkr::core::fail("write failed: no progress");
    off += static_cast<std::size_t>(n);
  }

  if (::tcdrain(fd.fd) != 0) return brokkr::core::failf("tcdrain failed: {}", std::strerror(errno));
  return {};
}

} // namespace brokkr::platform
