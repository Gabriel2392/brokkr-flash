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
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace brokkr::io {

class RandomAccessSource {
 public:
  virtual ~RandomAccessSource() = default;

  virtual const std::string& label() const noexcept = 0;
  virtual const std::string& identity() const noexcept = 0;
  virtual std::int64_t write_time() const noexcept = 0;
  virtual std::uint64_t size() const noexcept = 0;

  virtual brokkr::core::Result<std::size_t> read_at(std::uint64_t offset,
                                                    std::span<std::byte> out) const noexcept = 0;

  virtual void advise_sequential() const noexcept {}

  brokkr::core::Status read_exact_at(std::uint64_t offset, std::span<std::byte> out) const noexcept;
};

using RandomAccessSourcePtr = std::shared_ptr<const RandomAccessSource>;

brokkr::core::Result<RandomAccessSourcePtr> open_file_source(const std::filesystem::path& path) noexcept;

#if !defined(_WIN32)
brokkr::core::Result<RandomAccessSourcePtr> open_fd_source(int fd, std::string label) noexcept;
#endif

} // namespace brokkr::io
