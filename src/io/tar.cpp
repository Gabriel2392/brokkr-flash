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

#include "io/tar.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <spdlog/spdlog.h>

namespace brokkr::io {

namespace {

constexpr std::size_t kBlock = 512;

inline std::uint64_t round_up_512(std::uint64_t n) {
  return (n + (kBlock - 1)) & ~(static_cast<std::uint64_t>(kBlock - 1));
}

inline std::uint64_t parse_u64_dec(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    s.remove_prefix(1);
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' ||
                        s.back() == ' ' || s.back() == '\t'))
    s.remove_suffix(1);

  std::uint64_t v = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v, 10);
  if (ec != std::errc{})
    throw std::runtime_error("PAX: invalid decimal number");
  if (ptr != s.data() + s.size())
    throw std::runtime_error("PAX: trailing junk in decimal number");
  return v;
}

} // namespace

TarArchive::TarArchive(std::string path, bool validate_header_checksums)
    : path_(std::move(path)), validate_(validate_header_checksums) {
  if (!scan_()) {
    throw std::runtime_error("TarArchive: failed to read archive");
  }
}

bool TarArchive::is_tar_file(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open())
    return false;

  std::array<std::byte, 512> header{};
  in.read(reinterpret_cast<char *>(header.data()), header.size());
  if (!in.good())
    return false;

  if (header_all_zero(std::span<const std::byte, 512>(header)))
    return false;
  return validate_header_checksum(std::span<const std::byte, 512>(header));
}

std::optional<TarEntry>
TarArchive::find_by_basename(std::string_view base) const {
  for (const auto &e : entries_) {
    if (std::filesystem::path(e.name).filename() == base)
      return e;
  }
  return std::nullopt;
}

bool TarArchive::header_all_zero(std::span<const std::byte, 512> header) {
  for (auto b : header) {
    if (b != std::byte{0})
      return false;
  }
  return true;
}

std::string TarArchive::trim_cstr_field(const char *p, std::size_t n) {
  const auto *nul = static_cast<const char *>(std::memchr(p, '\0', n));
  const std::size_t len = nul ? static_cast<std::size_t>(nul - p) : n;

  std::size_t end = len;
  while (end > 0 && (p[end - 1] == ' ' || p[end - 1] == '\t' ||
                     p[end - 1] == '\r' || p[end - 1] == '\n'))
    --end;
  return std::string(p, p + end);
}

std::uint64_t TarArchive::parse_octal(std::string_view s) {
  while (!s.empty() &&
         (s.front() == ' ' || s.front() == '\t' || s.front() == '\0'))
    s.remove_prefix(1);
  while (!s.empty() &&
         (s.back() == ' ' || s.back() == '\t' || s.back() == '\0' ||
          s.back() == '\r' || s.back() == '\n'))
    s.remove_suffix(1);

  std::uint64_t v = 0;
  for (char ch : s) {
    if (ch < '0' || ch > '7')
      break;
    v = (v << 3) + static_cast<std::uint64_t>(ch - '0');
  }
  return v;
}

std::uint64_t TarArchive::parse_tar_number(const char *p, std::size_t n) {
  if (n == 0)
    return 0;

  const unsigned char b0 = static_cast<unsigned char>(p[0]);

  // bit7 set means base-256
  if (b0 & 0x80) {
    const bool negative = (b0 & 0x40) != 0;
    if (negative)
      throw std::runtime_error("Tar: negative base-256 numeric field");

    std::uint64_t val = static_cast<std::uint64_t>(b0 & 0x3Fu);
    for (std::size_t i = 1; i < n; ++i) {
      if (val > (std::numeric_limits<std::uint64_t>::max() >> 8)) {
        throw std::runtime_error(
            "Tar: base-256 numeric field too large for uint64");
      }
      val = (val << 8) | static_cast<unsigned char>(p[i]);
    }
    return val;
  }

  return parse_octal(std::string_view(p, n));
}

std::string TarArchive::join_ustar_name(std::string_view prefix,
                                        std::string_view name) {
  if (prefix.empty())
    return std::string(name);
  std::string out;
  out.reserve(prefix.size() + 1 + name.size());
  out.append(prefix);
  if (!out.empty() && out.back() != '/')
    out.push_back('/');
  out.append(name);
  return out;
}

