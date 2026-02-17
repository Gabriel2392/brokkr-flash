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
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

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
  std::string device_path; // Equivalent to devnode
};

namespace fs = std::filesystem;
namespace {
// Helper to extract 4-character hex values (like "vid_1234") from a string
uint16_t extract_hex(const std::string &str, const std::string &key) {
  std::string lower_str = str;
  for (char &c : lower_str)
    c = static_cast<char>(std::tolower(c));

  size_t pos = lower_str.find(key);
  if (pos == std::string::npos)
    return 0;

  pos += key.length();
  if (pos + 4 > lower_str.length())
    return 0;

  try {
    return static_cast<uint16_t>(
        std::stoul(lower_str.substr(pos, 4), nullptr, 16));
  } catch (...) {
    return 0;
  }
}

std::vector<UsbDeviceInfo>
enumerate_usb_devices_windows(uint16_t target_vid,
                              const std::vector<uint16_t> &allowed_pids) {
  std::vector<UsbDeviceInfo> result;

  // Ask SetupAPI to look at ALL devices currently present on the system.
  // We must check everything because Samsung CDC devices can hide under both
  // "Ports" and "Modems".
  HDEVINFO hDevInfo = SetupDiGetClassDevs(nullptr, nullptr, nullptr,
                                          DIGCF_PRESENT | DIGCF_ALLCLASSES);
  if (hDevInfo == INVALID_HANDLE_VALUE) {
    spdlog::error("SetupDiGetClassDevs failed: {}", GetLastError());
    return result;
  }

  SP_DEVINFO_DATA devInfoData;
  devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

  // Loop through every device
  for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); ++i) {
    char hwId[1024] = {0};

    // 1. Get the Hardware ID (e.g., "USB\VID_04E8&PID_685D&REV_021B")
    if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData,
                                          SPDRP_HARDWAREID, nullptr,
                                          (PBYTE)hwId, sizeof(hwId), nullptr)) {
      std::string hwIdStr(hwId);

      // 2. Extract VID/PID
      uint16_t vid = extract_hex(hwIdStr, "vid_");
      uint16_t pid = extract_hex(hwIdStr, "pid_");

      if (vid == 0)
        continue; // Not a USB device

      // Check if this device matches our target VID and allowed PIDs
      bool match_vid = (target_vid == 0 || vid == target_vid);
      bool match_pid = allowed_pids.empty() ||
                       (std::find(allowed_pids.begin(), allowed_pids.end(),
                                  pid) != allowed_pids.end());

      if (match_vid && match_pid) {
        // 3. We found a matching USB device! Now, open its registry key to find
        // the COM port name.
        HKEY hKey = SetupDiOpenDevRegKey(
            hDevInfo, &devInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (hKey != INVALID_HANDLE_VALUE) {
          char portName[256] = {0};
          DWORD type = 0;
          DWORD size = sizeof(portName);

          // Standard COM ports store their name in "PortName"
          bool found_port =
              (RegQueryValueExA(hKey, "PortName", nullptr, &type,
                                (LPBYTE)portName, &size) == ERROR_SUCCESS);

          // Modems (like Samsung Download Mode) sometimes store it in
          // "AttachedTo"
          if (!found_port) {
            size = sizeof(portName);
            found_port =
                (RegQueryValueExA(hKey, "AttachedTo", nullptr, &type,
                                  (LPBYTE)portName, &size) == ERROR_SUCCESS);
          }

          if (found_port) {
            UsbDeviceInfo info;
            info.device_path = portName;
            info.vendor = vid;
            info.product = pid;

            spdlog::info("Found Device: {} (VID: 0x{:04x}, PID: 0x{:04x})",
                         info.device_path, vid, pid);

            result.push_back(std::move(info));
          }
          RegCloseKey(hKey);
        }
      }
    }
  }
  SetupDiDestroyDeviceInfoList(hDevInfo);
  spdlog::info("Total matching devices found: {}", result.size());
  return result;
}
} // namespace

namespace brokkr::windows {

std::string UsbDeviceSysfsInfo::devnode() const {
  return sysname; // On Windows, we can treat the device path as the "devnode"
}

std::vector<UsbDeviceSysfsInfo>
enumerate_usb_devices_sysfs(const EnumerateFilter &filter) {
  std::vector<UsbDeviceSysfsInfo> result;
  auto devices = enumerate_usb_devices_windows(filter.vendor, filter.products);
  for (const auto &dev : devices) {
    UsbDeviceSysfsInfo info;
    info.sysname = dev.device_path; // Using device path as sysname
    info.vendor = dev.vendor;
    info.product = dev.product;
    // Note: Windows does not provide busnum/devnum in the same way as Linux, so
    // we leave them as -1.
    result.push_back(info);
  }
  return result;
}

std::optional<UsbDeviceSysfsInfo> find_by_sysname(std::string_view sysname) {
  auto devices = enumerate_usb_devices_sysfs(
      EnumerateFilter{.vendor = 0, .products = {}}); // Get all devices
  for (const auto &dev : devices) {
    if (dev.sysname == sysname)
      return dev;
  }
  return std::nullopt;
}

} // namespace brokkr::windows
