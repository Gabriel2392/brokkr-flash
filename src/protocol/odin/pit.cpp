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

#include "protocol/odin/pit.hpp"

#include "core/endian.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <utility>

#include <spdlog/spdlog.h>

namespace brokkr::odin::pit {

namespace {

static std::string trim_nul_string(const std::int8_t* p, std::size_t n) {
  const char* begin = reinterpret_cast<const char*>(p);
  const char* end = begin + n;
  const char* nul = static_cast<const char*>(std::memchr(begin, '\0', n));
  return std::string(begin, nul ? nul : end);
}

static std::string trim_fixed_field(const std::int8_t* p, std::size_t n) {
  std::string s = trim_nul_string(p, n);
  while (!s.empty()) {
    const unsigned char c = static_cast<unsigned char>(s.back());
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') s.pop_back();
    else break;
  }
  return s;
}

static std::int32_t block_bytes_for_dev_type(std::int32_t dev_type) noexcept {
  return (dev_type == 8) ? 4096 : 512;
}

struct RawEntry {
  PartitionInfoWire w{};
  std::string name;
  std::string file_name;
};

} // namespace

brokkr::core::Result<PitTable> parse(std::span<const std::byte> bytes) noexcept {
  if (bytes.size() < sizeof(PitHeaderWire)) return brokkr::core::fail("PIT parse: buffer too small for header");

  PitHeaderWire hdr{};
  std::memcpy(&hdr, bytes.data(), sizeof(hdr));

  hdr.magic    = brokkr::core::le_to_host(hdr.magic);
  hdr.count    = brokkr::core::le_to_host(hdr.count);
  hdr.lu_count = brokkr::core::le_to_host(hdr.lu_count);
  hdr.reserved = brokkr::core::le_to_host(hdr.reserved);

  if (hdr.magic != PIT_MAGIC)
  if (hdr.count < 0) return brokkr::core::fail("PIT parse: negative partition count");

  const std::size_t count = static_cast<std::size_t>(hdr.count);
  const std::size_t required = sizeof(PitHeaderWire) + count * sizeof(PartitionInfoWire);
  if (bytes.size() < required) return brokkr::core::fail("PIT parse: buffer smaller than declared partition table");

  PitTable out;
  out.com_tar2  = trim_fixed_field(hdr.com_tar2, sizeof(hdr.com_tar2));
  out.cpu_bl_id = trim_fixed_field(hdr.cpu_bl_id, sizeof(hdr.cpu_bl_id));
  out.lu_count  = hdr.lu_count;

  std::vector<RawEntry> raw;
  raw.reserve(count);

  std::size_t off = sizeof(PitHeaderWire);
  for (std::size_t i = 0; i < count; ++i, off += sizeof(PartitionInfoWire)) {
    PartitionInfoWire w{};
    std::memcpy(&w, bytes.data() + off, sizeof(w));

    w.binType         = brokkr::core::le_to_host(w.binType);
    w.devType         = brokkr::core::le_to_host(w.devType);
    w.id              = brokkr::core::le_to_host(w.id);
    w.attribute       = brokkr::core::le_to_host(w.attribute);
    w.updateAttribute = brokkr::core::le_to_host(w.updateAttribute);
    w.blockSize       = brokkr::core::le_to_host(w.blockSize);
    w.blockLength     = brokkr::core::le_to_host(w.blockLength);
    w.offset          = brokkr::core::le_to_host(w.offset);
    w.fileSize        = brokkr::core::le_to_host(w.fileSize);

    RawEntry r;
    r.w = w;
    r.name = trim_nul_string(w.name, sizeof(w.name));
    r.file_name = trim_nul_string(w.fileName, sizeof(w.fileName));
    raw.push_back(std::move(r));
  }

  std::int32_t max_blockSize = 0, max_offset = 0, max_blockLength = 0;
  for (const auto& r : raw) {
    max_blockSize = std::max(max_blockSize, r.w.blockSize);
    max_offset = std::max(max_offset, r.w.offset);
    max_blockLength = std::max(max_blockLength, r.w.blockLength);
  }

  const bool blockSize_is_begin_block = (max_blockSize > 4096) && (max_offset <= 4096);
  auto begin_block_of = [&](const PartitionInfoWire& w) -> std::int32_t { return blockSize_is_begin_block ? w.blockSize : w.offset; };

  out.partitions.resize(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto& r = raw[i];

    Partition p;
    p.id = r.w.id;
    p.dev_type = r.w.devType;

    p.begin_block = begin_block_of(r.w);
    p.block_bytes = block_bytes_for_dev_type(p.dev_type);

    p.name = r.name;
    p.file_name = r.file_name;

    out.partitions[i] = std::move(p);
  }

  std::unordered_map<std::int32_t, std::vector<std::size_t>> by_dev;
  by_dev.reserve(8);
  for (std::size_t i = 0; i < out.partitions.size(); ++i) by_dev[out.partitions[i].dev_type].push_back(i);

  for (auto& [dev, idxs] : by_dev) {
    (void)dev;
    std::ranges::sort(idxs, [&](std::size_t a, std::size_t b) { return out.partitions[a].begin_block < out.partitions[b].begin_block; });

    for (std::size_t k = 0; k < idxs.size(); ++k) {
      const std::size_t i = idxs[k];
      auto& p = out.partitions[i];

      std::int32_t blocks = 0;

      if (k + 1 < idxs.size()) {
        const std::size_t j = idxs[k + 1];
        const auto next_begin = out.partitions[j].begin_block;
        const auto cur_begin  = p.begin_block;
        if (next_begin > cur_begin) blocks = next_begin - cur_begin;
      } else {
        const auto pit_len = raw[i].w.blockLength;
        if (pit_len > 0) blocks = pit_len;
      }

      p.block_size = blocks;

      const std::uint64_t bb = (p.block_bytes > 0) ? static_cast<std::uint64_t>(p.block_bytes) : 0ull;
      const std::uint64_t bc = (p.block_size  > 0) ? static_cast<std::uint64_t>(p.block_size)  : 0ull;
      p.file_size = bb * bc;
    }
  }

  spdlog::debug("Parsed PIT: {} partitions, cpu_bl_id='{}'", out.partitions.size(), out.cpu_bl_id);
  return out;
}

const Partition* PitTable::find_by_file_name(std::string_view basename) const noexcept {
  if (basename.empty()) return nullptr;
  auto it = std::ranges::find_if(partitions, [&](const Partition& p) { return p.file_name == basename; });
  return (it == partitions.end()) ? nullptr : &*it;
}

std::optional<std::int32_t> PitTable::common_block_size() const noexcept {
  if (partitions.empty()) return std::nullopt;
  const auto bs = partitions.front().block_bytes;
  if (bs <= 0) return std::nullopt;

  if (std::ranges::all_of(partitions, [&](const Partition& p) { return p.block_bytes == bs; })) return bs;
  return std::nullopt;
}

} // namespace brokkr::odin::pit
