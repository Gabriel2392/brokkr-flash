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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace brokkr::app {

enum class DeviceTransport : std::uint8_t {
  Usb,
  Wireless,
};

enum class SlotResult : std::uint8_t {
  None,
  PassNormal,
  PassEnhanced,
  Fail,
};

struct DeviceSession {
  std::string sysname;
  // Changes on each attach.
  std::uint64_t connection_id = 0;
  DeviceTransport transport = DeviceTransport::Usb;

  friend bool operator==(const DeviceSession&, const DeviceSession&) = default;
};

struct SlotUpdate {
  std::vector<std::optional<DeviceSession>> devices;
  std::vector<std::size_t> added;
  std::size_t overflow = 0;
};

inline SlotUpdate update_slots(std::span<const std::optional<DeviceSession>> previous,
                               std::span<const DeviceSession> current, std::size_t capacity) {
  SlotUpdate out;
  out.devices.resize(capacity);

  // Ignore duplicate ports.
  std::vector<DeviceSession> devices;
  devices.reserve(current.size());
  for (const auto& device : current) {
    if (device.sysname.empty()) continue;
    const auto duplicate = std::ranges::find_if(devices, [&](const DeviceSession& existing) {
      return existing.transport == device.transport && existing.sysname == device.sysname;
    });
    if (duplicate == devices.end()) devices.push_back(device);
  }

  std::vector<bool> used(devices.size(), false);

  // Keep live devices in place.
  const std::size_t kept = std::min(previous.size(), capacity);
  for (std::size_t slot = 0; slot < kept; ++slot) {
    if (!previous[slot]) continue;

    for (std::size_t device = 0; device < devices.size(); ++device) {
      if (used[device] || devices[device] != *previous[slot]) continue;
      out.devices[slot] = devices[device];
      used[device] = true;
      break;
    }
  }

  // Reuse the slot on reconnect.
  for (std::size_t device = 0; device < devices.size(); ++device) {
    if (used[device]) continue;

    for (std::size_t slot = 0; slot < kept; ++slot) {
      if (out.devices[slot] || !previous[slot]) continue;
      if (previous[slot]->transport != devices[device].transport || previous[slot]->sysname != devices[device].sysname)
        continue;

      out.devices[slot] = devices[device];
      out.added.push_back(slot);
      used[device] = true;
      break;
    }
  }

  // New devices take empty slots.
  for (std::size_t device = 0; device < devices.size(); ++device) {
    if (used[device]) continue;

    const auto free = std::ranges::find_if(out.devices, [](const auto& item) { return !item; });
    if (free == out.devices.end()) {
      ++out.overflow;
      continue;
    }

    const auto slot = static_cast<std::size_t>(free - out.devices.begin());
    *free = devices[device];
    out.added.push_back(slot);
  }

  std::ranges::sort(out.added);
  return out;
}

} // namespace brokkr::app
