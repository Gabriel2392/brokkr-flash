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

#include "platform/linux/sysfs_usb.hpp"

#include "core/utils.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/spdlog.h>

namespace brokkr::linux {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kSysUsbDevices = "/sys/bus/usb/devices";

std::string read_text_file(const fs::path& p) {
  std::ifstream in(p);
  if (!in.is_open()) return {};
  std::string s;
  std::getline(in, s);
  return s;
}

std::vector<std::string> find_serial_nodes(std::string_view sysname) {
  std::error_code ec;
  std::vector<std::string> out;
  const fs::path base{kSysUsbDevices};

  for (const auto& entry : fs::directory_iterator(base, ec)) {
    const auto name = entry.path().filename().string();
    if (!name.starts_with(sysname) || name.size() <= sysname.size() || name[sysname.size()] != ':') continue;

    const fs::path tty_dir = entry.path() / "tty";
    if (!fs::is_directory(tty_dir, ec)) continue;

    for (const auto& tty : fs::directory_iterator(tty_dir, ec)) {
      const fs::path node = fs::path{"/dev"} / tty.path().filename();
      if (fs::exists(node, ec)) out.push_back(node.string());
    }
  }

  std::ranges::sort(out);
  return out;
}

std::optional<UsbDeviceSysfsInfo> load_one(const fs::path& dir, std::string sysname) {
  const fs::path idVendorPath = dir / "idVendor";
  const fs::path idProductPath = dir / "idProduct";
  const fs::path busnumPath = dir / "busnum";
  const fs::path devnumPath = dir / "devnum";

  if (!fs::exists(idVendorPath) || !fs::exists(idProductPath) || !fs::exists(busnumPath) || !fs::exists(devnumPath)) {
    return std::nullopt;
  }

  const auto vend = brokkr::core::parse_int_trimmed<std::uint16_t>(read_text_file(idVendorPath), 16);
  const auto prod = brokkr::core::parse_int_trimmed<std::uint16_t>(read_text_file(idProductPath), 16);
  const auto bus = brokkr::core::parse_int_trimmed<int>(read_text_file(busnumPath), 10);
  const auto dev = brokkr::core::parse_int_trimmed<int>(read_text_file(devnumPath), 10);

  if (!vend || !prod || !bus || !dev) return std::nullopt;

  UsbDeviceSysfsInfo out;
  out.sysname = std::move(sysname);
  out.vendor = *vend;
  out.product = *prod;
  out.busnum = *bus;
  out.devnum = *dev;
  out.connection_id = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(*bus)) << 32) |
                      static_cast<std::uint32_t>(*dev);
  out.has_connection_id = true;

  const fs::path cdPath = dir / "power" / "connected_duration";
  if (fs::exists(cdPath)) {
    if (auto ms = brokkr::core::parse_int_trimmed<int>(read_text_file(cdPath), 10)) {
      out.connected_duration_sec = *ms / 1000;
    }
  }

  return out;
}

} // namespace

std::string UsbDeviceSysfsInfo::devnode() const { return fmt::format("/dev/bus/usb/{:03d}/{:03d}", busnum, devnum); }

std::string UsbDeviceSysfsInfo::describe() const {
  return fmt::format("{} (Interface {}:{}, VID: 0x{:04x}, PID: 0x{:04x}, connected for {} seconds)", sysname, devnum,
                     busnum, vendor, product, connected_duration_sec);
}

std::vector<UsbDeviceSysfsInfo> enumerate_usb_devices_sysfs(const EnumerateFilter& filter) {
  std::vector<UsbDeviceSysfsInfo> out;

  const fs::path base{kSysUsbDevices};
  if (!fs::exists(base) || !fs::is_directory(base)) return out;

  for (const auto& entry : fs::directory_iterator(base)) {
    if (!entry.is_directory()) continue;
    const auto sysname = entry.path().filename().string();

    auto info = load_one(entry.path(), sysname);
    if (!info) continue;

    spdlog::debug("Found USB device: {} (VID: 0x{:04x}, PID: 0x{:04x})", info->sysname, info->vendor, info->product);

    if (info->vendor != filter.vendor) continue;
    if (!brokkr::core::matches_filter_or_all<std::uint16_t>(filter.products, info->product)) continue;

    info->serial_nodes = find_serial_nodes(info->sysname);
    spdlog::debug("Matched USB device: {}", info->describe());

    out.emplace_back(std::move(*info));
  }

  std::ranges::sort(out,
                    [](const auto& a, const auto& b) { return a.connected_duration_sec > b.connected_duration_sec; });

  return out;
}

std::optional<UsbDeviceSysfsInfo> find_by_sysname(std::string_view sysname) {
  const fs::path dir = fs::path{kSysUsbDevices} / std::string(sysname);
  if (!fs::exists(dir) || !fs::is_directory(dir)) return std::nullopt;

  auto info = load_one(dir, std::string(sysname));
  if (info) info->serial_nodes = find_serial_nodes(info->sysname);
  return info;
}

} // namespace brokkr::linux
