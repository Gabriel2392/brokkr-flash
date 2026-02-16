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

#include <cstdint>
#include <stdexcept>
#include <string>
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

namespace {

[[noreturn]] static void throw_iokit(const char *what, kern_return_t kr) {
  spdlog::error("{}: IOKit error 0x{:08x}", what, static_cast<unsigned>(kr));
  throw std::runtime_error(what);
}

static std::uint32_t parse_location_id(const std::string &s) {
  return static_cast<std::uint32_t>(std::stoul(s, nullptr, 0));
}

static io_service_t find_usb_device_by_location(std::uint32_t locationID) {
  const char *classNames[] = {"IOUSBHostDevice", "IOUSBDevice"};
  for (const char *cls : classNames) {
    CFMutableDictionaryRef dict = IOServiceMatching(cls);
    if (!dict)
      continue;

    SInt32 loc = static_cast<SInt32>(locationID);
    CFNumberRef locRef =
        CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &loc);
    CFDictionarySetValue(dict, CFSTR("locationID"), locRef);
    CFRelease(locRef);

    io_service_t service =
        IOServiceGetMatchingService(kIOMainPortDefault, dict);
    if (service)
      return service;
  }
  return 0;
}

} // namespace

UsbFsDevice::UsbFsDevice(std::string devnode) : devnode_(std::move(devnode)) {}

UsbFsDevice::~UsbFsDevice() { close(); }

UsbFsDevice::UsbFsDevice(UsbFsDevice &&o) noexcept { *this = std::move(o); }

