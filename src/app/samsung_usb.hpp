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

#include "platform/platform_all.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string_view>
#include <vector>

namespace brokkr::app {

inline constexpr std::uint16_t kSamsungVid = 0x04E8;
inline constexpr std::uint16_t kOdinPids[] = {0x6601, 0x685D, 0x68C3};

inline bool is_odin_product(std::uint16_t pid) noexcept {
  return std::ranges::find(kOdinPids, pid) != std::end(kOdinPids);
}

inline std::vector<brokkr::platform::UsbDeviceSysfsInfo> enumerate_samsung_targets() {
  brokkr::platform::EnumerateFilter f{.vendor = kSamsungVid};
  return brokkr::platform::enumerate_usb_devices_sysfs(f);
}

inline std::vector<brokkr::platform::UsbDeviceSysfsInfo> enumerate_odin_targets() {
  brokkr::platform::EnumerateFilter f{.vendor = kSamsungVid, .products = {std::begin(kOdinPids), std::end(kOdinPids)}};
  return brokkr::platform::enumerate_usb_devices_sysfs(f);
}

inline std::optional<brokkr::platform::UsbDeviceSysfsInfo> select_samsung_target(std::string_view sysname) {
  if (sysname.empty()) return std::nullopt;
  auto info = brokkr::platform::find_by_sysname(sysname);
  if (!info || info->vendor != kSamsungVid) return std::nullopt;
  return info;
}

inline std::optional<brokkr::platform::UsbDeviceSysfsInfo> select_odin_target(std::string_view sysname) {
  auto info = select_samsung_target(sysname);
  if (!info || !is_odin_product(info->product)) return std::nullopt;
  return info;
}

} // namespace brokkr::app
