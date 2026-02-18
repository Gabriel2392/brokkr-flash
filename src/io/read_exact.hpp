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

#include "core/status.hpp"
#include "io/source.hpp"

#include <cstddef>
#include <span>

namespace brokkr::io {

inline brokkr::core::Status read_exact(ByteSource& src, std::span<std::byte> out) noexcept {
  std::size_t off = 0;
  while (off < out.size()) {
    const std::size_t got = src.read(out.subspan(off));
    if (!got) {
      auto st = src.status();
      if (!st) return st;
      return brokkr::core::fail("Short read: " + src.display_name());
    }
    off += got;
  }
  return {};
}

} // namespace brokkr::io
