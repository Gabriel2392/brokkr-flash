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

#include "core/status.hpp"

#include <cstdint>
#include <string>

namespace brokkr::macos {

struct UsbIds {
  std::uint16_t vendor = 0;
  std::uint16_t product = 0;
};

struct UsbEndpoints {
  std::uint8_t bulk_in = 0;
  std::uint8_t bulk_out = 0;

  std::uint16_t bulk_in_max_packet = 0;
  std::uint16_t bulk_out_max_packet = 0;
};

class UsbFsDevice {
 public:
  explicit UsbFsDevice(std::string devnode);
  ~UsbFsDevice();

  UsbFsDevice(const UsbFsDevice&) = delete;
  UsbFsDevice& operator=(const UsbFsDevice&) = delete;

  UsbFsDevice(UsbFsDevice&&) noexcept;
  UsbFsDevice& operator=(UsbFsDevice&&) noexcept;

  brokkr::core::Status open_and_init() noexcept;

  void close() noexcept;
  bool is_open() const noexcept { return dev_intf_ != nullptr; }

  UsbIds ids() const noexcept { return ids_; }
  UsbEndpoints endpoints() const noexcept { return eps_; }
  int interface_number() const noexcept { return ifc_num_; }

  bool has_packet_size_limit() const noexcept { return false; }

  const std::string& devnode() const noexcept { return devnode_; }

  void reset_device() noexcept;

  void* usb_interface() const noexcept { return ifc_intf_; }
  std::uint8_t pipe_in_ref() const noexcept { return pipe_in_; }
  std::uint8_t pipe_out_ref() const noexcept { return pipe_out_; }

 private:
  std::string devnode_;

  void* dev_intf_ = nullptr; // IOUSBDeviceInterface320**
  void* ifc_intf_ = nullptr; // IOUSBInterfaceInterface300**

  UsbIds ids_{};
  UsbEndpoints eps_{};
  int ifc_num_ = -1;

  std::uint8_t pipe_in_ = 0;
  std::uint8_t pipe_out_ = 0;
};

} // namespace brokkr::macos
