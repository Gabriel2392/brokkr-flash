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
#include <cstddef>
#include <cstdint>
#include <span>

namespace brokkr::core {

inline std::span<const std::byte> bytes(const std::byte* p, std::size_t n) { return {p, n}; }
inline std::span<std::byte> bytes(std::byte* p, std::size_t n) { return {p, n}; }

inline std::span<const std::uint8_t> u8(std::span<const std::byte> s) {
  return {reinterpret_cast<const std::uint8_t*>(s.data()), s.size()};
}
inline std::span<std::uint8_t> u8(std::span<std::byte> s) {
  return {reinterpret_cast<std::uint8_t*>(s.data()), s.size()};
}

} // namespace brokkr::core
