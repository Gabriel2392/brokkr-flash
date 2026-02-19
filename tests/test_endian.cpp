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

#include "core/endian.hpp"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>

static int g_pass = 0;
static int g_fail = 0;

template <class T>
static void check_eq(const char* label, T got, T expected) {
  if (got == expected) {
    ++g_pass;
  } else {
    std::fprintf(stderr, "FAIL %s\n", label);
    ++g_fail;
  }
}

// ----- round-trip: host_to_le(le_to_host(x)) == x -----

static void test_roundtrip_u16() {
  constexpr std::uint16_t v = 0xBEEF;
  auto le = brokkr::core::host_to_le(v);
  auto rt = brokkr::core::le_to_host(le);
  check_eq("roundtrip_u16", rt, v);
}

static void test_roundtrip_u32() {
  constexpr std::uint32_t v = 0xDEADBEEF;
  auto le = brokkr::core::host_to_le(v);
  auto rt = brokkr::core::le_to_host(le);
  check_eq("roundtrip_u32", rt, v);
}

static void test_roundtrip_u64() {
  constexpr std::uint64_t v = 0x0123456789ABCDEFull;
  auto le = brokkr::core::host_to_le(v);
  auto rt = brokkr::core::le_to_host(le);
  check_eq("roundtrip_u64", rt, v);
}

static void test_roundtrip_i32() {
  constexpr std::int32_t v = -12345678;
  auto le = brokkr::core::host_to_le(v);
  auto rt = brokkr::core::le_to_host(le);
  check_eq("roundtrip_i32", rt, v);
}

// ----- wire-byte verification -----

static void test_host_to_le_bytes_u32() {
  constexpr std::uint32_t v = 0x04030201;
  auto le = brokkr::core::host_to_le(v);

  unsigned char bytes[4];
  std::memcpy(bytes, &le, 4);

  // LE wire bytes must be 01 02 03 04 regardless of host endianness
  bool ok = bytes[0] == 0x01 && bytes[1] == 0x02 && bytes[2] == 0x03 && bytes[3] == 0x04;
  check_eq("host_to_le_bytes_u32", ok, true);
}

static void test_le_to_host_from_bytes_u32() {
  // Construct a known LE buffer and verify le_to_host reads it correctly
  unsigned char bytes[4] = {0x78, 0x56, 0x34, 0x12};
  std::uint32_t le_val;
  std::memcpy(&le_val, bytes, 4);

  auto host = brokkr::core::le_to_host(le_val);
  check_eq("le_to_host_from_bytes_u32", host, std::uint32_t{0x12345678});
}

static void test_le_to_host_from_bytes_u16() {
  unsigned char bytes[2] = {0x34, 0x12};
  std::uint16_t le_val;
  std::memcpy(&le_val, bytes, 2);

  auto host = brokkr::core::le_to_host(le_val);
  check_eq("le_to_host_from_bytes_u16", host, std::uint16_t{0x1234});
}

// ----- identity on zero / -1 -----

static void test_zero() {
  check_eq("zero_u32", brokkr::core::le_to_host(std::uint32_t{0}), std::uint32_t{0});
  check_eq("zero_host_to_le_u32", brokkr::core::host_to_le(std::uint32_t{0}), std::uint32_t{0});
}

static void test_all_ones() {
  constexpr std::uint32_t all = 0xFFFFFFFF;
  check_eq("all_ones_le_to_host", brokkr::core::le_to_host(all), all);
  check_eq("all_ones_host_to_le", brokkr::core::host_to_le(all), all);
}

// ----- constexpr check -----

static void test_constexpr() {
  constexpr auto v = brokkr::core::le_to_host(std::uint32_t{0x12345678});
  // Just verify it compiled as constexpr
  check_eq("constexpr_le_to_host", v, v);
}

int main() {
  test_roundtrip_u16();
  test_roundtrip_u32();
  test_roundtrip_u64();
  test_roundtrip_i32();
  test_host_to_le_bytes_u32();
  test_le_to_host_from_bytes_u32();
  test_le_to_host_from_bytes_u16();
  test_zero();
  test_all_ones();
  test_constexpr();

  std::fprintf(stdout, "endian: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
