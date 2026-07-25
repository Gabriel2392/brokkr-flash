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
inline constexpr std::uint64_t kMaxLz4WindowBytes = 31ull * kOneMiB;

inline constexpr std::uint64_t kXmitStartAlign = 128ull * 1024ull;

inline std::uint64_t lz4_window_decomp_bytes(std::uint64_t window_bytes, std::size_t block_size) noexcept {
  if (block_size == 0) return 0;
  const std::uint64_t cap = std::min<std::uint64_t>(window_bytes, kMaxLz4WindowBytes);
  return cap - (cap % block_size);
}

inline std::uint64_t xmit_window_bytes(const pit::Partition& part, std::uint64_t default_bytes,
                                       std::uint64_t pkt) noexcept {
  std::uint64_t win = default_bytes;

  switch (part.dev_type) {
    case 1:
    case 2:
    case 8: win = 30ull * kOneMiB; break;
    case 6: win = 4ull * kOneMiB; break;
    case 7: win = kOneMiB; break;
    case 0: {
      const auto blk = static_cast<std::uint64_t>(part.wire_block_size > 0 ? part.wire_block_size : 0);
      const std::uint64_t units = blk >> 7;
      if (units == 0) break;
      const std::uint64_t base = blk + ((part.attribute == 1) ? units * 4 : 0);
      const std::uint64_t computed = base * 1024ull * (800ull / units);
      if (computed) win = computed;
      break;
    }
    default: break;
  }

  if (pkt == 0 || win == 0) return win;

  const std::uint64_t floored = win - (win % pkt);
  return floored ? floored : pkt;
}

} // namespace detail
} // namespace brokkr::odin
