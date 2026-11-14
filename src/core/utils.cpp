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

#include "core/utils.hpp"

#include <algorithm>
#include <cstring>

namespace brokkr::core {

std::string_view trim_ascii_ws(std::string_view s) noexcept {
  while (!s.empty() && is_ascii_ws(s.front())) s.remove_prefix(1);
  return rtrim_ascii_ws(s);
}

std::string_view rtrim_ascii_ws(std::string_view s) noexcept {
  while (!s.empty() && is_ascii_ws(s.back())) s.remove_suffix(1);
  return s;
}

std::string_view ltrim_ascii_blank(std::string_view s) noexcept {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  return s;
}

std::string extract_cstr(const char* p, std::size_t n) {
  const auto* nul = static_cast<const char*>(std::memchr(p, '\0', n));
  const std::size_t len = nul ? static_cast<std::size_t>(nul - p) : n;
  return std::string(p, len);
}

std::string trim_cstr_field(const char* p, std::size_t n) {
  const auto* nul = static_cast<const char*>(std::memchr(p, '\0', n));
  const std::size_t len = nul ? static_cast<std::size_t>(nul - p) : n;
  return std::string(rtrim_ascii_ws({p, len}));
}

std::string to_lower_ascii(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(ascii_lower(static_cast<unsigned char>(c)));
  return out;
}

std::string to_upper_ascii(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(ascii_upper(static_cast<unsigned char>(c)));
  return out;
}

bool contains_ci(std::string_view haystack, std::string_view needle) noexcept {
  const auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), [](char a, char b) {
    return ascii_lower(static_cast<unsigned char>(a)) == ascii_lower(static_cast<unsigned char>(b));
  });
  return it != haystack.end();
}

int hex_nibble(unsigned char c) noexcept {
  if (c >= '0' && c <= '9') return (c - '0');
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

bool parse_md5_hex(std::string_view hex32, std::array<unsigned char, 16>& out) noexcept {
  if (hex32.size() != out.size() * 2) return false;
  for (std::size_t i = 0; i < out.size(); ++i) {
    const int hi = hex_nibble(static_cast<unsigned char>(hex32[2 * i + 0]));
    const int lo = hex_nibble(static_cast<unsigned char>(hex32[2 * i + 1]));
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<unsigned char>((hi << 4) | lo);
  }
  return true;
}

std::string format_hex_lower(std::span<const unsigned char> data) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(data.size() * 2, '0');
  for (std::size_t i = 0; i < data.size(); ++i) {
    out[2 * i + 0] = kHex[(data[i] >> 4) & 0x0F];
    out[2 * i + 1] = kHex[data[i] & 0x0F];
  }
  return out;
}

std::optional<std::uint32_t> parse_u32_auto(std::string_view s) noexcept {
  s = ltrim_ascii_blank(rtrim_ascii_ws(s));

  int base = 10;
  if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    base = 16;
    s.remove_prefix(2);
  }

  std::uint32_t v = 0;
  if (!parse_int_exact(s, v, base)) return std::nullopt;
  return v;
}

} // namespace brokkr::core
