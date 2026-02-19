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

#include "third_party/md5/md5.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

static std::string hex(const unsigned char hash[16]) {
  static constexpr char lut[] = "0123456789abcdef";
  std::string out(32, '\0');
  for (int i = 0; i < 16; ++i) {
    out[2 * i + 0] = lut[(hash[i] >> 4) & 0x0F];
    out[2 * i + 1] = lut[hash[i] & 0x0F];
  }
  return out;
}

static std::string md5_str(std::string_view input) {
  MD5_CTX ctx{};
  md5_init(&ctx);
  md5_update(&ctx, reinterpret_cast<const MD5_BYTE*>(input.data()), input.size());
  unsigned char hash[16]{};
  md5_final(&ctx, hash);
  return hex(hash);
}

static int g_pass = 0;
static int g_fail = 0;

static void check(const char* label, std::string_view input, const char* expected) {
  auto got = md5_str(input);
  if (got == expected) {
    ++g_pass;
  } else {
    std::fprintf(stderr, "FAIL %s: expected %s, got %s\n", label, expected, got.c_str());
    ++g_fail;
  }
}

// ----- RFC 1321 § A.5 test vectors -----

static void test_rfc1321_vectors() {
  check("empty", "", "d41d8cd98f00b204e9800998ecf8427e");
  check("a", "a", "0cc175b9c0f1b6a831c399e269772661");
  check("abc", "abc", "900150983cd24fb0d6963f7d28e17f72");
  check("message digest", "message digest", "f96b697d7cb7938d525a2f31aaf161d0");
  check("a..z", "abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b");
  check("A..Za..z0..9", "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
        "d174ab98d277d9f5a5611c2c9f419d9f");
  check("numeric_repeat", "12345678901234567890123456789012345678901234567890123456789012345678901234567890",
        "57edf4a22be3c955ac49da2e2107b67a");
}

// ----- boundary / chunked feeding -----

static void test_single_byte_feed() {
  const char* msg = "abc";
  MD5_CTX ctx{};
  md5_init(&ctx);
  for (std::size_t i = 0; i < 3; ++i) md5_update(&ctx, reinterpret_cast<const MD5_BYTE*>(msg + i), 1);
  unsigned char hash[16]{};
  md5_final(&ctx, hash);
  auto got = hex(hash);

  if (got == "900150983cd24fb0d6963f7d28e17f72") {
    ++g_pass;
  } else {
    std::fprintf(stderr, "FAIL single_byte_feed: expected 900150983cd24fb0d6963f7d28e17f72, got %s\n", got.c_str());
    ++g_fail;
  }
}

static void test_exact_block_boundary() {
  // 64 bytes = exactly one MD5 block
  std::string block(64, 'A');
  auto got = md5_str(block);
  if (got == "d289a97565bc2d27ac8b8545a5ddba45") {
    ++g_pass;
  } else {
    std::fprintf(stderr, "FAIL exact_block(64xA): expected d289a97565bc2d27ac8b8545a5ddba45, got %s\n", got.c_str());
    ++g_fail;
  }
}

static void test_cross_block_boundary() {
  // 63 bytes then 2 bytes = crosses internal buffer boundary
  std::string part1(63, 'B');
  std::string part2(2, 'B');

  MD5_CTX ctx{};
  md5_init(&ctx);
  md5_update(&ctx, reinterpret_cast<const MD5_BYTE*>(part1.data()), part1.size());
  md5_update(&ctx, reinterpret_cast<const MD5_BYTE*>(part2.data()), part2.size());
  unsigned char hash[16]{};
  md5_final(&ctx, hash);

  // reference: md5("B" * 65)
  auto ref = md5_str(std::string(65, 'B'));
  auto got = hex(hash);

  if (got == ref) {
    ++g_pass;
  } else {
    std::fprintf(stderr, "FAIL cross_block: expected %s, got %s\n", ref.c_str(), got.c_str());
    ++g_fail;
  }
}

static void test_large_buffer() {
  // 1 MiB of zeroes — verifies bulk md5_update path
  constexpr std::size_t sz = 1024 * 1024;
  std::vector<unsigned char> buf(sz, 0);

  MD5_CTX ctx{};
  md5_init(&ctx);
  md5_update(&ctx, buf.data(), buf.size());
  unsigned char hash[16]{};
  md5_final(&ctx, hash);

  auto got = hex(hash);
  if (got == "b6d81b360a5672d80c27430f39153e2c") {
    ++g_pass;
  } else {
    std::fprintf(stderr, "FAIL large_buffer(1MiB zeroes): expected b6d81b360a5672d80c27430f39153e2c, got %s\n",
                 got.c_str());
    ++g_fail;
  }
}

static void test_padding_boundary_56() {
  // Exactly 56 bytes — boundary where padding just fits without extra block
  std::string msg(56, 'C');
  auto got = md5_str(msg);
  // pre-computed reference
  if (got == "ddeabe78031243dc616e86065dfa8161") {
    ++g_pass;
  } else {
    std::fprintf(stderr, "FAIL pad_boundary(56xC): expected ddeabe78031243dc616e86065dfa8161, got %s\n", got.c_str());
    ++g_fail;
  }
}

static void test_padding_boundary_55() {
  // 55 bytes — last size that fits in one padded block
  std::string msg(55, 'D');
  auto got = md5_str(msg);
  if (got == "42e214ea558c966c0f6cf8f3d6b6b755") {
    ++g_pass;
  } else {
    std::fprintf(stderr, "FAIL pad_boundary(55xD): expected 42e214ea558c966c0f6cf8f3d6b6b755, got %s\n", got.c_str());
    ++g_fail;
  }
}

int main() {
  test_rfc1321_vectors();
  test_single_byte_feed();
  test_exact_block_boundary();
  test_cross_block_boundary();
  test_large_buffer();
  test_padding_boundary_56();
  test_padding_boundary_55();

  std::fprintf(stdout, "md5: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
