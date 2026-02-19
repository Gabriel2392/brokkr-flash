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

#include <bit>
#include <cstdint>
#include <type_traits>

namespace brokkr::core {

template <class T>
  requires std::is_integral_v<T>
constexpr T le_to_host(T v) noexcept {
  if constexpr (std::endian::native == std::endian::little)
    return v;
  else
    return std::byteswap(v);
}

template <class T>
  requires std::is_integral_v<T>
constexpr T host_to_le(T v) noexcept {
  if constexpr (std::endian::native == std::endian::little)
    return v;
  else
    return std::byteswap(v);
}

} // namespace brokkr::core
