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
#include "platform/posix-common/filehandle.hpp"

#include <cstdint>
#include <string>

namespace brokkr::linux {

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
  bool is_open() const noexcept { return fd_.valid(); }

  int fd() const noexcept { return fd_.fd; }
  bool writable() const noexcept { return writable_; }

  UsbIds ids() const noexcept { return ids_; }
  UsbEndpoints endpoints() const noexcept { return eps_; }
  int interface_number() const noexcept { return ifc_num_; }

  std::uint32_t caps() const noexcept { return caps_; }
  bool has_packet_size_limit() const noexcept;

  const std::string& devnode() const noexcept { return devnode_; }

  void reset_device() noexcept;

 private:
  brokkr::core::Status parse_descriptors_() noexcept;
  void query_caps_() noexcept;

  bool kernel_driver_active_() const noexcept;
  brokkr::core::Status detach_kernel_driver_() noexcept;
  void attach_kernel_driver_() noexcept;
  brokkr::core::Status claim_interface_() noexcept;
  void release_interface_() noexcept;

 private:
  std::string devnode_;
  brokkr::FileHandle fd_;

  bool writable_ = true;

  bool claimed_ = false;
  bool driver_detached_ = false;

  UsbIds ids_{};
  UsbEndpoints eps_{};
  int ifc_num_ = -1;

  std::uint32_t caps_ = 0;
};

} // namespace brokkr::linux
