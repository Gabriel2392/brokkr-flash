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

// Firmware archives are untrusted input. These tests build small tar files
// by hand and check that TarArchive::open rejects a bad checksum, a payload
// cut short, and an oversized GNU long-name header, and accepts a valid entry.

#include "io/tar.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
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

using Header = std::array<char, 512>;

static void put_str(Header& h, std::size_t off, const std::string& s, std::size_t max) {
  std::memcpy(h.data() + off, s.data(), std::min(s.size(), max));
}

// Writes an octal field of `len` bytes: len-1 digits and a NUL.
static void put_octal(Header& h, std::size_t off, std::size_t len, std::uint64_t v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%0*llo", static_cast<int>(len - 1), static_cast<unsigned long long>(v));
  std::memcpy(h.data() + off, buf, len - 1);
  h[off + len - 1] = '\0';
}

static void finish_checksum(Header& h) {
  std::memset(h.data() + 148, ' ', 8);
  unsigned sum = 0;
  for (char c : h) sum += static_cast<unsigned char>(c);
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%06o", sum);
  std::memcpy(h.data() + 148, buf, 6);
  h[154] = '\0';
  h[155] = ' ';
}

static Header make_header(const std::string& name, std::uint64_t size, char typeflag) {
  Header h{};
  put_str(h, 0, name, 100);
  put_octal(h, 100, 8, 0644);
  put_octal(h, 108, 8, 0);
  put_octal(h, 116, 8, 0);
  put_octal(h, 124, 12, size);
  put_octal(h, 136, 12, 0);
  h[156] = typeflag;
  std::memcpy(h.data() + 257, "ustar", 6);
  std::memcpy(h.data() + 263, "00", 2);
  finish_checksum(h);
  return h;
}

static std::filesystem::path write_archive(const char* tag, const std::vector<char>& bytes) {
  auto path = std::filesystem::temp_directory_path() / (std::string("brokkr_tar_") + tag + ".tar");
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return path;
}

static void append(std::vector<char>& out, const Header& h) {
  out.insert(out.end(), h.begin(), h.end());
}

static void append_padded(std::vector<char>& out, const std::string& payload) {
  out.insert(out.end(), payload.begin(), payload.end());
  const std::size_t pad = (512 - payload.size() % 512) % 512;
  out.insert(out.end(), pad, '\0');
}

static void append_end_of_archive(std::vector<char>& out) { out.insert(out.end(), 1024, '\0'); }

static void test_valid_entry() {
  std::vector<char> bytes;
  append(bytes, make_header("system.img", 4, '0'));
  append_padded(bytes, "ABCD");
  append_end_of_archive(bytes);
  auto path = write_archive("valid", bytes);

  auto r = brokkr::io::TarArchive::open(path);
  check("valid_tar_opens", static_cast<bool>(r));
  if (r) {
    auto e = r->find_by_basename("system.img");
    check("valid_tar_entry_found", e.has_value());
    if (e) check("valid_tar_entry_size", e->size == 4);
  }
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

static void test_bad_checksum() {
  auto h = make_header("system.img", 4, '0');
  h[0] = 'X';  // change a name byte after the checksum was computed
  std::vector<char> bytes;
  append(bytes, h);
  append_padded(bytes, "ABCD");
  append_end_of_archive(bytes);
  auto path = write_archive("badsum", bytes);

  auto r = brokkr::io::TarArchive::open(path);
  check("bad_checksum_rejected", !r);
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

static void test_truncated_payload() {
  std::vector<char> bytes;
  append(bytes, make_header("system.img", 1000, '0'));  // claims 1000 bytes
  append_padded(bytes, std::string(10, 'x'));            // only 10 are there
  auto path = write_archive("truncated", bytes);

  auto r = brokkr::io::TarArchive::open(path);
  check("truncated_payload_rejected", !r);
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

static void test_huge_gnu_longname() {
  std::vector<char> bytes;
  // GNU long-name entry claiming 9 MiB, above the 8 MiB cap.
  append(bytes, make_header("././@LongLink", 9ull * 1024 * 1024, 'L'));
  append_end_of_archive(bytes);
  auto path = write_archive("longname", bytes);

  auto r = brokkr::io::TarArchive::open(path);
  check("huge_longname_rejected", !r);
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

int main() {
  test_valid_entry();
  test_bad_checksum();
  test_truncated_payload();
  test_huge_gnu_longname();

  std::fprintf(stdout, "tar_malformed: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
