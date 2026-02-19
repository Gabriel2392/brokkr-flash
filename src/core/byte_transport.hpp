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

#pragma once

#include <cstdint>
#include <span>

namespace brokkr::core {

class IByteTransport {
 public:
  enum class Kind { UsbBulk, TcpStream };

  virtual ~IByteTransport() = default;

  virtual Kind kind() const noexcept = 0;

  virtual bool connected() const noexcept = 0;

  virtual void set_timeout_ms(int ms) noexcept = 0;
  virtual int timeout_ms() const noexcept = 0;

  virtual int send(std::span<const std::uint8_t> data, unsigned retries = 8) = 0;
  virtual int recv(std::span<std::uint8_t> data, unsigned retries = 8) = 0;

  // Ghost func when operating over tcp.
  virtual int recv_zlp(unsigned retries = 0) = 0;
};

} // namespace brokkr::core
