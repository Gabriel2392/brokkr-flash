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

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <vector>

namespace brokkr::app {

inline brokkr::core::Result<std::vector<std::byte>> read_pit_file(const std::filesystem::path& p) {
  std::error_code ec;
  const auto sz = std::filesystem::file_size(p, ec);
  if (ec) return brokkr::core::fail("Cannot stat PIT file.");

  std::vector<std::byte> buf(static_cast<std::size_t>(sz));
  std::ifstream in(p, std::ios::binary);
  if (!in.is_open()) return brokkr::core::fail("Cannot open PIT file.");

  if (!buf.empty()) {
    in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    if (!in.good()) return brokkr::core::fail("Failed to read PIT file.");
  }

  return buf;
}

} // namespace brokkr::app
