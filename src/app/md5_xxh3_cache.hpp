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

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace brokkr::app {

struct Md5Xxh3CacheEntry {
  std::array<unsigned char, 16> md5{};
  std::uint64_t bytes_to_hash = 0;
  std::uint64_t xxh3_64 = 0;
  std::uint64_t touched = 0;
};

std::string md5_hex32(const std::array<unsigned char, 16>& digest);
std::string xxh3_hex16(std::uint64_t digest);

std::filesystem::path md5_xxh3_cache_file(const std::filesystem::path& app_cache_dir) noexcept;

brokkr::core::Result<std::vector<Md5Xxh3CacheEntry>> load_md5_xxh3_cache(
    const std::filesystem::path& cache_file) noexcept;
brokkr::core::Status save_md5_xxh3_cache(const std::filesystem::path& cache_file,
                                         std::vector<Md5Xxh3CacheEntry> entries,
                                         std::size_t max_entries = 65535) noexcept;

std::optional<std::uint64_t> lookup_md5_xxh3_cache(std::vector<Md5Xxh3CacheEntry>& entries,
                                                   const std::array<unsigned char, 16>& md5,
                                                   std::uint64_t bytes_to_hash) noexcept;
bool forget_md5_xxh3_cache(std::vector<Md5Xxh3CacheEntry>& entries,
                           const std::array<unsigned char, 16>& md5,
                           std::uint64_t bytes_to_hash) noexcept;
void remember_md5_xxh3_cache(std::vector<Md5Xxh3CacheEntry>& entries,
                             const std::array<unsigned char, 16>& md5,
                             std::uint64_t bytes_to_hash,
                             std::uint64_t xxh3_64,
                             std::size_t max_entries = 65535) noexcept;

} // namespace brokkr::app