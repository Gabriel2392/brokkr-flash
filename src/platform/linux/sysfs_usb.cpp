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

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <spdlog/spdlog.h>

namespace brokkr::linux {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kSysUsbDevices = "/sys/bus/usb/devices";

std::string read_text_file(const fs::path &p) {
  std::ifstream in(p);
  if (!in.is_open())
    return {};
  std::string s;
  std::getline(in, s);
  return s;
}

std::optional<int> parse_int_dec(std::string_view s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' ||
                        s.back() == ' ' || s.back() == '\t'))
    s.remove_suffix(1);
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    s.remove_prefix(1);

  int v = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v, 10);
  if (ec != std::errc{})
    return std::nullopt;
  if (ptr != s.data() + s.size())
    return std::nullopt;
  return v;
}

std::optional<std::uint16_t> parse_u16_hex(std::string_view s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' ||
                        s.back() == ' ' || s.back() == '\t'))
    s.remove_suffix(1);
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    s.remove_prefix(1);

  unsigned v = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v, 16);
  if (ec != std::errc{})
    return std::nullopt;
  if (ptr != s.data() + s.size())
    return std::nullopt;
  if (v > 0xFFFFu)
    return std::nullopt;
  return static_cast<std::uint16_t>(v);
}

bool product_allowed(std::uint16_t product,
                     const std::vector<std::uint16_t> &allowed) {
  if (allowed.empty())
    return true;
  return std::find(allowed.begin(), allowed.end(), product) != allowed.end();
}

std::optional<UsbDeviceSysfsInfo> load_one(const fs::path &dir,
                                           std::string sysname) {
  const fs::path idVendorPath = dir / "idVendor";
  const fs::path idProductPath = dir / "idProduct";
  const fs::path busnumPath = dir / "busnum";
  const fs::path devnumPath = dir / "devnum";

  if (!fs::exists(idVendorPath) || !fs::exists(idProductPath) ||
      !fs::exists(busnumPath) || !fs::exists(devnumPath)) {
    return std::nullopt;
  }

  const auto vend = parse_u16_hex(read_text_file(idVendorPath));
  const auto prod = parse_u16_hex(read_text_file(idProductPath));
  const auto bus = parse_int_dec(read_text_file(busnumPath));
  const auto dev = parse_int_dec(read_text_file(devnumPath));

  if (!vend || !prod || !bus || !dev)
    return std::nullopt;

  UsbDeviceSysfsInfo out;
  out.sysname = std::move(sysname);
  out.vendor = *vend;
  out.product = *prod;
  out.busnum = *bus;
  out.devnum = *dev;

  const fs::path cdPath = dir / "power" / "connected_duration";
  if (fs::exists(cdPath)) {
    if (auto ms = parse_int_dec(read_text_file(cdPath))) {
      out.connected_duration_sec = *ms / 1000;
    }
  }

  return out;
}

} // namespace

std::string UsbDeviceSysfsInfo::devnode() const {
  std::ostringstream oss;
  oss << "/dev/bus/usb/";
  oss.width(3);
  oss.fill('0');
  oss << busnum;
  oss << "/";
  oss.width(3);
  oss.fill('0');
  oss << devnum;
  return oss.str();
}

std::vector<UsbDeviceSysfsInfo>
enumerate_usb_devices_sysfs(const EnumerateFilter &filter) {
  std::vector<UsbDeviceSysfsInfo> out;

  const fs::path base{kSysUsbDevices};
  if (!fs::exists(base) || !fs::is_directory(base))
    return out;

  for (const auto &entry : fs::directory_iterator(base)) {
    if (!entry.is_directory())
      continue;
    const auto sysname = entry.path().filename().string();

    auto info = load_one(entry.path(), sysname);
    if (!info)
      continue;

    spdlog::debug("Found USB device: {} (VID: 0x{:04x}, PID: 0x{:04x})",
                  info->sysname, info->vendor, info->product);

    if (info->vendor != filter.vendor)
      continue;
    if (!product_allowed(info->product, filter.products))
      continue;

    spdlog::info("Matched USB device: {} (VID: 0x{:04x}, PID: 0x{:04x}, "
                 "connected for {} seconds)",
                 info->sysname, info->vendor, info->product,
                 info->connected_duration_sec);

    out.push_back(std::move(*info));
  }

  std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
    return a.connected_duration_sec > b.connected_duration_sec;
  });

  return out;
}

std::optional<UsbDeviceSysfsInfo> find_by_sysname(std::string_view sysname) {
  const fs::path dir = fs::path{kSysUsbDevices} / std::string(sysname);
  if (!fs::exists(dir) || !fs::is_directory(dir))
    return std::nullopt;
  return load_one(dir, std::string(sysname));
}

} // namespace brokkr::linux
