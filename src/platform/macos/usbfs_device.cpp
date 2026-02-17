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

#include "platform/macos/usbfs_device.hpp"

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>

#include <spdlog/spdlog.h>

#if !defined(kIOMainPortDefault)
#define kIOMainPortDefault kIOMasterPortDefault
#endif

namespace brokkr::macos {

// sysfs_usb.cpp
extern io_service_t find_device_by_location(std::uint32_t locationID);

namespace {

static brokkr::core::Status fail_iokit(const char* what, kern_return_t kr) noexcept {
  return brokkr::core::Status::Failf("{}: IOKit error 0x{:08x}", what, static_cast<unsigned>(kr));
}

static std::optional<std::uint32_t> parse_location_id(std::string_view s) noexcept {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);

  int base = 10;
  if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    base = 16;
    s.remove_prefix(2);
  }

  std::uint32_t v = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v, base);
  if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
  return v;
}

} // namespace

UsbFsDevice::UsbFsDevice(std::string devnode) : devnode_(std::move(devnode)) {}
UsbFsDevice::~UsbFsDevice() { close(); }

UsbFsDevice::UsbFsDevice(UsbFsDevice&& o) noexcept { *this = std::move(o); }

UsbFsDevice& UsbFsDevice::operator=(UsbFsDevice&& o) noexcept {
  if (this == &o) return *this;
  close();

  devnode_ = std::move(o.devnode_);
  dev_intf_ = o.dev_intf_;
  o.dev_intf_ = nullptr;

  ifc_intf_ = o.ifc_intf_;
  o.ifc_intf_ = nullptr;

  ids_ = o.ids_;
  eps_ = o.eps_;
  ifc_num_ = o.ifc_num_;
  o.ifc_num_ = -1;

  pipe_in_ = o.pipe_in_;
  o.pipe_in_ = 0;

  pipe_out_ = o.pipe_out_;
  o.pipe_out_ = 0;

  return *this;
}