bool TarArchive::validate_header_checksum(
    std::span<const std::byte, 512> header) {
  constexpr std::size_t chk_off = 148;
  constexpr std::size_t chk_len = 8;

  const char *chk_field =
      reinterpret_cast<const char *>(header.data() + chk_off);
  const auto expected = static_cast<unsigned long>(
      parse_octal(std::string_view(chk_field, chk_len)));

  auto compute = [&](bool signed_mode) -> unsigned long {
    long sum = 0;
    for (std::size_t i = 0; i < header.size(); ++i) {
      unsigned char c = static_cast<unsigned char>(header[i]);
      if (i >= chk_off && i < chk_off + chk_len)
        c = 0x20;

      if (signed_mode)
        sum += static_cast<signed char>(c);
      else
        sum += static_cast<unsigned char>(c);
    }
    return static_cast<unsigned long>(sum);
  };

  const auto u = compute(false);
  const auto s = compute(true);
  return expected == u || expected == s;
}

TarArchive::PaxKV TarArchive::parse_pax_payload(std::string_view payload) {
  PaxKV kv;

  std::size_t pos = 0;
  while (pos < payload.size()) {
    const auto sp = payload.find(' ', pos);
    if (sp == std::string_view::npos)
      break;

    const auto len_str = payload.substr(pos, sp - pos);
    const std::uint64_t rec_len = parse_u64_dec(len_str);
    if (rec_len == 0)
      break;
    if (pos + rec_len > payload.size())
      break;

    const auto rec = payload.substr(pos, static_cast<std::size_t>(rec_len));
    pos += static_cast<std::size_t>(rec_len);

    const auto sp2 = rec.find(' ');
    if (sp2 == std::string_view::npos)
      continue;
    std::string_view kvs = rec.substr(sp2 + 1);
    if (!kvs.empty() && kvs.back() == '\n')
      kvs.remove_suffix(1);

    const auto eq = kvs.find('=');
    if (eq == std::string_view::npos)
      continue;

    const auto key = kvs.substr(0, eq);
    const auto val = kvs.substr(eq + 1);

    if (key == "path") {
      kv.path = std::string(val);
    } else if (key == "size") {
      kv.size = parse_u64_dec(val);
    }
  }

  return kv;
}

