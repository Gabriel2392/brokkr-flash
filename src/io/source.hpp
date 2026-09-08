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

#include "core/path_utf8.hpp"
#include "core/status.hpp"
#include "io/random_access.hpp"
#include "io/tar.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace brokkr::io {

class ByteSource {
 public:
  virtual ~ByteSource() = default;

  virtual std::string display_name() const = 0;
  virtual std::uint64_t size() const = 0;

  virtual std::size_t read(std::span<std::byte> out) = 0;

  virtual brokkr::core::Status status() const noexcept { return {}; }
};

std::unique_ptr<ByteSource> open_range(RandomAccessSourcePtr src, std::uint64_t offset, std::uint64_t size,
                                       std::string display);

brokkr::core::Result<std::unique_ptr<ByteSource>> open_raw_file(const std::filesystem::path& path) noexcept;

std::unique_ptr<ByteSource> open_tar_entry(RandomAccessSourcePtr src, const TarEntry& entry);
brokkr::core::Result<std::unique_ptr<ByteSource>> open_tar_entry(const std::filesystem::path& tar_path,
                                                                 const TarEntry& entry) noexcept;

inline std::string tar_basename(std::string_view path_like) {
  bool valid_utf8 = true;
  for (std::size_t i = 0; i < path_like.size();) {
    const auto c = static_cast<unsigned char>(path_like[i]);
    std::size_t continuation = 0;
    if (c <= 0x7f) {
      ++i;
      continue;
    }
    if (c >= 0xc2 && c <= 0xdf) continuation = 1;
    else if (c >= 0xe0 && c <= 0xef) continuation = 2;
    else if (c >= 0xf0 && c <= 0xf4) continuation = 3;
    else {
      valid_utf8 = false;
      break;
    }
    if (i + continuation >= path_like.size()) {
      valid_utf8 = false;
      break;
    }
    for (std::size_t j = 1; j <= continuation; ++j) {
      if ((static_cast<unsigned char>(path_like[i + j]) & 0xc0) != 0x80) {
        valid_utf8 = false;
        break;
      }
    }
    if (!valid_utf8) break;
    if ((c == 0xe0 && static_cast<unsigned char>(path_like[i + 1]) < 0xa0) ||
        (c == 0xed && static_cast<unsigned char>(path_like[i + 1]) >= 0xa0) ||
        (c == 0xf0 && static_cast<unsigned char>(path_like[i + 1]) < 0x90) ||
        (c == 0xf4 && static_cast<unsigned char>(path_like[i + 1]) >= 0x90)) {
      valid_utf8 = false;
      break;
    }
    i += continuation + 1;
  }
  const auto separator = valid_utf8 ? path_like.find_last_of("/\\") : path_like.find_last_of('/');
  return std::string(separator == std::string_view::npos ? path_like : path_like.substr(separator + 1));
}

inline std::string basename(std::string_view path_like) {
  return brokkr::core::path_to_utf8(brokkr::core::path_from_utf8(path_like).filename());
}

} // namespace brokkr::io