brokkr::core::Status UsbFsDevice::open_and_init() noexcept {
  close();

  const auto loc = parse_location_id(devnode_);
  if (!loc) return brokkr::core::Status::Failf("Invalid macOS USB locationID sysname: '{}'", devnode_);

  io_service_t service = find_device_by_location(*loc);
  if (!service) return brokkr::core::Status::Failf("USB device not found at location: {}", devnode_);

  IOCFPlugInInterface** plugIn = nullptr;
  SInt32 score = 0;
  kern_return_t kr = IOCreatePlugInInterfaceForService(
      service, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugIn, &score);
  IOObjectRelease(service);

  if (kr != kIOReturnSuccess || !plugIn) return fail_iokit("IOCreatePlugInInterfaceForService(device)", kr);

  IOUSBDeviceInterface320** devIntf = nullptr;
  const HRESULT res = (*plugIn)->QueryInterface(
      plugIn, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID320), reinterpret_cast<LPVOID*>(&devIntf));
  (*plugIn)->Release(plugIn);

  if (res != S_OK || !devIntf) return brokkr::core::Status::Fail("Failed to get IOUSBDeviceInterface320");

  dev_intf_ = devIntf;

  kr = (*devIntf)->USBDeviceOpen(devIntf);
  if (kr != kIOReturnSuccess) {
    kr = (*devIntf)->USBDeviceOpenSeize(devIntf);
    if (kr != kIOReturnSuccess) {
      (*devIntf)->Release(devIntf);
      dev_intf_ = nullptr;
      return fail_iokit("USBDeviceOpen", kr);
    }
  }

  UInt16 vid = 0, pid = 0;
  (void)(*devIntf)->GetDeviceVendor(devIntf, &vid);
  (void)(*devIntf)->GetDeviceProduct(devIntf, &pid);
  ids_.vendor = vid;
  ids_.product = pid;

  UInt8 curCfg = 0;
  if ((*devIntf)->GetConfiguration(devIntf, &curCfg) == kIOReturnSuccess && curCfg == 0) {
    IOUSBConfigurationDescriptorPtr configDesc = nullptr;
    kr = (*devIntf)->GetConfigurationDescriptorPtr(devIntf, 0, &configDesc);
    if (kr == kIOReturnSuccess && configDesc) {
      (void)(*devIntf)->SetConfiguration(devIntf, configDesc->bConfigurationValue);
    }
  }

  IOUSBFindInterfaceRequest ifcRequest;
  ifcRequest.bInterfaceClass = kIOUSBFindInterfaceDontCare;
  ifcRequest.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
  ifcRequest.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
  ifcRequest.bAlternateSetting = kIOUSBFindInterfaceDontCare;

  io_iterator_t ifcIter = 0;
  kr = (*devIntf)->CreateInterfaceIterator(devIntf, &ifcRequest, &ifcIter);
  if (kr != kIOReturnSuccess) {
    close();
    return fail_iokit("CreateInterfaceIterator", kr);
  }

  io_service_t usbIfc = 0;
  while ((usbIfc = IOIteratorNext(ifcIter)) != 0) {
    IOCFPlugInInterface** ifcPlugIn = nullptr;
    SInt32 ifcScore = 0;
    kr = IOCreatePlugInInterfaceForService(
        usbIfc, kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID, &ifcPlugIn, &ifcScore);
    IOObjectRelease(usbIfc);

    if (kr != kIOReturnSuccess || !ifcPlugIn) continue;

    IOUSBInterfaceInterface300** ifcIntf = nullptr;
    const HRESULT ires = (*ifcPlugIn)->QueryInterface(
        ifcPlugIn, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300), reinterpret_cast<LPVOID*>(&ifcIntf));
    (*ifcPlugIn)->Release(ifcPlugIn);

    if (ires != S_OK || !ifcIntf) continue;

    kr = (*ifcIntf)->USBInterfaceOpen(ifcIntf);
    if (kr != kIOReturnSuccess) {
      (*ifcIntf)->Release(ifcIntf);
      continue;
    }

    UInt8 alt = 0;
    if ((*ifcIntf)->GetAlternateSetting(ifcIntf, &alt) == kIOReturnSuccess && alt != 0) {
      (void)(*ifcIntf)->SetAlternateInterface(ifcIntf, 0);
      (void)(*ifcIntf)->GetAlternateSetting(ifcIntf, &alt);
    }
    if (alt != 0) {
      (*ifcIntf)->USBInterfaceClose(ifcIntf);
      (*ifcIntf)->Release(ifcIntf);
      continue;
    }

    UInt8 numEndpoints = 0;
    (void)(*ifcIntf)->GetNumEndpoints(ifcIntf, &numEndpoints);

    UInt8 ifcNumber = 0;
    (void)(*ifcIntf)->GetInterfaceNumber(ifcIntf, &ifcNumber);

    std::uint8_t foundIn = 0, foundOut = 0;
    UsbEndpoints foundEps{};

    for (UInt8 pipe = 1; pipe <= numEndpoints; pipe++) {
      UInt8 direction = 0, number = 0, transferType = 0, interval = 0;
      UInt16 maxPacketSize = 0;
      kr = (*ifcIntf)->GetPipeProperties(ifcIntf, pipe, &direction, &number, &transferType, &maxPacketSize, &interval);
      if (kr != kIOReturnSuccess) continue;

      if (transferType == kUSBBulk) {
        if (direction == kUSBIn && !foundIn) {
          foundIn = pipe;
          foundEps.bulk_in = static_cast<std::uint8_t>(number | 0x80);
          foundEps.bulk_in_max_packet = maxPacketSize;
        } else if (direction == kUSBOut && !foundOut) {
          foundOut = pipe;
          foundEps.bulk_out = number;
          foundEps.bulk_out_max_packet = maxPacketSize;
        }
      }
    }

    if (foundIn && foundOut) {
      ifc_intf_ = ifcIntf;
      ifc_num_ = static_cast<int>(ifcNumber);
      pipe_in_ = foundIn;
      pipe_out_ = foundOut;
      eps_ = foundEps;
      break;
    }

    (*ifcIntf)->USBInterfaceClose(ifcIntf);
    (*ifcIntf)->Release(ifcIntf);
  }

  IOObjectRelease(ifcIter);

  if (!ifc_intf_) {
    close();
    return brokkr::core::Status::Fail("No interface with bulk endpoints found");
  }

  spdlog::info("Opened USB device at {} (VID: 0x{:04x}, PID: 0x{:04x}, bulk_in: 0x{:02x}, bulk_out: 0x{:02x})",
               devnode_, ids_.vendor, ids_.product, eps_.bulk_in, eps_.bulk_out);

  return brokkr::core::Status::Ok();
}

void UsbFsDevice::close() noexcept {
  if (ifc_intf_) {
    auto ifcIntf = static_cast<IOUSBInterfaceInterface300**>(ifc_intf_);
    (void)(*ifcIntf)->USBInterfaceClose(ifcIntf);
    (*ifcIntf)->Release(ifcIntf);
    ifc_intf_ = nullptr;
  }

  if (dev_intf_) {
    auto devIntf = static_cast<IOUSBDeviceInterface320**>(dev_intf_);
    (void)(*devIntf)->USBDeviceClose(devIntf);
    (*devIntf)->Release(devIntf);
    dev_intf_ = nullptr;
  }

  pipe_in_ = 0;
  pipe_out_ = 0;
  ifc_num_ = -1;
  eps_ = {};
  ids_ = {};
}

void UsbFsDevice::reset_device() noexcept {
  if (!dev_intf_) return;
  auto devIntf = static_cast<IOUSBDeviceInterface320**>(dev_intf_);
  (void)(*devIntf)->ResetDevice(devIntf);
}

} // namespace brokkr::macos
