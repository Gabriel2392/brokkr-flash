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

#include "platform/macos/usbfs_conn.hpp"

#include <algorithm>
#include <cstdint>

#include <IOKit/usb/IOUSBLib.h>
#include <unistd.h>

namespace brokkr::macos {

namespace {
constexpr std::size_t BULK_BUFFER_LENGTH_LIMIT    = 16 * 1024;
constexpr std::size_t BULK_BUFFER_LENGTH_NO_LIMIT = 128 * 1024;

inline bool ok_or_underrun(IOReturn kr) noexcept {
  // On macOS, short transfers can come back as kIOReturnUnderrun.
  // For our protocol framing, short transfer is a normal "end of message".
  return kr == kIOReturnSuccess || kr == kIOReturnUnderrun;
}
} // namespace

UsbFsConnection::UsbFsConnection(UsbFsDevice &dev) : dev_(dev) {}

bool UsbFsConnection::open() {
  if (connected_)
    return true;
  if (!dev_.is_open())
    return false;

  max_pack_size_ = dev_.has_packet_size_limit()
      ? BULK_BUFFER_LENGTH_LIMIT
      : BULK_BUFFER_LENGTH_NO_LIMIT;

  connected_ = true;
  zlp_needed_ = true;
  return true;
}

void UsbFsConnection::close() noexcept { connected_ = false; }

int UsbFsConnection::send(std::span<const std::uint8_t> data,
                          unsigned retries) {
  if (!connected_)
    return -1;

  auto ifc =
      static_cast<IOUSBInterfaceInterface300 **>(dev_.usb_interface());
  if (!ifc)
    return -1;

  const std::uint8_t *p = data.data();
  const std::uint8_t *end = p + data.size();
  const std::uint8_t *begin = p;

  while (p < end) {
    UInt32 want = static_cast<UInt32>(
        std::min<std::size_t>(static_cast<std::size_t>(end - p),
                              max_pack_size_));

    unsigned attempt = 0;
    for (;;) {
      IOReturn kr = (*ifc)->WritePipeTO(
          ifc, dev_.pipe_out_ref(),
          const_cast<void *>(static_cast<const void *>(p)), want,
          static_cast<UInt32>(timeout_ms_), static_cast<UInt32>(timeout_ms_));

      if (ok_or_underrun(kr)) {
        p += want;
        break;
      }

      if (++attempt > retries)
        return -1;
      ::usleep(10'000);
    }
  }

  if (zlp_needed_) {
    IOReturn kr = (*ifc)->WritePipeTO(ifc, dev_.pipe_out_ref(), nullptr, 0,
                                      100, 100);
    if (!ok_or_underrun(kr)) {
      zlp_needed_ = false;
    }
  }

  return static_cast<int>(p - begin);
}

int UsbFsConnection::recv_zlp(unsigned /*retries*/) {
  if (!connected_)
    return -1;

  auto ifc =
      static_cast<IOUSBInterfaceInterface300 **>(dev_.usb_interface());
  if (!ifc)
    return -1;

  UInt32 bytesRead = 0;
  (void)(*ifc)->ReadPipeTO(ifc, dev_.pipe_in_ref(), nullptr, &bytesRead, 10,
                           10);
  return 0;
}

int UsbFsConnection::recv(std::span<std::uint8_t> data, unsigned retries) {
  if (!connected_)
    return -1;

  if (data.empty())
    return recv_zlp();

  auto ifc =
      static_cast<IOUSBInterfaceInterface300 **>(dev_.usb_interface());
  if (!ifc)
    return -1;

  std::uint8_t *p = data.data();
  std::uint8_t *end = p + data.size();
  std::uint8_t *begin = p;

  while (p < end) {
    auto xfer = static_cast<UInt32>(
        std::min<std::size_t>(static_cast<std::size_t>(end - p),
                              max_pack_size_));

    UInt32 bytesRead = xfer;
    unsigned attempt = 0;
    for (;;) {
      bytesRead = xfer;
      IOReturn kr = (*ifc)->ReadPipeTO(
          ifc, dev_.pipe_in_ref(), p, &bytesRead,
          static_cast<UInt32>(timeout_ms_), static_cast<UInt32>(timeout_ms_));

      if (ok_or_underrun(kr))
        break;

      if (++attempt > retries)
        return -1;
      ::usleep(10'000);
    }

    p += bytesRead;

    if (bytesRead < xfer)
      break;
  }

  return static_cast<int>(p - begin);
}

} // namespace brokkr::macos
