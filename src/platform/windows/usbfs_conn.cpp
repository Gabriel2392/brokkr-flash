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
#include <span>

#include <windows.h>

namespace brokkr::windows {

namespace {

inline bool is_disconnect_error(DWORD err) noexcept {
  return err == ERROR_GEN_FAILURE || err == ERROR_OPERATION_ABORTED || err == ERROR_NO_SUCH_DEVICE ||
         err == ERROR_FILE_NOT_FOUND;
}

inline void backoff_10ms() noexcept { ::Sleep(10); }

} // namespace

UsbFsConnection::UsbFsConnection(UsbFsDevice& dev) : dev_(dev) {}

brokkr::core::Status UsbFsConnection::open() noexcept {
  if (connected_ && dev_.is_open()) return {};

  if (!dev_.is_open()) {
    auto st = dev_.open_and_init();
    if (!st) return st;
  }

  connected_ = dev_.is_open();
  return connected_ ? brokkr::core::Status{} : brokkr::core::fail("UsbFsConnection: device not open after init");
}

void UsbFsConnection::close() noexcept {
  dev_.close();
  connected_ = false;
}

int UsbFsConnection::send(std::span<const std::uint8_t> data, unsigned retries) {
  if (!connected_ || dev_.handle() == INVALID_HANDLE_VALUE) return -1;

  COMMTIMEOUTS timeouts{};
  timeouts.WriteTotalTimeoutConstant = static_cast<DWORD>(timeout_ms_);
  (void)::SetCommTimeouts(dev_.handle(), &timeouts);

  const std::uint8_t* p = data.data();
  std::size_t left = data.size();
  std::size_t total = 0;

  while (left) {
    const DWORD want =
        static_cast<DWORD>(std::min<std::size_t>(left, std::min<std::size_t>(max_pack_size_, 0xFFFFFFFFu)));

    DWORD bytes_written = 0;

    unsigned attempt = 0;
    for (;;) {
      if (::WriteFile(dev_.handle(), p, want, &bytes_written, nullptr)) {
        if (bytes_written == 0) {
          if (attempt++ >= retries) return -1;
          backoff_10ms();
          continue;
        }
        break;
      }

      const DWORD err = ::GetLastError();
      if (is_disconnect_error(err)) {
        connected_ = false;
        return -1;
      }

      if (err == ERROR_TIMEOUT) {
        if (attempt++ >= retries) return -1;
        backoff_10ms();
        continue;
      }

      if (attempt++ >= retries) return -1;
      backoff_10ms();
    }

    p += bytes_written;
    left -= bytes_written;
    total += bytes_written;
  }

  return static_cast<int>(total);
}

int UsbFsConnection::recv(std::span<std::uint8_t> data, unsigned retries) {
  if (!connected_ || dev_.handle() == INVALID_HANDLE_VALUE) return -1;
  if (data.empty()) return recv_zlp();

  COMMTIMEOUTS timeouts{};
  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutConstant = static_cast<DWORD>(timeout_ms_);
  timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
  (void)::SetCommTimeouts(dev_.handle(), &timeouts);

  std::uint8_t* p = data.data();
  std::size_t left = data.size();
  std::size_t total = 0;

  while (left) {
    const DWORD want =
        static_cast<DWORD>(std::min<std::size_t>(left, std::min<std::size_t>(max_pack_size_, 0xFFFFFFFFu)));

    DWORD bytes_read = 0;

    unsigned attempt = 0;
    for (;;) {
      if (::ReadFile(dev_.handle(), p, want, &bytes_read, nullptr)) {
        if (bytes_read == 0) {
          if (total > 0) return static_cast<int>(total);
          if (attempt++ >= retries) return -1;
          backoff_10ms();
          continue;
        }
        break;
      }

      const DWORD err = ::GetLastError();
      if (is_disconnect_error(err)) {
        connected_ = false;
        return -1;
      }

      if (err == ERROR_TIMEOUT) {
        if (total > 0) return static_cast<int>(total);
        if (attempt++ >= retries) return -1;
        backoff_10ms();
        continue;
      }

      if (attempt++ >= retries) return -1;
      backoff_10ms();
    }

    p += bytes_read;
    left -= bytes_read;
    total += bytes_read;

    if (bytes_read < want) break;
  }

  return static_cast<int>(total);
}

int UsbFsConnection::recv_zlp(unsigned /*retries*/) { return 0; }

} // namespace brokkr::windows
