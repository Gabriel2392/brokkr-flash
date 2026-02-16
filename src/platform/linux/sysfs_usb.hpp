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

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace brokkr::linux {

struct UsbDeviceSysfsInfo {
  std::string sysname;
  int busnum = -1;
  int devnum = -1;
  std::uint16_t vendor = 0;
  std::uint16_t product = 0;
  int connected_duration_sec = 0;

  std::string devnode() const;
};

struct EnumerateFilter {
  std::uint16_t vendor = 0x04E8;
  std::vector<std::uint16_t> products;
};

std::vector<UsbDeviceSysfsInfo>
enumerate_usb_devices_sysfs(const EnumerateFilter &filter);
std::optional<UsbDeviceSysfsInfo> find_by_sysname(std::string_view sysname);

} // namespace brokkr::linux
