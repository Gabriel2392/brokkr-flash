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
#include <vector>

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

std::optional<std::uint32_t> get_u32_property(io_service_t service,
                                              CFStringRef key) {
  CFTypeRef prop =
      IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
  if (!prop)
    return std::nullopt;

  std::uint32_t value = 0;
  bool ok = false;
  if (CFGetTypeID(prop) == CFNumberGetTypeID()) {
    ok = CFNumberGetValue(static_cast<CFNumberRef>(prop), kCFNumberSInt32Type,
                          &value);
  }
  CFRelease(prop);
  return ok ? std::optional<std::uint32_t>{value} : std::nullopt;
}

io_service_t find_device_by_location(std::uint32_t locationID) {
    const char* classNames[] = { "IOUSBHostDevice", "IOUSBDevice" };

    for (const char* cls : classNames) {
        CFMutableDictionaryRef dict = IOServiceMatching(cls);
        if (!dict) continue;

        io_iterator_t iter = 0;
        if (IOServiceGetMatchingServices(kIOMainPortDefault, dict, &iter) != KERN_SUCCESS) {
            continue;
        }

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

bool product_allowed(std::uint16_t product,
                     const std::vector<std::uint16_t> &allowed) {
  if (allowed.empty())
    return true;
  return std::find(allowed.begin(), allowed.end(), product) != allowed.end();
}

void enumerate_class(const char *className, const EnumerateFilter &filter,
                     std::vector<UsbDeviceSysfsInfo> &out) {
  CFMutableDictionaryRef dict = IOServiceMatching(className);
  if (!dict)
    return;

  io_iterator_t iter = 0;
  // IOServiceGetMatchingServices consumes the dictionary
  if (IOServiceGetMatchingServices(kIOMainPortDefault, dict, &iter) !=
      KERN_SUCCESS)
    return;

  io_service_t service;
  while ((service = IOIteratorNext(iter)) != 0) {
    auto vid_opt = get_u32_property(service, CFSTR("idVendor"));
    auto pid_opt = get_u32_property(service, CFSTR("idProduct"));
    auto loc_opt = get_u32_property(service, CFSTR("locationID"));

    if (!vid_opt || !pid_opt || !loc_opt) {
      IOObjectRelease(service);
      continue;
    }

    auto vendor = static_cast<std::uint16_t>(*vid_opt);
    auto product = static_cast<std::uint16_t>(*pid_opt);
    auto locationID = *loc_opt;

    spdlog::debug("Found USB device: 0x{:08x} (VID: 0x{:04x}, PID: 0x{:04x})",
                  locationID, vendor, product);

    if (!product_allowed(product, filter.products)) {
      IOObjectRelease(service);
      continue;
    }

    UsbDeviceSysfsInfo info;
    info.sysname = fmt::format("0x{:08x}", locationID);
    info.vendor = vendor;
    info.product = product;
    info.busnum = static_cast<int>((locationID >> 24) & 0xFF);
    info.devnum = static_cast<int>(locationID & 0xFFFF);

    spdlog::info(
        "Matched USB device: {} (VID: 0x{:04x}, PID: 0x{:04x})",
        info.sysname, info.vendor, info.product);

    out.push_back(std::move(info));
    IOObjectRelease(service);
  }

  IOObjectRelease(iter);
}

} // namespace

std::string UsbDeviceSysfsInfo::devnode() const { return sysname; }

std::vector<UsbDeviceSysfsInfo>
enumerate_usb_devices_sysfs(const EnumerateFilter &filter) {
  std::vector<UsbDeviceSysfsInfo> out;

  enumerate_class("IOUSBHostDevice", filter, out);
  if (out.empty()) {
    enumerate_class("IOUSBDevice", filter, out);
  }

  std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
    return a.connected_duration_sec > b.connected_duration_sec;
  });

  return out;
}

std::optional<UsbDeviceSysfsInfo> find_by_sysname(std::string_view sysname) {
  std::uint32_t locationID = 0;
  try {
    std::string s(sysname);
    locationID = static_cast<std::uint32_t>(std::stoul(s, nullptr, 0));
  } catch (...) {
    return std::nullopt;
  }

  io_service_t service = find_device_by_location(locationID);
  if (!service)
    return std::nullopt;

  auto vid_opt = get_u32_property(service, CFSTR("idVendor"));
  auto pid_opt = get_u32_property(service, CFSTR("idProduct"));
  IOObjectRelease(service);

  if (!vid_opt || !pid_opt)
    return std::nullopt;

  UsbDeviceSysfsInfo info;
  info.sysname = std::string(sysname);
  info.vendor = static_cast<std::uint16_t>(*vid_opt);
  info.product = static_cast<std::uint16_t>(*pid_opt);
  info.busnum = static_cast<int>((locationID >> 24) & 0xFF);
  info.devnum = static_cast<int>(locationID & 0xFFFF);

  return info;
}

} // namespace brokkr::macos
