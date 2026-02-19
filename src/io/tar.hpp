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
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace brokkr::io {

struct TarEntry {
  std::string name;
  std::uint64_t size = 0;
  std::uint64_t data_offset = 0;
};

class TarArchive {
 public:
  static brokkr::core::Result<TarArchive> open(std::string path, bool validate_header_checksums = true) noexcept;

  const std::string& path() const noexcept { return path_; }
  const std::vector<TarEntry>& entries() const noexcept { return entries_; }

  std::optional<TarEntry> find_by_basename(std::string_view base) const;

  static bool is_tar_file(const std::string& path) noexcept;

  std::optional<std::uint64_t> payload_size_bytes() const noexcept { return payload_size_bytes_; }

 private:
  TarArchive() = default;

  static bool validate_header_checksum(std::span<const std::byte, 512> header) noexcept;
  static std::uint64_t parse_octal(std::string_view s) noexcept;
  static brokkr::core::Result<std::uint64_t> parse_tar_number(const char* p, std::size_t n) noexcept;

  static std::string trim_cstr_field(const char* p, std::size_t n);
  static bool header_all_zero(std::span<const std::byte, 512> header) noexcept;

  static std::string join_ustar_name(std::string_view prefix, std::string_view name);

  struct PaxKV {
    std::optional<std::string> path;
    std::optional<std::uint64_t> size;
    void clear() {
      path.reset();
      size.reset();
    }
    void merge_from(const PaxKV& o) {
      if (o.path) path = *o.path;
      if (o.size) size = *o.size;
    }
  };

  static brokkr::core::Result<PaxKV> parse_pax_payload(std::string_view payload) noexcept;

  brokkr::core::Status scan_() noexcept;

 private:
  std::string path_;
  bool validate_ = true;
  std::vector<TarEntry> entries_;
  std::optional<std::uint64_t> payload_size_bytes_;
};

} // namespace brokkr::io
