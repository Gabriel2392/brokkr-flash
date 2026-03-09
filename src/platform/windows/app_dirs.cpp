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

#include "platform/windows/app_dirs.hpp"

#include <cstdlib>

namespace brokkr::windows {

brokkr::core::Result<std::filesystem::path> app_cache_dir() noexcept {
  const wchar_t* local_app_data = _wgetenv(L"LOCALAPPDATA");
  if (!local_app_data || !*local_app_data) return brokkr::core::fail("LOCALAPPDATA is not set");
  return std::filesystem::path(local_app_data) / "Brokkr";
}

} // namespace brokkr::windows