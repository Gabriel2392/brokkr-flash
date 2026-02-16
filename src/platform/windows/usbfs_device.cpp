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
#include "usbfs_device.hpp"
#include <iostream>
#include <stdexcept>
#include <system_error>

#include <spdlog/spdlog.h>

#pragma comment(lib, "winusb.lib")

namespace brokkr::windows {

UsbFsDevice::UsbFsDevice(std::string devnode) : devnode_(std::move(devnode)) {}

UsbFsDevice::~UsbFsDevice() { close(); }

UsbFsDevice::UsbFsDevice(UsbFsDevice &&other) noexcept
    : devnode_(std::move(other.devnode_)), handle_(other.handle_),
      ids_(other.ids_), eps_(other.eps_), ifc_num_(other.ifc_num_) {
  other.handle_ = INVALID_HANDLE_VALUE;
}

UsbFsDevice &UsbFsDevice::operator=(UsbFsDevice &&other) noexcept {
  if (this != &other) {
    close();
    devnode_ = std::move(other.devnode_);
    handle_ = other.handle_;
    ids_ = other.ids_;
    eps_ = other.eps_;
    ifc_num_ = other.ifc_num_;

    other.handle_ = INVALID_HANDLE_VALUE;
  }
  return *this;
}

bool UsbFsDevice::open_and_init() {
  // Format the COM port path correctly.
  // Win32 requires the \\.\ prefix to open ports like COM10 and higher.
  std::string port_path = devnode_;
  if (port_path.find("\\\\.\\") == std::string::npos &&
      port_path.find("COM") != std::string::npos) {
    port_path = "\\\\.\\" + port_path;
  }

  spdlog::info("Opening COM port: {}", port_path);

  handle_ = CreateFileA(port_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                        0, // COM ports strictly require exclusive access (0)
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

  if (handle_ == INVALID_HANDLE_VALUE) {
    spdlog::error("Failed to open COM port '{}', error code: {}", port_path,
                  GetLastError());
    return false;
  }

  // Configure standard serial parameters
  DCB dcbSerialParams = {0};
  dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

  if (GetCommState(handle_, &dcbSerialParams)) {
    // 115200 is standard for most modems and Samsung CDC interfaces
    dcbSerialParams.BaudRate = CBR_115200;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    if (!SetCommState(handle_, &dcbSerialParams)) {
      close();
      spdlog::error("Failed to set COM state for '{}', error code: {}",
                    port_path, GetLastError());
      return false;
    }
  } else {
    close();
    spdlog::error("Failed to get COM state for '{}', error code: {}", port_path,
                  GetLastError());
    return false;
  }

  // Note: A COM port is a stream, not a packet interface.
  // We leave the UsbEndpoints (eps_) zeroed out as they are irrelevant for
  // Serial IO.
  return true;
}

void UsbFsDevice::close() noexcept {
  if (handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
  }
}

void UsbFsDevice::reset_device() {
  if (!is_open())
    return;
  // Flush the TX/RX buffers to simulate a device reset on the pipe
  PurgeComm(handle_,
            PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR);
}
} // namespace brokkr::windows