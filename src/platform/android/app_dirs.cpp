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

#include "platform/android/app_dirs.hpp"

#include <mutex>

namespace brokkr::android_platform {

namespace {
std::mutex g_cache_dir_mutex;
std::filesystem::path g_cache_dir;
} // namespace

void set_app_cache_dir(std::filesystem::path path) noexcept {
  std::lock_guard<std::mutex> lock(g_cache_dir_mutex);
  g_cache_dir = std::move(path);
}

brokkr::core::Result<std::filesystem::path> app_cache_dir() noexcept {
  std::lock_guard<std::mutex> lock(g_cache_dir_mutex);
  if (g_cache_dir.empty()) {
    return brokkr::core::fail("Android app cache dir has not been configured");
  }
  return g_cache_dir;
}

} // namespace brokkr::android_platform