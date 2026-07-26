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

#include "sysfs_usb.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <charconv>

// clang-format off
#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <cfgmgr32.h>
#include <devpkey.h>
#include <Usbiodef.h>
// clang-format on

#include <spdlog/spdlog.h>

#pragma comment(lib, "setupapi.lib")

struct UsbDeviceInfo {
  uint16_t vendor = 0;
  uint16_t product = 0;
  std::uint64_t connection_id = 0;
  bool has_connection_id = false;
  std::string device_path;
};

namespace {

std::mutex g_ports_mtx;
struct PortState {
  std::uint64_t id = 0;
  std::uint64_t last_arrival = 0;
  bool present = false;
  bool arrival_pending = false;
  bool removed = false;
};
std::unordered_map<std::string, PortState> g_ports;
std::uint64_t g_next_id = 1;

std::string port_key(std::string_view port) {
  std::string normalized(port);
  for (auto& c : normalized) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return normalized;
}

std::uint64_t port_id(std::string_view port, std::uint64_t last_arrival) {
  std::lock_guard lock(g_ports_mtx);
  auto& state = g_ports[port_key(port)];
  const bool first_seen = state.id == 0 && !state.removed;
  if (state.id == 0) state.id = g_next_id++;

  bool new_arrival = false;
  if (last_arrival != 0) {
    if (state.last_arrival == 0) {
      state.last_arrival = last_arrival;
      if (state.arrival_pending) {
        state.arrival_pending = false;
        new_arrival = true;
      }
    } else if (state.last_arrival != last_arrival) {
      state.last_arrival = last_arrival;
      new_arrival = true;
      if (state.arrival_pending)
        state.arrival_pending = false;
      else
        state.id = g_next_id++;
    }
  }

  // Ignore stale rows after removal.
  if (state.present || first_seen || new_arrival) {
    state.present = true;
    state.removed = false;
  }
  return state.id;
}

void port_arrived(std::string_view port) {
  if (port.empty()) return;
  std::lock_guard lock(g_ports_mtx);
  auto& state = g_ports[port_key(port)];
  if (!state.present) {
    state.id = g_next_id++;
    state.arrival_pending = true;
  }
  state.present = true;
  state.removed = false;
}

void port_removed(std::string_view port) {
  if (port.empty()) return;
  std::lock_guard lock(g_ports_mtx);
  auto& state = g_ports[port_key(port)];
  state.present = false;
  state.arrival_pending = false;
  state.removed = true;
}

std::optional<std::uint64_t> last_arrival(HDEVINFO set, SP_DEVINFO_DATA& device) {
  DEVPROPTYPE type = 0;
  FILETIME arrival{};
  if (!SetupDiGetDevicePropertyW(set, &device, &DEVPKEY_Device_LastArrivalDate, &type,
                                 reinterpret_cast<PBYTE>(&arrival), sizeof(arrival), nullptr, 0) ||
      type != DEVPROP_TYPE_FILETIME) {
    return std::nullopt;
  }
  const auto value = (static_cast<std::uint64_t>(arrival.dwHighDateTime) << 32) |
                     static_cast<std::uint64_t>(arrival.dwLowDateTime);
  if (value == 0) return std::nullopt;
  return value;
}

uint16_t extract_hex4(const std::string& str, const std::string& key) {
  std::string lower_str = str;
  for (char& c : lower_str) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  const size_t pos0 = lower_str.find(key);
  if (pos0 == std::string::npos) return 0;

  const size_t pos = pos0 + key.length();
  if (pos + 4 > lower_str.size()) return 0;

  unsigned v = 0;
  auto sv = std::string_view(lower_str).substr(pos, 4);
  auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v, 16);
  if (ec != std::errc{} || ptr != sv.data() + sv.size() || v > 0xFFFFu) return 0;
  return static_cast<uint16_t>(v);
}

