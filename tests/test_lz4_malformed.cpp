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

// LZ4 frames are read from firmware files, so the frame header is untrusted.
// These tests feed malformed headers to parse_lz4_frame_header and check that
// each one is rejected before any block is read.

#include "io/lz4_frame.hpp"
#include "io/source.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

static void check(const char* label, bool ok) {
  if (ok) {
    ++g_pass;
  } else {
    std::fprintf(stderr, "FAIL %s\n", label);
    ++g_fail;
  }
}

// An in-memory ByteSource, enough for parse_lz4_frame_header to read from.
class MemorySource final : public brokkr::io::ByteSource {
 public:
  explicit MemorySource(std::vector<std::byte> data) : data_(std::move(data)) {}

  std::string display_name() const override { return "memory"; }
  std::uint64_t size() const override { return data_.size(); }

  std::size_t read(std::span<std::byte> out) override {
    const std::size_t n = std::min(out.size(), data_.size() - pos_);
    std::memcpy(out.data(), data_.data() + pos_, n);
    pos_ += n;
    return n;
  }

 private:
  std::vector<std::byte> data_;
  std::size_t pos_ = 0;
};

// Standard LZ4 frame magic, little-endian 0x184D2204.
static std::vector<std::byte> magic_bytes() {
  return {std::byte{0x04}, std::byte{0x22}, std::byte{0x4D}, std::byte{0x18}};
}

static bool parse_ok(std::vector<std::byte> bytes) {
  MemorySource src(std::move(bytes));
  auto r = brokkr::io::parse_lz4_frame_header(src);
  return static_cast<bool>(r);
}

static void test_bad_magic() {
  std::vector<std::byte> bytes{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
                               std::byte{0x60}, std::byte{0x40}, std::byte{0}};
  check("bad_magic_rejected", !parse_ok(bytes));
}

static void test_truncated_header() {
  // Magic only: the flags and block descriptor are missing.
  check("truncated_header_rejected", !parse_ok(magic_bytes()));
}

static void test_unsupported_version() {
  auto bytes = magic_bytes();
  bytes.push_back(std::byte{0x00});  // FLG: version bits 00, not 01
  bytes.push_back(std::byte{0x40});  // BD: 64 KiB blocks
  bytes.insert(bytes.end(), 9, std::byte{0});
  check("unsupported_version_rejected", !parse_ok(bytes));
}

static void test_missing_content_size() {
  auto bytes = magic_bytes();
  bytes.push_back(std::byte{0x60});  // FLG: version 01, independent blocks, no content size
  bytes.push_back(std::byte{0x40});  // BD: 64 KiB blocks
  bytes.insert(bytes.end(), 9, std::byte{0});
  check("missing_content_size_rejected", !parse_ok(bytes));
}

static void test_invalid_block_descriptor() {
  auto bytes = magic_bytes();
  bytes.push_back(std::byte{0x68});  // FLG: version 01, independent, content size present
  bytes.push_back(std::byte{0x00});  // BD: block size id 0 is not defined
  bytes.insert(bytes.end(), 9, std::byte{0});
  check("invalid_block_descriptor_rejected", !parse_ok(bytes));
}

int main() {
  test_bad_magic();
  test_truncated_header();
  test_unsupported_version();
  test_missing_content_size();
  test_invalid_block_descriptor();

  std::fprintf(stdout, "lz4_malformed: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
