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

#include "io/source.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace brokkr::io {

inline constexpr std::uint64_t LZ4_ONE_MIB = 1024ull * 1024ull;

struct Lz4FrameHeaderInfo {
    std::uint64_t content_size = 0;

    std::uint8_t flg = 0;
    std::uint8_t bd  = 0;

    bool block_independence = false;
    bool block_checksum     = false;
    bool content_checksum   = false;
    bool has_content_size   = false;
    bool has_dict_id        = false;

    std::size_t max_block_size = 0;

    std::size_t header_bytes = 0;
};

Lz4FrameHeaderInfo parse_lz4_frame_header_or_throw(ByteSource& src);

//   u32 block_size_le;  (bit31 => uncompressed block)
//   u8 block_data[block_size & 0x7fffffff]
class Lz4BlockStreamReader {
public:
    explicit Lz4BlockStreamReader(std::unique_ptr<ByteSource> src);

    std::string display_name() const { return src_ ? src_->display_name() : std::string{}; }
    std::uint64_t content_size() const noexcept { return hdr_.content_size; }
    const Lz4FrameHeaderInfo& header() const noexcept { return hdr_; }

    std::size_t total_blocks_1m() const noexcept;

    std::size_t blocks_read_1m() const noexcept { return blocks_read_; }
    std::size_t blocks_remaining_1m() const noexcept;

    std::size_t read_n_blocks(std::size_t n, std::vector<std::byte>& out);

private:
    void read_exact_(std::span<std::byte> out);

private:
    std::unique_ptr<ByteSource> src_;
    Lz4FrameHeaderInfo hdr_{};
    std::size_t blocks_read_ = 0;
};

class Lz4DecompressedSource final : public ByteSource {
public:
    explicit Lz4DecompressedSource(std::unique_ptr<ByteSource> src);

    std::string display_name() const override { return display_; }
    std::uint64_t size() const override { return total_out_; }
    std::size_t read(std::span<std::byte> out) override;

private:
    void read_exact_(std::span<std::byte> out);
    bool fill_next_block_();

private:
    std::unique_ptr<ByteSource> src_;
    std::string display_;

    Lz4FrameHeaderInfo hdr_{};

    std::uint64_t total_out_ = 0;
    std::uint64_t produced_ = 0;

    std::vector<std::byte> block_out_;
    std::size_t block_off_ = 0;

    std::vector<char> comp_payload_;
};

std::unique_ptr<ByteSource> open_lz4_decompressed(std::unique_ptr<ByteSource> src);

} // namespace brokkr::io
