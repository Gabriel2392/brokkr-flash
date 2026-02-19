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

#include "platform/macos/sysfs_usb.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <charconv>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#if !defined(kIOMainPortDefault)
  #define kIOMainPortDefault kIOMasterPortDefault
#endif

namespace brokkr::macos {
namespace {

std::optional<std::uint32_t> get_u32_property(io_service_t service, CFStringRef key) {
  CFTypeRef prop = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
  if (!prop) return std::nullopt;

  std::uint32_t value = 0;
  bool ok = false;
  if (CFGetTypeID(prop) == CFNumberGetTypeID()) {
    ok = CFNumberGetValue(static_cast<CFNumberRef>(prop), kCFNumberSInt32Type, &value);
  }
  CFRelease(prop);
  return ok ? std::optional<std::uint32_t>{value} : std::nullopt;
}

std::optional<std::uint64_t> get_registry_entry_id(io_service_t service) {
  std::uint64_t id = 0;
  const kern_return_t kr = IORegistryEntryGetRegistryEntryID(service, &id);
  if (kr != KERN_SUCCESS) return std::nullopt;
  return id;
}

bool product_allowed(std::uint16_t product, const std::vector<std::uint16_t>& allowed) {
  if (allowed.empty()) return true;
  return std::find(allowed.begin(), allowed.end(), product) != allowed.end();
}

struct Match {
  UsbDeviceSysfsInfo info;
  std::uint64_t registry_id = 0;
};

void enumerate_class(const char* className, const EnumerateFilter& filter, std::vector<Match>& out) {
  CFMutableDictionaryRef dict = IOServiceMatching(className);
  if (!dict) return;

  io_iterator_t iter = 0;
  if (IOServiceGetMatchingServices(kIOMainPortDefault, dict, &iter) != KERN_SUCCESS) return;

  io_service_t service;
  while ((service = IOIteratorNext(iter)) != 0) {
    auto vid_opt = get_u32_property(service, CFSTR("idVendor"));
    auto pid_opt = get_u32_property(service, CFSTR("idProduct"));
    auto loc_opt = get_u32_property(service, CFSTR("locationID"));
    auto rid_opt = get_registry_entry_id(service);

    if (!vid_opt || !pid_opt || !loc_opt) {
      IOObjectRelease(service);
      continue;
    }

    const auto vendor = static_cast<std::uint16_t>(*vid_opt);
    const auto product = static_cast<std::uint16_t>(*pid_opt);
    const auto locationID = *loc_opt;

    spdlog::debug("Found USB device: Loc: 0x{:08x} (VID: 0x{:04x}, PID: 0x{:04x})", locationID, vendor, product);

    if (vendor != filter.vendor || !product_allowed(product, filter.products)) {
      IOObjectRelease(service);
      continue;
    }

    UsbDeviceSysfsInfo info;
    info.sysname = fmt::format("0x{:08x}", locationID);
    info.vendor = vendor;
    info.product = product;

    spdlog::info("Matched USB device: {}", info.describe());

    Match m;
    m.info = std::move(info);
    m.registry_id = rid_opt.value_or(0);
    out.push_back(std::move(m));

    IOObjectRelease(service);
  }

  IOObjectRelease(iter);
}

static std::optional<std::uint32_t> parse_u32_sysname(std::string_view sysname) {
  while (!sysname.empty() && (sysname.front() == ' ' || sysname.front() == '\t')) sysname.remove_prefix(1);
  while (!sysname.empty() &&
         (sysname.back() == ' ' || sysname.back() == '\t' || sysname.back() == '\r' || sysname.back() == '\n'))
    sysname.remove_suffix(1);

  int base = 10;
  if (sysname.size() >= 2 && sysname[0] == '0' && (sysname[1] == 'x' || sysname[1] == 'X')) {
    base = 16;
    sysname.remove_prefix(2);
  }

  std::uint32_t v = 0;
  auto [ptr, ec] = std::from_chars(sysname.data(), sysname.data() + sysname.size(), v, base);
  if (ec != std::errc{} || ptr != sysname.data() + sysname.size()) return std::nullopt;
  return v;
}

} // namespace

io_service_t find_device_by_location(std::uint32_t locationID) {
  const char* classNames[] = {"IOUSBHostDevice", "IOUSBDevice"};

  for (const char* cls : classNames) {
    CFMutableDictionaryRef dict = IOServiceMatching(cls);
    if (!dict) continue;

    io_iterator_t iter = 0;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, dict, &iter) != KERN_SUCCESS) continue;

    io_service_t service;
    while ((service = IOIteratorNext(iter)) != 0) {
      auto loc_opt = get_u32_property(service, CFSTR("locationID"));
      if (loc_opt && *loc_opt == locationID) {
        IOObjectRelease(iter);
        return service;
      }
      IOObjectRelease(service);
    }
    IOObjectRelease(iter);
  }

  return 0;
}

std::string UsbDeviceSysfsInfo::devnode() const { return sysname; }

std::string UsbDeviceSysfsInfo::describe() const {
  return fmt::format("{} (VID: 0x{:04x}, PID: 0x{:04x})", sysname, vendor, product);
}

std::vector<UsbDeviceSysfsInfo> enumerate_usb_devices_sysfs(const EnumerateFilter& filter) {
  std::vector<Match> matches;

  enumerate_class("IOUSBHostDevice", filter, matches);
  if (matches.empty()) enumerate_class("IOUSBDevice", filter, matches);

  std::ranges::sort(matches, [](const Match& a, const Match& b) { return a.registry_id > b.registry_id; });

  std::vector<UsbDeviceSysfsInfo> out;
  out.reserve(matches.size());
  for (auto& m : matches) out.emplace_back(std::move(m.info));
  return out;
}

std::optional<UsbDeviceSysfsInfo> find_by_sysname(std::string_view sysname) {
  const auto loc = parse_u32_sysname(sysname);
  if (!loc) {
    spdlog::error("Invalid sysname format: '{}'", sysname);
    return std::nullopt;
  }

  io_service_t service = find_device_by_location(*loc);
  if (!service) {
    spdlog::error("No device found with locationID: 0x{:08x}", *loc);
    return std::nullopt;
  }

  auto vid_opt = get_u32_property(service, CFSTR("idVendor"));
  auto pid_opt = get_u32_property(service, CFSTR("idProduct"));
  IOObjectRelease(service);

  if (!vid_opt || !pid_opt) {
    spdlog::error("Failed to read properties for device with locationID: 0x{:08x}", *loc);
    return std::nullopt;
  }

  UsbDeviceSysfsInfo info;
  info.sysname = std::string(sysname);
  info.vendor = static_cast<std::uint16_t>(*vid_opt);
  info.product = static_cast<std::uint16_t>(*pid_opt);
  return info;
}

} // namespace brokkr::macos
