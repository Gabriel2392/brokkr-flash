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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace brokkr::io {
class Lz4BlockStreamReader;
} // namespace brokkr::io

namespace brokkr::odin {

struct ImageSpec {
  enum class Kind { RawFile, TarEntry };

  Kind kind{};
  std::filesystem::path path;
  io::TarEntry entry{};

  std::string basename;
  std::string source_basename;

  std::uint64_t size = 0;
  std::uint64_t disk_size = 0;

  bool lz4 = false;

  std::string display;

  brokkr::core::Result<std::unique_ptr<io::ByteSource>> open() const noexcept;
};

struct FlashItem {
  pit::Partition part;
  ImageSpec spec;
};

brokkr::core::Result<std::vector<ImageSpec>> expand_inputs_tar_or_raw(
    const std::vector<std::filesystem::path>& inputs) noexcept;
brokkr::core::Result<std::vector<FlashItem>> map_to_pit(const pit::PitTable& pit_table,
                                                        const std::vector<ImageSpec>& sources) noexcept;

bool is_pit_name(std::string_view base) noexcept;
std::shared_ptr<const std::vector<std::byte>> pit_from_specs(const std::vector<ImageSpec>& specs);

namespace detail {

inline brokkr::core::Status checked_add_u64(std::uint64_t& acc, std::uint64_t v, std::string_view what) noexcept {
  if (std::numeric_limits<std::uint64_t>::max() - acc < v)
    return brokkr::core::fail("Overflow while computing " + std::string(what));
  acc += v;
  return {};
}

constexpr std::uint64_t round_up64(std::uint64_t n, std::uint64_t base) noexcept {
  if (base == 0) return n;
  const auto r = n % base;
  return r ? (n + (base - r)) : n;
}

inline constexpr std::uint64_t kOneMiB = 1024ull * 1024ull;
inline constexpr std::size_t kMaxNonFinalLz4Blocks = 31;

inline constexpr std::uint64_t kXmitStartAlign = 128ull * 1024ull;

inline std::size_t lz4_nonfinal_block_limit(std::uint64_t buffer_bytes) noexcept {
  const auto want = static_cast<std::size_t>(buffer_bytes / kOneMiB);
  return std::min<std::size_t>(want, kMaxNonFinalLz4Blocks);
}

inline std::uint64_t xmit_window_bytes(const pit::Partition& part, std::uint64_t default_bytes) noexcept {
  switch (part.dev_type) {
    case 1:
    case 2:
    case 8: return 30ull * kOneMiB;
    case 6: return 4ull * kOneMiB;
    case 7: return kOneMiB;
    case 0: {
      const auto blk = static_cast<std::uint64_t>(part.wire_block_size > 0 ? part.wire_block_size : 0);
      const std::uint64_t units = blk >> 7;
      if (units == 0) return default_bytes;
      const std::uint64_t base = blk + ((part.attribute == 1) ? units * 4 : 0);
      const std::uint64_t win = base * 1024ull * (800ull / units);
      return win ? win : default_bytes;
    }
    default: return default_bytes;
  }
}

} // namespace detail
} // namespace brokkr::odin
