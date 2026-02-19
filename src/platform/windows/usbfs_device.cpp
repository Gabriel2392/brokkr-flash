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

#include <cstring>
#include <utility>

namespace brokkr::windows {

namespace {

static std::string win32_err(DWORD e) {
  char* buf = nullptr;
  const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;

  const DWORD n = ::FormatMessageA(flags, nullptr, e, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                   reinterpret_cast<LPSTR>(&buf), 0, nullptr);

  std::string out;
  if (n && buf) out.assign(buf, buf + n);
  if (buf) ::LocalFree(buf);

  while (!out.empty() && (out.back() == '\r' || out.back() == '\n' || out.back() == ' ' || out.back() == '\t'))
    out.pop_back();
  if (out.empty()) out = "error " + std::to_string(static_cast<unsigned>(e));
  return out;
}

} // namespace

UsbFsDevice::UsbFsDevice(std::string devnode) : devnode_(std::move(devnode)) {}
UsbFsDevice::~UsbFsDevice() { close(); }

UsbFsDevice::UsbFsDevice(UsbFsDevice&& o) noexcept { *this = std::move(o); }

UsbFsDevice& UsbFsDevice::operator=(UsbFsDevice&& o) noexcept {
  if (this == &o) return *this;
  close();

  devnode_ = std::move(o.devnode_);
  handle_ = o.handle_;
  ids_ = o.ids_;
  eps_ = o.eps_;
  ifc_num_ = o.ifc_num_;

  o.handle_ = INVALID_HANDLE_VALUE;
  o.ifc_num_ = -1;
  o.ids_ = {};
  o.eps_ = {};
  return *this;
}

brokkr::core::Status UsbFsDevice::open_and_init() noexcept {
  close();

  std::string port_path = devnode_;
  if (port_path.rfind("\\\\.\\", 0) != 0 && port_path.find("COM") != std::string::npos)
    port_path = "\\\\.\\" + port_path;

  handle_ = ::CreateFileA(port_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, nullptr);

  if (handle_ == INVALID_HANDLE_VALUE) {
    const DWORD e = ::GetLastError();
    return brokkr::core::fail("Failed to open COM port '" + port_path + "': " + win32_err(e));
  }

  DCB dcb{};
  dcb.DCBlength = sizeof(dcb);

  if (!::GetCommState(handle_, &dcb)) {
    const DWORD e = ::GetLastError();
    close();
    return brokkr::core::fail("GetCommState failed: " + win32_err(e));
  }

  dcb.BaudRate = CBR_115200;
  dcb.ByteSize = 8;
  dcb.StopBits = ONESTOPBIT;
  dcb.Parity = NOPARITY;

  if (!::SetCommState(handle_, &dcb)) {
    const DWORD e = ::GetLastError();
    close();
    return brokkr::core::fail("SetCommState failed: " + win32_err(e));
  }

  return {};
}

void UsbFsDevice::close() noexcept {
  if (handle_ != INVALID_HANDLE_VALUE) {
    ::CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
  }
}

void UsbFsDevice::reset_device() noexcept {
  if (!is_open()) return;
  (void)::PurgeComm(handle_, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR);
}

} // namespace brokkr::windows
