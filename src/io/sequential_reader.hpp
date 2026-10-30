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
#include "io/random_access.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace brokkr::io {

class SequentialReader {
 public:
  virtual ~SequentialReader() = default;

  virtual brokkr::core::Result<std::span<const std::byte>> next() noexcept = 0;
};

brokkr::core::Result<std::unique_ptr<SequentialReader>> open_sequential_reader(RandomAccessSourcePtr source,
                                                                               std::uint64_t bytes,
                                                                               std::size_t chunk_bytes) noexcept;

} // namespace brokkr::io