UsbFsDevice &UsbFsDevice::operator=(UsbFsDevice &&o) noexcept {
  if (this == &o)
    return *this;
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

void UsbFsDevice::open_and_init() {
  close();

  const std::uint32_t locationID = parse_location_id(devnode_);
  io_service_t service = find_usb_device_by_location(locationID);
  if (!service) {
    throw std::runtime_error("USB device not found at location: " + devnode_);
  }

  // Create plugin interface for the device
  IOCFPlugInInterface **plugIn = nullptr;
  SInt32 score;
  kern_return_t kr = IOCreatePlugInInterfaceForService(
      service, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugIn,
      &score);
  IOObjectRelease(service);

  if (kr != kIOReturnSuccess || !plugIn) {
    throw std::runtime_error("Failed to create plugin for USB device: " +
                             devnode_);
  }

  // Query for the device interface
  IOUSBDeviceInterface320 **devIntf = nullptr;
  HRESULT res = (*plugIn)->QueryInterface(
      plugIn, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID320),
      reinterpret_cast<LPVOID *>(&devIntf));
  (*plugIn)->Release(plugIn);

  if (res != S_OK || !devIntf) {
    throw std::runtime_error("Failed to get device interface: " + devnode_);
  }

  dev_intf_ = devIntf;

  // Open the device
  kr = (*devIntf)->USBDeviceOpen(devIntf);
  if (kr != kIOReturnSuccess) {
    kr = (*devIntf)->USBDeviceOpenSeize(devIntf);
    if (kr != kIOReturnSuccess) {
      (*devIntf)->Release(devIntf);
      dev_intf_ = nullptr;
      throw_iokit("USBDeviceOpen", kr);
    }
  }

  // Read device IDs
  UInt16 vid = 0, pid = 0;
  (*devIntf)->GetDeviceVendor(devIntf, &vid);
  (*devIntf)->GetDeviceProduct(devIntf, &pid);
  ids_.vendor = vid;
  ids_.product = pid;

  // Set configuration
  IOUSBConfigurationDescriptorPtr configDesc = nullptr;
  kr = (*devIntf)->GetConfigurationDescriptorPtr(devIntf, 0, &configDesc);
  if (kr == kIOReturnSuccess && configDesc) {
    (void)(*devIntf)->SetConfiguration(devIntf,
                                       configDesc->bConfigurationValue);
  }

  // Find interface with bulk endpoints
  IOUSBFindInterfaceRequest ifcRequest;
  ifcRequest.bInterfaceClass = kIOUSBFindInterfaceDontCare;
  ifcRequest.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
  ifcRequest.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
  ifcRequest.bAlternateSetting = kIOUSBFindInterfaceDontCare;

  io_iterator_t ifcIter = 0;
  kr = (*devIntf)->CreateInterfaceIterator(devIntf, &ifcRequest, &ifcIter);
  if (kr != kIOReturnSuccess) {
    (*devIntf)->USBDeviceClose(devIntf);
    (*devIntf)->Release(devIntf);
    dev_intf_ = nullptr;
    throw_iokit("CreateInterfaceIterator", kr);
  }

  io_service_t usbIfc;
  while ((usbIfc = IOIteratorNext(ifcIter)) != 0) {
    IOCFPlugInInterface **ifcPlugIn = nullptr;
    SInt32 ifcScore;
    kr = IOCreatePlugInInterfaceForService(usbIfc,
                                           kIOUSBInterfaceUserClientTypeID,
                                           kIOCFPlugInInterfaceID, &ifcPlugIn,
                                           &ifcScore);
    IOObjectRelease(usbIfc);

    if (kr != kIOReturnSuccess || !ifcPlugIn)
      continue;

    IOUSBInterfaceInterface300 **ifcIntf = nullptr;
    res = (*ifcPlugIn)->QueryInterface(
        ifcPlugIn, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300),
        reinterpret_cast<LPVOID *>(&ifcIntf));
    (*ifcPlugIn)->Release(ifcPlugIn);

    if (res != S_OK || !ifcIntf)
      continue;

    kr = (*ifcIntf)->USBInterfaceOpen(ifcIntf);
    if (kr != kIOReturnSuccess) {
      (*ifcIntf)->Release(ifcIntf);
      continue;
    }

    UInt8 numEndpoints = 0;
    (*ifcIntf)->GetNumEndpoints(ifcIntf, &numEndpoints);

    UInt8 ifcNumber = 0;
    (*ifcIntf)->GetInterfaceNumber(ifcIntf, &ifcNumber);

    std::uint8_t foundIn = 0, foundOut = 0;
    UsbEndpoints foundEps{};

    for (UInt8 pipe = 1; pipe <= numEndpoints; pipe++) {
      UInt8 direction = 0, number = 0, transferType = 0, interval = 0;
      UInt16 maxPacketSize = 0;
      kr = (*ifcIntf)->GetPipeProperties(ifcIntf, pipe, &direction, &number,
                                         &transferType, &maxPacketSize,
                                         &interval);
      if (kr != kIOReturnSuccess)
        continue;

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
    (*devIntf)->USBDeviceClose(devIntf);
    (*devIntf)->Release(devIntf);
    dev_intf_ = nullptr;
    throw std::runtime_error(
        "No interface with bulk endpoints found: " + devnode_);
  }

  spdlog::info("Opened USB device at {} (VID: 0x{:04x}, PID: 0x{:04x}, "
               "bulk_in: 0x{:02x}, bulk_out: 0x{:02x})",
               devnode_, ids_.vendor, ids_.product, eps_.bulk_in,
               eps_.bulk_out);
}

void UsbFsDevice::close() noexcept {
  if (ifc_intf_) {
    auto ifcIntf = static_cast<IOUSBInterfaceInterface300 **>(ifc_intf_);
    (*ifcIntf)->USBInterfaceClose(ifcIntf);
    (*ifcIntf)->Release(ifcIntf);
    ifc_intf_ = nullptr;
  }

  if (dev_intf_) {
    auto devIntf = static_cast<IOUSBDeviceInterface320 **>(dev_intf_);
    (*devIntf)->USBDeviceClose(devIntf);
    (*devIntf)->Release(devIntf);
    dev_intf_ = nullptr;
  }

  pipe_in_ = 0;
  pipe_out_ = 0;
  ifc_num_ = -1;
}

void UsbFsDevice::reset_device() {
  if (!dev_intf_)
    return;
  auto devIntf = static_cast<IOUSBDeviceInterface320 **>(dev_intf_);
  (void)(*devIntf)->ResetDevice(devIntf);
}

} // namespace brokkr::macos
