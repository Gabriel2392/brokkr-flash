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

#pragma once

#include "core/byte_transport.hpp"
#include "usbfs_device.hpp"

#include <cstdint>
#include <span>

namespace brokkr::windows {

class UsbFsConnection : public brokkr::core::IByteTransport {
public:
  explicit UsbFsConnection(UsbFsDevice &dev);

  Kind kind() const noexcept override { return Kind::UsbBulk; }

  bool open();
  void close() noexcept;
  bool connected() const noexcept override { return connected_; }

  int send(std::span<const std::uint8_t> data, unsigned retries = 8) override;
  int recv(std::span<std::uint8_t> data, unsigned retries = 8) override;
  int recv_zlp(unsigned retries = 0) override;

  void set_timeout_ms(int ms) noexcept override { timeout_ms_ = ms; }
  int timeout_ms() const noexcept override { return timeout_ms_; }

private:
  UsbFsDevice &dev_;
  bool connected_ = false;
  int timeout_ms_ = 1000;

  std::size_t max_pack_size_ = 128 * 1024;
};

} // namespace brokkr::windows
