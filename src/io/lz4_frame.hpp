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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace brokkr::io {

inline constexpr std::uint64_t LZ4_ONE_MIB = 1024ull * 1024ull;

struct Lz4FrameHeaderInfo {
  std::uint64_t content_size = 0;

  std::uint8_t flg = 0;
  std::uint8_t bd = 0;

  bool block_independence = false;
  bool block_checksum = false;
  bool content_checksum = false;
  bool has_content_size = false;
  bool has_dict_id = false;

  std::size_t max_block_size = 0;
  std::size_t header_bytes = 0;
};

brokkr::core::Result<Lz4FrameHeaderInfo> parse_lz4_frame_header(ByteSource& src) noexcept;

class Lz4BlockStreamReader {
 public:
  static brokkr::core::Result<Lz4BlockStreamReader> open(std::unique_ptr<ByteSource> src) noexcept;

  Lz4BlockStreamReader(const Lz4BlockStreamReader&) = delete;
  Lz4BlockStreamReader& operator=(const Lz4BlockStreamReader&) = delete;

  Lz4BlockStreamReader(Lz4BlockStreamReader&&) noexcept = default;
  Lz4BlockStreamReader& operator=(Lz4BlockStreamReader&&) noexcept = default;

  std::string display_name() const { return src_ ? src_->display_name() : std::string{}; }
  std::uint64_t content_size() const noexcept { return hdr_.content_size; }
  const Lz4FrameHeaderInfo& header() const noexcept { return hdr_; }

  std::size_t total_blocks_1m() const noexcept;
  std::size_t blocks_read_1m() const noexcept { return blocks_read_; }
  std::size_t blocks_remaining_1m() const noexcept;

  brokkr::core::Result<std::size_t> read_n_blocks(std::size_t n, std::vector<std::byte>& out) noexcept;

 private:
  Lz4BlockStreamReader(std::unique_ptr<ByteSource> src, Lz4FrameHeaderInfo hdr) noexcept
      : src_(std::move(src)), hdr_(hdr) {}

  brokkr::core::Status read_exact_(std::span<std::byte> out) noexcept;

 private:
  std::unique_ptr<ByteSource> src_;
  Lz4FrameHeaderInfo hdr_{};
  std::size_t blocks_read_ = 0;
};

class Lz4DecompressedSource final : public ByteSource {
 public:
  static brokkr::core::Result<std::unique_ptr<ByteSource>> open(std::unique_ptr<ByteSource> src) noexcept;

  std::string display_name() const override { return display_; }
  std::uint64_t size() const override { return total_out_; }
  std::size_t read(std::span<std::byte> out) override;

  brokkr::core::Status status() const noexcept override { return st_; }

 private:
  Lz4DecompressedSource(std::unique_ptr<ByteSource> src, Lz4FrameHeaderInfo hdr) noexcept;

  brokkr::core::Status read_exact_(std::span<std::byte> out) noexcept;
  brokkr::core::Status fill_next_block_() noexcept;

 private:
  std::unique_ptr<ByteSource> src_;
  std::string display_;
  Lz4FrameHeaderInfo hdr_{};

  std::uint64_t total_out_ = 0;
  std::uint64_t produced_ = 0;

  std::vector<std::byte> block_out_;
  std::size_t block_off_ = 0;

  std::vector<char> comp_payload_;

  brokkr::core::Status st_{};
};

brokkr::core::Result<std::unique_ptr<ByteSource>> open_lz4_decompressed(std::unique_ptr<ByteSource> src) noexcept;

} // namespace brokkr::io
