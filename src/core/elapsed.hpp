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

#include <chrono>
#include <string>

#include <fmt/format.h>

namespace brokkr::core {

inline std::string format_elapsed(std::chrono::steady_clock::duration d) {
  const long long total = std::chrono::round<std::chrono::seconds>(d).count();
  const long long mins = total / 60;
  const long long secs = total % 60;
  const std::string sec_part = fmt::format("{} sec{}", secs, secs == 1 ? "" : "s");
  if (mins < 1) return sec_part;
  return fmt::format("{} min{} and {}", mins, mins == 1 ? "" : "s", sec_part);
}

} // namespace brokkr::core
