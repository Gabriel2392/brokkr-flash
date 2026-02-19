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

#if defined(BROKKR_PLATFORM_LINUX)
  #include "platform/posix-common/signal_shield.hpp"
  #include "platform/posix-common/single_instance.hpp"
  #include "platform/posix-common/tcp_transport.hpp"

  #include "platform/linux/sysfs_usb.hpp"
  #include "platform/linux/usbfs_conn.hpp"
  #include "platform/linux/usbfs_device.hpp"

namespace brokkr::platform {
using namespace linux;
using namespace posix_common;
} // namespace brokkr::platform

#elif defined(BROKKR_PLATFORM_WINDOWS)
  #include "platform/windows/signal_shield.hpp"
  #include "platform/windows/single_instance.hpp"
  #include "platform/windows/sysfs_usb.hpp"
  #include "platform/windows/tcp_transport.hpp"
  #include "platform/windows/usbfs_conn.hpp"
  #include "platform/windows/usbfs_device.hpp"

namespace brokkr::platform {
using namespace windows;
} // namespace brokkr::platform

#elif defined(BROKKR_PLATFORM_MACOS)
  #include "platform/posix-common/signal_shield.hpp"
  #include "platform/posix-common/single_instance.hpp"
  #include "platform/posix-common/tcp_transport.hpp"

  #include "platform/macos/sysfs_usb.hpp"
  #include "platform/macos/usbfs_conn.hpp"
  #include "platform/macos/usbfs_device.hpp"

namespace brokkr::platform {
using namespace macos;
using namespace posix_common;
} // namespace brokkr::platform

#else
  #error "Unsupported platform"
#endif
