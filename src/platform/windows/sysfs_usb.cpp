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
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
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
  std::string device_path;
};

namespace {

constexpr std::string_view kSuddlmod = "AT+SUDDLMOD=0,0\r";
constexpr std::uint16_t kOdinPids[] = {0x6601, 0x685D, 0x68C3};

bool is_odin_pid(std::uint16_t pid) {
  return std::find(std::begin(kOdinPids), std::end(kOdinPids), pid) != std::end(kOdinPids);
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
      UsbDeviceInfo info;
      info.device_path = portName;
      info.vendor = vid;
      info.product = pid;
      spdlog::debug("Found Device: {} (VID: 0x{:04x}, PID: 0x{:04x})", info.device_path, vid, pid);
      result.push_back(std::move(info));
    }

    RegCloseKey(hKey);
  }

  SetupDiDestroyDeviceInfoList(hDevInfo);
  return result;
}

std::string normalize_com_path(std::string port) {
  if (port.empty()) return {};
  if (port.rfind("\\\\.\\", 0) == 0) return port;

  std::string upper = port;
  for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  if (upper.rfind("COM", 0) == 0) return std::string("\\\\.\\") + port;
  return {};
}

std::optional<std::string> send_suddlmod_win32(std::string_view comPort) {
  const std::string path = normalize_com_path(std::string(comPort));
  if (path.empty()) return std::string("invalid port name");

  HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) return std::string("open failed: ") + std::to_string(GetLastError());

  DCB dcb{};
  dcb.DCBlength = sizeof(DCB);
  if (!GetCommState(h, &dcb)) {
    const auto e = GetLastError();
    CloseHandle(h);
    return std::string("GetCommState failed: ") + std::to_string(e);
  }

  dcb.BaudRate = CBR_115200;
  dcb.ByteSize = 8;
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  dcb.fOutxCtsFlow = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fOutX = FALSE;
  dcb.fInX = FALSE;
  dcb.fRtsControl = RTS_CONTROL_DISABLE;
  dcb.fDtrControl = DTR_CONTROL_ENABLE;

  if (!SetCommState(h, &dcb)) {
    const auto e = GetLastError();
    CloseHandle(h);
    return std::string("SetCommState failed: ") + std::to_string(e);
  }

  COMMTIMEOUTS to{};
  to.ReadIntervalTimeout = 50;
  to.ReadTotalTimeoutConstant = 100;
  to.ReadTotalTimeoutMultiplier = 10;
  to.WriteTotalTimeoutConstant = 1000;
  to.WriteTotalTimeoutMultiplier = 10;
  (void)SetCommTimeouts(h, &to);

  DWORD wrote = 0;
  const BOOL ok = WriteFile(h, kSuddlmod.data(), static_cast<DWORD>(kSuddlmod.size()), &wrote, nullptr);
  if (!ok || wrote != static_cast<DWORD>(kSuddlmod.size())) {
    const auto e = GetLastError();
    CloseHandle(h);
    return std::string("write failed: ") + std::to_string(e);
  }

  (void)FlushFileBuffers(h);
  CloseHandle(h);
  return std::nullopt;
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
    info.vendor = dev.vendor;
    info.product = dev.product;
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

SuddlmodResult send_suddlmod_to_samsung_serial(std::uint16_t vendor, int maxTargets) {
  SuddlmodResult out;
  const auto devices = enumerate_usb_devices_windows(vendor, {});

  std::set<std::string> uniquePorts;
  for (const auto& d : devices) {
    if (is_odin_pid(d.product)) continue;
    if (!d.device_path.empty()) uniquePorts.insert(d.device_path);
  }

  std::vector<std::string> ports(uniquePorts.begin(), uniquePorts.end());
  if (maxTargets > 0 && static_cast<int>(ports.size()) > maxTargets) {
    ports.resize(static_cast<std::size_t>(maxTargets));
  }
  out.ports_seen = static_cast<int>(ports.size());

  std::vector<std::optional<std::string>> errors(ports.size());
  std::vector<std::thread> workers;
  workers.reserve(ports.size());

  for (std::size_t i = 0; i < ports.size(); ++i) {
    workers.emplace_back([&, i]() { errors[i] = send_suddlmod_win32(ports[i]); });
  }
  for (auto& t : workers)
    if (t.joinable()) t.join();

  for (std::size_t i = 0; i < ports.size(); ++i) {
    if (errors[i]) {
      out.failures.push_back(ports[i] + ": " + *errors[i]);
      continue;
    }
    ++out.sent_ok;
  }

  return out;
}

} // namespace brokkr::windows
