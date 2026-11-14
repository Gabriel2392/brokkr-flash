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
#include "io/source.hpp"
#include "io/tar.hpp"
#include "protocol/odin/pit.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace brokkr::odin {

struct ImageSpec {
  enum class Kind { RawFile, TarEntry };

  Kind kind{};
  io::RandomAccessSourcePtr source;
  io::TarEntry entry{};

  std::string basename;
  std::string source_basename;

  std::uint64_t size = 0;
  std::uint64_t disk_size = 0;

  bool lz4 = false;
  std::size_t lz4_block_size = 0;

  std::string display;

  brokkr::core::Result<std::unique_ptr<io::ByteSource>> open() const noexcept;
};

struct FlashItem {
  pit::Partition part;
  ImageSpec spec;
};

struct ExpandOptions {
  bool allow_raw_files = true;
};

brokkr::core::Result<std::vector<ImageSpec>> expand_inputs(const std::vector<io::RandomAccessSourcePtr>& inputs,
                                                           ExpandOptions options = {}) noexcept;
brokkr::core::Result<std::vector<ImageSpec>> expand_inputs_tar_or_raw(
    const std::vector<std::filesystem::path>& inputs) noexcept;
brokkr::core::Result<std::vector<FlashItem>> map_to_pit(const pit::PitTable& pit_table,
                                                        const std::vector<ImageSpec>& sources) noexcept;

std::shared_ptr<const std::vector<std::byte>> pit_from_specs(const std::vector<ImageSpec>& specs);

} // namespace brokkr::odin
