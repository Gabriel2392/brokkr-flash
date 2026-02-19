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

namespace brokkr::odin::pit {

inline constexpr std::int32_t PIT_MAGIC = 0x12349876;

#pragma pack(push, 1)
struct PitHeaderWire {
  std::int32_t magic;
  std::int32_t count;
  std::int8_t com_tar2[8];
  std::int8_t cpu_bl_id[8];
  std::uint16_t lu_count;
  std::uint16_t reserved;
};
#pragma pack(pop)
static_assert(sizeof(PitHeaderWire) == 28);

#pragma pack(push, 1)
struct PartitionInfoWire {
  std::int32_t binType;
  std::int32_t devType;
  std::int32_t id;
  std::int32_t attribute;
  std::int32_t updateAttribute;

  std::int32_t blockSize;
  std::int32_t blockLength;
  std::int32_t offset;
  std::int32_t fileSize;

  std::int8_t name[32];
  std::int8_t fileName[32];
  std::int8_t deltaName[32];
};
#pragma pack(pop)
static_assert(sizeof(PartitionInfoWire) == 4 * 9 + 32 * 3);

struct Partition {
  std::int32_t id = 0;
  std::int32_t dev_type = 0;

  std::int32_t begin_block = 0;
  std::int32_t block_bytes = 0;
  std::int32_t block_size = 0;
  std::uint64_t file_size = 0;

  std::string name;
  std::string file_name;
};

struct PitTable {
  std::string com_tar2;
  std::string cpu_bl_id;
  std::uint16_t lu_count = 0;

  std::vector<Partition> partitions;

  const Partition* find_by_file_name(std::string_view basename) const noexcept;
  std::optional<std::int32_t> common_block_size() const noexcept;
};

brokkr::core::Result<PitTable> parse(std::span<const std::byte> bytes) noexcept;

} // namespace brokkr::odin::pit
