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

#include "usbfs_conn.hpp"

#include <algorithm>
#include <cstdint>

#include <iostream>
#include <span>
#include <vector>
#include <windows.h>
#include <winusb.h>

#include <spdlog/spdlog.h>

#pragma comment(lib, "winusb.lib")

namespace brokkr::windows {

UsbFsConnection::UsbFsConnection(UsbFsDevice &dev) : dev_(dev) {}

bool UsbFsConnection::open() {
  if (!dev_.is_open()) {
    if (!dev_.open_and_init()) {
      spdlog::error("Failed to open and initialize device at '{}'",
                    dev_.devnode());
      return false;
    }
  }
  connected_ = dev_.is_open();
  return connected_;
}

void UsbFsConnection::close() noexcept {
  dev_.close();
  connected_ = false;
}

int UsbFsConnection::send(std::span<const std::uint8_t> data,
                          unsigned retries) {
  if (!connected_ || dev_.handle() == INVALID_HANDLE_VALUE)
    return -1;

  // Apply the configured timeout to this write operation
  COMMTIMEOUTS timeouts = {0};
  timeouts.WriteTotalTimeoutConstant = timeout_ms_;
  SetCommTimeouts(dev_.handle(), &timeouts);

  DWORD bytes_written = 0;
  for (unsigned i = 0; i <= retries; ++i) {
    if (WriteFile(dev_.handle(), data.data(), static_cast<DWORD>(data.size()),
                  &bytes_written, nullptr)) {
      return static_cast<int>(bytes_written);
    }
	int err = GetLastError();
	// Certain errors indicate the device is no longer present (e.g. due to rebooting).
	// Do not print out error. Instead, treat it as a disconnection and break out of the loop.
    if (err == ERROR_GEN_FAILURE || 
        err == ERROR_OPERATION_ABORTED ||
        err == ERROR_NO_SUCH_DEVICE ||
        err == ERROR_FILE_NOT_FOUND) {
        spdlog::info("Device disconnected (likely rebooting).");
		return 0; // Treat as clean disconnection with no more data to write.
    }
    spdlog::warn("WriteFile attempt {} failed with error code {}", i + 1, err);
  }
  return -1;
}

int UsbFsConnection::recv(std::span<std::uint8_t> data, unsigned retries) {
  if (!connected_ || dev_.handle() == INVALID_HANDLE_VALUE)
    return -1;

  // Set aggressive read timeouts.
  // This setup tells Windows to return immediately if any bytes are present,
  // or wait up to timeout_ms_ if the buffer is entirely empty.
  COMMTIMEOUTS timeouts = {0};
  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutConstant = timeout_ms_;
  timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
  SetCommTimeouts(dev_.handle(), &timeouts);

  DWORD bytes_read = 0;
  for (unsigned i = 0; i <= retries; ++i) {
    if (ReadFile(dev_.handle(), data.data(), static_cast<DWORD>(data.size()),
                 &bytes_read, nullptr)) {
      return static_cast<int>(bytes_read);
    }
	int err = GetLastError();
    // Certain errors indicate the device is no longer present (e.g. due to rebooting).
    // Do not print out error. Instead, treat it as a disconnection and break out of the loop.
    if (err == ERROR_GEN_FAILURE ||
        err == ERROR_OPERATION_ABORTED ||
        err == ERROR_NO_SUCH_DEVICE ||
        err == ERROR_FILE_NOT_FOUND) {
        spdlog::info("Device disconnected (likely rebooting).");
		return 0; // Treat as clean disconnection with no more data to read.
    }
    spdlog::warn("ReadFile attempt {} failed with error code {}", i + 1, err);
  }
  return -1;
}

int UsbFsConnection::recv_zlp(unsigned retries) {
  // Zero-Length Packets (ZLPs) are a USB bulk/interrupt concept.
  // Serial ports operate on a continuous byte stream, so we simulate
  // a successful "empty read" to keep API parity with the Linux endpoints.
  return 0;
}

} // namespace brokkr::windows