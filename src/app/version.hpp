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

#include <string>

#include <fmt/format.h>

namespace brokkr::app {

inline const std::string& version_string() {
  static const std::string v = [] {
#ifdef NDEBUG
    return fmt::format("{}-{}", BROKKR_VERSION, BROKKR_BUILD_TYPE);
#else
    return fmt::format("{}-{}+{}", BROKKR_VERSION, BROKKR_BUILD_TYPE, BROKKR_COMMIT_COUNT);
#endif
  }();
  return v;
}

} // namespace brokkr::app
