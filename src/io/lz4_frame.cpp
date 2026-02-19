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

#include "io/lz4_frame.hpp"

#include "io/read_exact.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "third_party/lz4/lz4.h"

#include <spdlog/spdlog.h>

namespace brokkr::io {

namespace {

constexpr std::array<std::byte, 4> kLz4Magic{std::byte{0x04}, std::byte{0x22}, std::byte{0x4D}, std::byte{0x18}};

static std::uint32_t u32_le(std::span<const std::byte, 4> b) {
  const auto u0 = static_cast<std::uint32_t>(static_cast<unsigned char>(b[0]));
  const auto u1 = static_cast<std::uint32_t>(static_cast<unsigned char>(b[1]));
  const auto u2 = static_cast<std::uint32_t>(static_cast<unsigned char>(b[2]));
  const auto u3 = static_cast<std::uint32_t>(static_cast<unsigned char>(b[3]));
  return (u0) | (u1 << 8) | (u2 << 16) | (u3 << 24);
}

static std::uint64_t u64_le(std::span<const std::byte, 8> b) {
  std::uint64_t v = 0;
  for (std::size_t i = 0; i < 8; ++i) v |= (static_cast<std::uint64_t>(static_cast<unsigned char>(b[i])) << (8u * i));
  return v;
}

static std::size_t max_block_size_from_bd(std::uint8_t bd) {
  const std::uint8_t id = static_cast<std::uint8_t>((bd >> 4) & 0x07u);
  switch (id) {
    case 4: return 64u * 1024u;
    case 5: return 256u * 1024u;
    case 6: return 1024u * 1024u;
    case 7: return 4u * 1024u * 1024u;
    default: return 0;
  }
}

} // namespace

brokkr::core::Result<Lz4FrameHeaderInfo> parse_lz4_frame_header(ByteSource& src) noexcept {
  Lz4FrameHeaderInfo info{};

  std::array<std::byte, 4> magic{};
  auto st = read_exact(src, std::span<std::byte>(magic.data(), magic.size()));
  if (!st) return brokkr::core::fail(std::move(st.error()));
  if (magic != kLz4Magic) return brokkr::core::fail("LZ4: bad magic (not standard LZ4 frame)");

  std::array<std::byte, 2> fb{};
  st = read_exact(src, std::span<std::byte>(fb.data(), fb.size()));
  if (!st) return brokkr::core::fail(std::move(st.error()));

  info.flg = static_cast<std::uint8_t>(static_cast<unsigned char>(fb[0]));
  info.bd = static_cast<std::uint8_t>(static_cast<unsigned char>(fb[1]));

  const std::uint8_t version = static_cast<std::uint8_t>((info.flg >> 6) & 0x03u);
  if (version != 1) return brokkr::core::fail("LZ4: unsupported frame version");

  info.block_independence = (info.flg & 0x20u) != 0;
  info.block_checksum = (info.flg & 0x10u) != 0;
  info.has_content_size = (info.flg & 0x08u) != 0;
  info.content_checksum = (info.flg & 0x04u) != 0;
  info.has_dict_id = (info.flg & 0x01u) != 0;

  if (!info.block_independence) return brokkr::core::fail("LZ4: frame must use independent blocks");
  if (info.block_checksum) return brokkr::core::fail("LZ4: block checksum not supported");
  if (info.has_dict_id) return brokkr::core::fail("LZ4: dictionary ID not supported");
  if (!info.has_content_size) return brokkr::core::fail("LZ4: content size missing (compress with --content-size)");

  info.max_block_size = max_block_size_from_bd(info.bd);
  if (info.max_block_size == 0) return brokkr::core::fail("LZ4: invalid BD/max block size");
  if (info.max_block_size > static_cast<std::size_t>(LZ4_ONE_MIB))
    return brokkr::core::fail("LZ4: max block size > 1MiB not supported");

  std::array<std::byte, 8> cs{};
  st = read_exact(src, std::span<std::byte>(cs.data(), cs.size()));
  if (!st) return brokkr::core::fail(std::move(st.error()));
  info.content_size = u64_le(std::span<const std::byte, 8>(cs.data(), 8));

  if (info.content_size > LZ4_ONE_MIB && info.max_block_size != static_cast<std::size_t>(LZ4_ONE_MIB)) {
    return brokkr::core::fail("LZ4: content > 1MiB requires 1MiB blocks (compress with -B6)");
  }

  std::array<std::byte, 1> hc{};
  st = read_exact(src, std::span<std::byte>(hc.data(), hc.size()));
  if (!st) return brokkr::core::fail(std::move(st.error()));

  info.header_bytes = 4 + 1 + 1 + 8 + 1;
  return info;
}

brokkr::core::Result<Lz4BlockStreamReader> Lz4BlockStreamReader::open(std::unique_ptr<ByteSource> src) noexcept {
  if (!src) return brokkr::core::fail("LZ4: null source");
  BRK_TRYV(h, parse_lz4_frame_header(*src));
  return Lz4BlockStreamReader(std::move(src), h);
}

std::size_t Lz4BlockStreamReader::total_blocks_1m() const noexcept {
  if (hdr_.content_size == 0) return 0;
  return static_cast<std::size_t>((hdr_.content_size + (LZ4_ONE_MIB - 1)) / LZ4_ONE_MIB);
}

std::size_t Lz4BlockStreamReader::blocks_remaining_1m() const noexcept {
  const auto t = total_blocks_1m();
  return (blocks_read_ >= t) ? 0u : (t - blocks_read_);
}

brokkr::core::Status Lz4BlockStreamReader::read_exact_(std::span<std::byte> out) noexcept {
  return read_exact(*src_, out);
}

brokkr::core::Result<std::size_t> Lz4BlockStreamReader::read_n_blocks(std::size_t n,
                                                                      std::vector<std::byte>& out) noexcept {
  if (n == 0) return std::size_t{0};

  const std::size_t before = out.size();
  const std::size_t total = total_blocks_1m();
  if (blocks_read_ + n > total) return brokkr::core::fail("LZ4: too many blocks requested");

  for (std::size_t i = 0; i < n; ++i) {
    std::array<std::byte, 4> szb{};
    auto st = read_exact_({szb.data(), szb.size()});
    if (!st) return brokkr::core::fail(std::move(st.error()));

    const std::uint32_t raw_sz = u32_le(std::span<const std::byte, 4>(szb.data(), 4));
    if (raw_sz == 0) return brokkr::core::fail("LZ4: encountered endmark unexpectedly");

    const std::uint32_t payload = raw_sz & 0x7FFFFFFFu;

    const std::size_t off = out.size();
    out.resize(out.size() + 4 + payload);
    std::memcpy(out.data() + off, szb.data(), 4);

    if (payload) {
      st = read_exact_({out.data() + off + 4, payload});
      if (!st) return brokkr::core::fail(std::move(st.error()));
    }

    blocks_read_++;
  }

  return out.size() - before;
}

Lz4DecompressedSource::Lz4DecompressedSource(std::unique_ptr<ByteSource> src, Lz4FrameHeaderInfo hdr) noexcept
    : src_(std::move(src)),
      display_(src_ ? src_->display_name() : std::string{}),
      hdr_(hdr),
      total_out_(hdr_.content_size) {
  block_out_.reserve(static_cast<std::size_t>(LZ4_ONE_MIB));
  comp_payload_.reserve(static_cast<std::size_t>(LZ4_ONE_MIB) + 64);
}

brokkr::core::Result<std::unique_ptr<ByteSource>> Lz4DecompressedSource::open(std::unique_ptr<ByteSource> src) noexcept {
  if (!src) return brokkr::core::fail("LZ4: null source");
  BRK_TRYV(h, parse_lz4_frame_header(*src));
  return std::unique_ptr<ByteSource>(new Lz4DecompressedSource(std::move(src), h));
}

brokkr::core::Status Lz4DecompressedSource::read_exact_(std::span<std::byte> out) noexcept {
  return read_exact(*src_, out);
}

brokkr::core::Status Lz4DecompressedSource::fill_next_block_() noexcept {
  if (produced_ >= total_out_) return brokkr::core::fail("LZ4: internal: produced >= total");
  if (!st_) return st_;

  const std::uint64_t remaining = total_out_ - produced_;
  const std::size_t expected_out = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, LZ4_ONE_MIB));

  std::array<std::byte, 4> szb{};
  auto st = read_exact_({szb.data(), szb.size()});
  if (!st) return st;

  const std::uint32_t raw_sz = u32_le(std::span<const std::byte, 4>(szb.data(), 4));
  if (raw_sz == 0) return brokkr::core::fail("LZ4: encountered endmark unexpectedly while decoding");

  const bool uncompressed = (raw_sz & 0x80000000u) != 0;
  const std::uint32_t payload = raw_sz & 0x7FFFFFFFu;

  comp_payload_.resize(payload);
  if (payload) {
    st = read_exact_({reinterpret_cast<std::byte*>(comp_payload_.data()), payload});
    if (!st) return st;
  }

  block_out_.resize(expected_out);

  if (uncompressed) {
    if (payload != expected_out) return brokkr::core::fail("LZ4: uncompressed block size mismatch");
    std::memcpy(block_out_.data(), comp_payload_.data(), payload);
  } else {
    const int ret = ::LZ4_decompress_safe(comp_payload_.data(), reinterpret_cast<char*>(block_out_.data()),
                                          static_cast<int>(payload), static_cast<int>(expected_out));
    if (ret < 0) return brokkr::core::fail("LZ4: decompression failed (LZ4_decompress_safe)");
    if (static_cast<std::size_t>(ret) != expected_out)
      return brokkr::core::fail("LZ4: decompression produced unexpected size");
  }

  produced_ += expected_out;
  block_off_ = 0;
  return {};
}

std::size_t Lz4DecompressedSource::read(std::span<std::byte> out) {
  if (!st_ || out.empty()) return 0;
  if (produced_ >= total_out_ && block_off_ >= block_out_.size()) return 0;

  std::size_t written = 0;
  while (written < out.size()) {
    if (block_off_ >= block_out_.size()) {
      block_out_.clear();
      block_off_ = 0;

      if (produced_ >= total_out_) break;

      st_ = fill_next_block_();
      if (!st_) {
        spdlog::error("LZ4 read error: {}", st_.error());
        break;
      }
    }

    const std::size_t avail = block_out_.size() - block_off_;
    const std::size_t want = std::min<std::size_t>(avail, out.size() - written);
    std::memcpy(out.data() + written, block_out_.data() + block_off_, want);

    block_off_ += want;
    written += want;
  }

  return written;
}

brokkr::core::Result<std::unique_ptr<ByteSource>> open_lz4_decompressed(std::unique_ptr<ByteSource> src) noexcept {
  return Lz4DecompressedSource::open(std::move(src));
}

} // namespace brokkr::io