std::vector<UsbDeviceInfo> enumerate_usb_devices_windows(uint16_t target_vid,
                                                         const std::vector<uint16_t>& allowed_pids) {
  std::vector<UsbDeviceInfo> result;

  HDEVINFO hDevInfo = SetupDiGetClassDevs(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
  if (hDevInfo == INVALID_HANDLE_VALUE) {
    spdlog::error("SetupDiGetClassDevs failed: {}", GetLastError());
    return result;
  }

  SP_DEVINFO_DATA devInfoData;
  devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

  for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); ++i) {
    char hwId[1024] = {0};

    if (!SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData, SPDRP_HARDWAREID, nullptr, (PBYTE)hwId, sizeof(hwId),
                                           nullptr)) {
      continue;
    }

    std::string hwIdStr(hwId);

    const uint16_t vid = extract_hex4(hwIdStr, "vid_");
    const uint16_t pid = extract_hex4(hwIdStr, "pid_");
    if (vid == 0) continue;

    const bool match_vid = (target_vid == 0 || vid == target_vid);
    const bool match_pid = allowed_pids.empty() ||
                           (std::find(allowed_pids.begin(), allowed_pids.end(), pid) != allowed_pids.end());
    if (!match_vid || !match_pid) continue;

    HKEY hKey = SetupDiOpenDevRegKey(hDevInfo, &devInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
    if (hKey == INVALID_HANDLE_VALUE) continue;

    char portName[256] = {0};
    DWORD type = 0;
    DWORD size = sizeof(portName);

    bool found_port = (RegQueryValueExA(hKey, "PortName", nullptr, &type, (LPBYTE)portName, &size) == ERROR_SUCCESS);

    if (!found_port) {
      size = sizeof(portName);
      found_port = (RegQueryValueExA(hKey, "AttachedTo", nullptr, &type, (LPBYTE)portName, &size) == ERROR_SUCCESS);
    }

    if (found_port) {
      const auto duplicate = std::ranges::find_if(result, [&](const UsbDeviceInfo& existing) {
        return port_key(existing.device_path) == port_key(portName);
      });
      if (duplicate == result.end()) {
        UsbDeviceInfo info;
        info.device_path = portName;
        info.vendor = vid;
        info.product = pid;
        const auto arrival = last_arrival(hDevInfo, devInfoData);
        info.connection_id = port_id(info.device_path, arrival.value_or(0));
        info.has_connection_id = arrival.has_value();
        spdlog::debug("Found Device: {} (VID: 0x{:04x}, PID: 0x{:04x})", info.device_path, vid, pid);
        result.push_back(std::move(info));
      }
    }

    RegCloseKey(hKey);
  }

  SetupDiDestroyDeviceInfoList(hDevInfo);
  return result;
}

} // namespace

namespace brokkr::windows {

std::string UsbDeviceSysfsInfo::devnode() const { return sysname; }

std::string UsbDeviceSysfsInfo::describe() const {
  return fmt::format("{} (VID: 0x{:04x}, PID: 0x{:04x})", sysname, vendor, product);
}

std::vector<UsbDeviceSysfsInfo> enumerate_usb_devices_sysfs(const EnumerateFilter& filter) {
  std::vector<UsbDeviceSysfsInfo> result;
  auto devices = enumerate_usb_devices_windows(filter.vendor, filter.products);
  for (const auto& dev : devices) {
    UsbDeviceSysfsInfo info;
    info.sysname = dev.device_path;
    info.serial_nodes = {dev.device_path};
    info.vendor = dev.vendor;
    info.product = dev.product;
    info.connection_id = dev.connection_id;
    info.has_connection_id = dev.has_connection_id;
    result.push_back(info);
  }
  spdlog::debug("Total USB devices found: {}", result.size());
  return result;
}

std::optional<UsbDeviceSysfsInfo> find_by_sysname(std::string_view sysname) {
  auto devices = enumerate_usb_devices_sysfs(EnumerateFilter{.vendor = 0, .products = {}});
  for (const auto& dev : devices) {
    if (dev.sysname == sysname) return dev;
  }
  return std::nullopt;
}

void note_device_arrival(std::string_view sysname) { port_arrived(sysname); }

void note_device_removal(std::string_view sysname) { port_removed(sysname); }

} // namespace brokkr::windows