bool TarArchive::scan_() {
  std::ifstream in(path_, std::ios::binary);
  if (!in.is_open()) {
    spdlog::error("TarArchive: cannot open file: {}", path_);
    return false;
  }

  entries_.clear();
  payload_size_bytes_.reset();

  std::uint64_t pos = 0;
  std::array<std::byte, 512> header{};

  PaxKV pax_global;
  PaxKV pax_next;

  std::string gnu_longname_next;
  bool have_gnu_longname_next = false;

  std::unordered_map<std::string, TarEntry> payload_by_name;

  struct PendingHardlink {
    std::string name;
    std::string target;
  };
  std::vector<PendingHardlink> pending_hardlinks;

  auto read_exact = [&](std::byte *dst, std::size_t n) {
    in.read(reinterpret_cast<char *>(dst), static_cast<std::streamsize>(n));
    if (!in.good()) {
      throw std::runtime_error("TarArchive: read failed: " + path_);
    }
    pos += n;
  };

  auto skip_exact = [&](std::uint64_t n) {
    if (n == 0)
      return true;

    if (n > static_cast<std::uint64_t>(
                std::numeric_limits<std::streamoff>::max())) {
      spdlog::error(
          "TarArchive: cannot skip huge amount of data: {} bytes in {}", n,
          path_);
      return false;
    }
    in.seekg(static_cast<std::streamoff>(n), std::ios::cur);
    if (!in.good()) {
      spdlog::error("TarArchive: seek failed while skipping data in {}: tried "
                    "to skip {} bytes",
                    path_, n);
      return false;
    }
    pos += n;
    return true;
  };

  auto apply_name_overrides = [&](std::string full_name,
                                  std::uint64_t &size) -> std::string {
    if (have_gnu_longname_next) {
      full_name = std::move(gnu_longname_next);
      gnu_longname_next.clear();
      have_gnu_longname_next = false;
    }

    PaxKV eff = pax_global;
    eff.merge_from(pax_next);
    pax_next.clear();

    if (eff.path)
      full_name = *eff.path;
    if (eff.size)
      size = *eff.size;

    return full_name;
  };

  for (;;) {
    read_exact(header.data(), header.size());

    if (header_all_zero(std::span<const std::byte, 512>(header))) {
      std::array<std::byte, 512> hdr2{};
      in.read(reinterpret_cast<char *>(hdr2.data()),
              static_cast<std::streamsize>(hdr2.size()));
      const auto got2 = in.gcount();

      if (got2 == static_cast<std::streamsize>(hdr2.size()) &&
          header_all_zero(std::span<const std::byte, 512>(hdr2))) {
        pos += hdr2.size();
        payload_size_bytes_ = pos;
      }
      break;
    }

    if (validate_ &&
        !validate_header_checksum(std::span<const std::byte, 512>(header))) {
      spdlog::error("TarArchive: invalid header checksum in file: {}", path_);
      return false;
    }

    const char *h = reinterpret_cast<const char *>(header.data());

    const std::string name = trim_cstr_field(h + 0, 100);
    const std::string prefix = trim_cstr_field(h + 345, 155);
    const char typeflag = h[156];

    std::uint64_t size = parse_tar_number(h + 124, 12);

    if (typeflag == 'x' || typeflag == 'g') {
      if (size > (1024ull * 1024ull * 8ull)) {
        spdlog::error("TarArchive: refusing huge PAX header: {} bytes in {}",
                      size, path_);
      }

      std::string payload;
      payload.resize(static_cast<std::size_t>(size));
      if (size)
        read_exact(reinterpret_cast<std::byte *>(payload.data()),
                   static_cast<std::size_t>(size));
      if (!skip_exact(round_up_512(size) - size)) {
        spdlog::error("TarArchive: failed to skip padding after PAX header in "
                      "{}: tried to skip {} bytes",
                      path_, round_up_512(size) - size);
      }

      PaxKV kv = parse_pax_payload(payload);
      if (typeflag == 'g')
        pax_global.merge_from(kv);
      else
        pax_next.merge_from(kv);
      continue;
    }

    if (typeflag == 'L') {
      if (size > (1024ull * 1024ull * 8ull)) {
        spdlog::error(
            "TarArchive: refusing huge GNU longname header: {} bytes in {}",
            size, path_);
      }

      std::string payload;
      payload.resize(static_cast<std::size_t>(size));
      if (size)
        read_exact(reinterpret_cast<std::byte *>(payload.data()),
                   static_cast<std::size_t>(size));
      if (!skip_exact(round_up_512(size) - size)) {
        spdlog::error("TarArchive: failed to skip padding after GNU longname "
                      "header in {}: tried to skip {} bytes",
                      path_, round_up_512(size) - size);
      }

      const auto nul = payload.find('\0');
      if (nul != std::string::npos)
        payload.resize(nul);

      have_gnu_longname_next = !payload.empty();
      gnu_longname_next = std::move(payload);
      continue;
    }

    std::string full_name = join_ustar_name(prefix, name);
    full_name = apply_name_overrides(std::move(full_name), size);

    const std::uint64_t data_offset = pos;

    const bool is_payload =
        (typeflag == '0' || typeflag == '\0' || typeflag == '7');

    if (is_payload && !full_name.empty()) {
      TarEntry e{full_name, size, data_offset};
      entries_.push_back(e);
      payload_by_name.emplace(e.name, e);
    } else if (typeflag == '1') {
      std::string target = trim_cstr_field(h + 157, 100);
      if (!full_name.empty() && !target.empty()) {
        pending_hardlinks.push_back(PendingHardlink{full_name, target});
      }
    }

    if (!skip_exact(round_up_512(size))) {
      spdlog::error(
          "TarArchive: failed to skip file data in {}: tried to skip {} bytes",
          path_, round_up_512(size));
    }
  }

  for (const auto &hl : pending_hardlinks) {
    auto it = payload_by_name.find(hl.target);
    if (it == payload_by_name.end())
      continue;

    TarEntry e;
    e.name = hl.name;
    e.size = it->second.size;
    e.data_offset = it->second.data_offset;

    entries_.push_back(std::move(e));
  }
  return true;
}

} // namespace brokkr::io
