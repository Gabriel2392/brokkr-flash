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

// The PIT is read from the device, so every field here is untrusted input.
// These tests pin down how parse() rejects truncated, oversized and
// malformed tables, and that a well-formed table still parses.

#include "core/endian.hpp"
#include "protocol/odin/pit.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
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

static void put_i32(std::vector<std::byte>& buf, std::size_t off, std::int32_t v) {
  const std::int32_t le = brokkr::core::host_to_le(v);
  std::memcpy(buf.data() + off, &le, sizeof(le));
}

static std::vector<std::byte> make_header(std::int32_t magic, std::int32_t count, std::size_t total_size) {
  std::vector<std::byte> buf(total_size, std::byte{0});
  put_i32(buf, 0, magic);
  put_i32(buf, 4, count);
  return buf;
}

static void test_empty_buffer() {
  std::vector<std::byte> buf;
  auto r = brokkr::odin::pit::parse(buf);
  check("empty_buffer_rejected", !r);
}

static void test_short_header() {
  auto buf = make_header(brokkr::odin::pit::PIT_MAGIC, 0, sizeof(brokkr::odin::pit::PitHeaderWire) - 1);
  auto r = brokkr::odin::pit::parse(buf);
  check("short_header_rejected", !r);
}

static void test_bad_magic() {
  auto buf = make_header(0x0BADF00D, 0, sizeof(brokkr::odin::pit::PitHeaderWire));
  auto r = brokkr::odin::pit::parse(buf);
  check("bad_magic_rejected", !r);
}

static void test_negative_count() {
  auto buf = make_header(brokkr::odin::pit::PIT_MAGIC, -1, sizeof(brokkr::odin::pit::PitHeaderWire));
  auto r = brokkr::odin::pit::parse(buf);
  check("negative_count_rejected", !r);
}

static void test_count_past_end_of_buffer() {
  auto buf = make_header(brokkr::odin::pit::PIT_MAGIC, 1000, sizeof(brokkr::odin::pit::PitHeaderWire));
  auto r = brokkr::odin::pit::parse(buf);
  check("count_past_end_rejected", !r);
}

static void test_huge_count_small_buffer() {
  // Must fail on the size check, before reserving space for INT32_MAX entries.
  auto buf = make_header(brokkr::odin::pit::PIT_MAGIC, std::numeric_limits<std::int32_t>::max(),
                         sizeof(brokkr::odin::pit::PitHeaderWire));
  auto r = brokkr::odin::pit::parse(buf);
  check("huge_count_small_buffer_rejected", !r);
}

static void test_one_valid_entry() {
  using brokkr::odin::pit::PartitionInfoWire;
  using brokkr::odin::pit::PitHeaderWire;

  const std::size_t total = sizeof(PitHeaderWire) + sizeof(PartitionInfoWire);
  auto buf = make_header(brokkr::odin::pit::PIT_MAGIC, 1, total);

  const std::size_t entry = sizeof(PitHeaderWire);
  put_i32(buf, entry + 0, 0);      // binType
  put_i32(buf, entry + 4, 0);      // devType
  put_i32(buf, entry + 8, 7);      // id
  put_i32(buf, entry + 12, 0);     // attribute
  put_i32(buf, entry + 16, 0);     // updateAttribute
  put_i32(buf, entry + 20, 512);   // blockSize
  put_i32(buf, entry + 24, 16);    // blockLength
  put_i32(buf, entry + 28, 100);   // offset
  put_i32(buf, entry + 32, 4096);  // fileSize
  const char name[] = "system";
  std::memcpy(buf.data() + entry + 36, name, sizeof(name) - 1);

  auto r = brokkr::odin::pit::parse(buf);
  check("valid_entry_parses", static_cast<bool>(r));
  if (r) {
    check("valid_entry_count", r->partitions.size() == 1);
    if (!r->partitions.empty()) {
      check("valid_entry_id", r->partitions[0].id == 7);
      check("valid_entry_name", r->partitions[0].name == "system");
    }
  }
}

int main() {
  test_empty_buffer();
  test_short_header();
  test_bad_magic();
  test_negative_count();
  test_count_past_end_of_buffer();
  test_huge_count_small_buffer();
  test_one_valid_entry();

  std::fprintf(stdout, "pit_parse: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
