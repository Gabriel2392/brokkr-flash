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
#include "io/tar.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace brokkr::io {

class ByteSource {
public:
  virtual ~ByteSource() = default;

  virtual std::string display_name() const = 0;
  virtual std::uint64_t size() const = 0;

  virtual std::size_t read(std::span<std::byte> out) = 0;
};

brokkr::core::Result<std::unique_ptr<ByteSource>> open_raw_file(const std::filesystem::path& path) noexcept;
brokkr::core::Result<std::unique_ptr<ByteSource>> open_tar_entry(const std::filesystem::path& tar_path, const TarEntry& entry) noexcept;

inline std::string basename(std::string_view path_like) {
  std::filesystem::path p(path_like);
  return p.filename().string();
}

} // namespace brokkr::io
