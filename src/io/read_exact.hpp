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
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

inline brokkr::core::Result<std::string> read_text(ByteSource& src, std::size_t max_bytes,
                                                   std::string_view what) noexcept {
  const std::uint64_t n64 = src.size();
  if (n64 > max_bytes) return brokkr::core::fail("read_text: too large: " + std::string(what));
  const std::size_t n = static_cast<std::size_t>(n64);

  std::string s(n, '\0');
  if (!read_exact(src, std::as_writable_bytes(std::span<char>(s))))
    return brokkr::core::fail("Short read: " + std::string(what));
  return s;
}

inline brokkr::core::Result<std::vector<std::byte>> read_all(
    ByteSource& src, std::uint64_t max_bytes = 256ull * 1024ull * 1024ull) noexcept {
  const auto sz64 = src.size();
  if (sz64 > max_bytes) return brokkr::core::fail("Source too large: " + src.display_name());

  std::vector<std::byte> out(static_cast<std::size_t>(sz64));
  BRK_TRY(read_exact(src, out));
  return out;
}

} // namespace brokkr::io
