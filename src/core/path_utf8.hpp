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

#include <filesystem>
#include <string>
#include <string_view>

namespace brokkr::core {

inline std::string path_to_utf8(const std::filesystem::path& p) {
  const auto s = p.u8string();
  return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

inline std::string path_to_utf8_generic(const std::filesystem::path& p) {
  const auto s = p.generic_u8string();
  return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

inline std::filesystem::path path_from_utf8(std::string_view s) {
  if (s.empty()) return {};
  return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

} // namespace brokkr::core
