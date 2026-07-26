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

#include "platform/serial_port.hpp"

#include <cctype>
#include <string>

#include <windows.h>

namespace brokkr::platform {

namespace {

class Handle {
 public:
  explicit Handle(HANDLE h) noexcept : h_(h) {}
  ~Handle() {
    if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
  }

  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;

  bool valid() const noexcept { return h_ != INVALID_HANDLE_VALUE; }
  HANDLE get() const noexcept { return h_; }

 private:
  HANDLE h_ = INVALID_HANDLE_VALUE;
};

std::string normalize_com_path(std::string port) {
  if (port.empty()) return {};
  if (port.rfind("\\\\.\\", 0) == 0) return port;

  std::string upper = port;
  for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  if (upper.rfind("COM", 0) == 0) return std::string("\\\\.\\") + port;
  return {};
}

brokkr::core::Status configure(HANDLE h) noexcept {
  DCB dcb{};
  dcb.DCBlength = sizeof(DCB);
  if (!GetCommState(h, &dcb)) return brokkr::core::failf("GetCommState failed: {}", GetLastError());

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

  if (!SetCommState(h, &dcb)) return brokkr::core::failf("SetCommState failed: {}", GetLastError());

  COMMTIMEOUTS to{};
  to.ReadIntervalTimeout = 50;
  to.ReadTotalTimeoutConstant = 100;
  to.ReadTotalTimeoutMultiplier = 10;
  to.WriteTotalTimeoutConstant = 1000;
  to.WriteTotalTimeoutMultiplier = 10;
  (void)SetCommTimeouts(h, &to);

  return {};
}

} // namespace

brokkr::core::Status write_serial_port(const std::string& node, std::string_view data) noexcept {
  const std::string path = normalize_com_path(node);
  if (path.empty()) return brokkr::core::fail("invalid port name");

  const Handle h(CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr));
  if (!h.valid()) return brokkr::core::failf("open failed: {}", GetLastError());

  BRK_TRY(configure(h.get()));

  DWORD wrote = 0;
  if (!WriteFile(h.get(), data.data(), static_cast<DWORD>(data.size()), &wrote, nullptr))
    return brokkr::core::failf("write failed: {}", GetLastError());
  if (wrote != static_cast<DWORD>(data.size())) return brokkr::core::fail("write timed out (short write)");

  (void)FlushFileBuffers(h.get());
  return {};
}

} // namespace brokkr::platform
