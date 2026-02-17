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

#include "platform/linux/usbfs_conn.hpp"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cstdint>

#include <linux/usbdevice_fs.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace brokkr::linux {

namespace {
constexpr std::size_t BULK_BUFFER_LENGTH_LIMIT = 16 * 1024;
constexpr std::size_t BULK_BUFFER_LENGTH_NO_LIMIT = 128 * 1024;
} // namespace

UsbFsConnection::UsbFsConnection(UsbFsDevice &dev) : dev_(dev) {}

bool UsbFsConnection::open() {
  if (connected_) {
    spdlog::warn("UsbFsConnection::open: already connected");
    return true;
  }
  if (!dev_.is_open()) {
    spdlog::error("UsbFsConnection::open: device not open");
    return false;
  }

  max_pack_size_ = dev_.has_packet_size_limit() ? BULK_BUFFER_LENGTH_LIMIT
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

  const auto eps = dev_.endpoints();
  if (eps.bulk_out == 0) {
    spdlog::error("UsbFsConnection::send: no bulk OUT endpoint");
    return -1;
  }

  const std::uint8_t *p = data.data();
  const std::uint8_t *end = p + data.size();
  const std::uint8_t *begin = p;

  usbdevfs_bulktransfer bulk{};
  bulk.ep = eps.bulk_out;
  bulk.timeout = timeout_ms_;

  while (p < end) {
    const int want = static_cast<int>(
        std::min<std::size_t>(std::size_t(end - p), max_pack_size_));
    bulk.len = want;
    bulk.data = const_cast<std::uint8_t *>(p);

    unsigned attempt = 0;
    for (;;) {
      const int rc = ::ioctl(dev_.fd(), USBDEVFS_BULK, &bulk);
      if (rc >= 0) {
        if (rc == 0 && want != 0)
          return -1;
        p += rc;
        break;
      }

      spdlog::error("UsbFsConnection::send: bulk transfer error: {}",
                    std::strerror(errno));
      if (++attempt > retries) {
        spdlog::error("UsbFsConnection::send: giving up after {} attempts",
                      attempt);
        return -1;
      }
      ::usleep(10'000);
    }
  }

  if (zlp_needed_) {
    usbdevfs_bulktransfer zlp{};
    zlp.ep = eps.bulk_out;
    zlp.timeout = 100;
    zlp.len = 0;
    zlp.data = nullptr;

    const int rc = ::ioctl(dev_.fd(), USBDEVFS_BULK, &zlp);
    if (rc != 0) {
      zlp_needed_ = false;
    }
  }

  return static_cast<int>(p - begin);
}

int UsbFsConnection::recv_zlp(unsigned /*retries*/) {
  if (!connected_) {
    spdlog::error("UsbFsConnection::recv_zlp: not connected");
    return -1;
  }

  const auto eps = dev_.endpoints();
  if (eps.bulk_in == 0) {
    spdlog::error("UsbFsConnection::recv_zlp: no bulk IN endpoint");
    return -1;
  }

  usbdevfs_bulktransfer zlp{};
  zlp.ep = eps.bulk_in;
  zlp.timeout = 10;
  zlp.len = 0;
  zlp.data = nullptr;
  (void)::ioctl(dev_.fd(), USBDEVFS_BULK, &zlp);
  return 0;
}

int UsbFsConnection::recv(std::span<std::uint8_t> data, unsigned retries) {
  if (!connected_) {
    spdlog::error("UsbFsConnection::recv: not connected");
    return -1;
  }

  const auto eps = dev_.endpoints();
  if (eps.bulk_in == 0) {
    spdlog::error("UsbFsConnection::recv: no bulk IN endpoint");
    return -1;
  }

  if (data.size() == 0) {
    return recv_zlp();
  }

  std::uint8_t *p = data.data();
  std::uint8_t *end = p + data.size();
  std::uint8_t *begin = p;

  usbdevfs_bulktransfer bulk{};

  while (p < end) {
    const auto xfer = static_cast<int>(
        std::min<std::size_t>(std::size_t(end - p), max_pack_size_));
    bulk.ep = eps.bulk_in;
    bulk.len = xfer;
    bulk.data = p;
    bulk.timeout = timeout_ms_;

    int retBytes = 0;
    unsigned attempt = 0;
    for (;;) {
      retBytes = ::ioctl(dev_.fd(), USBDEVFS_BULK, &bulk);
      if (retBytes >= 0)
        break;
      spdlog::error("UsbFsConnection::recv: bulk transfer error: {}",
                    std::strerror(errno));
      if (++attempt > retries) {
        spdlog::error("UsbFsConnection::recv: giving up after {} attempts",
                      attempt);
        return -1;
      }
      ::usleep(10'000);
    }

    p += retBytes;
    if (retBytes < xfer)
      break;
  }

  return static_cast<int>(p - begin);
}

} // namespace brokkr::linux
