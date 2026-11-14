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

#include "core/str.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace brokkr::core {

constexpr bool is_ascii_ws(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

constexpr unsigned char ascii_upper(unsigned char c) noexcept {
  return (c >= 'a' && c <= 'z') ? static_cast<unsigned char>(c - 'a' + 'A') : c;
}

constexpr bool is_hex_digit(unsigned char c) noexcept {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

std::string_view trim_ascii_ws(std::string_view s) noexcept;
std::string_view rtrim_ascii_ws(std::string_view s) noexcept;
std::string_view ltrim_ascii_blank(std::string_view s) noexcept;

std::string extract_cstr(const char* p, std::size_t n);
inline std::string extract_cstr(std::span<const std::int8_t> field) {
  return extract_cstr(reinterpret_cast<const char*>(field.data()), field.size());
}

std::string trim_cstr_field(const char* p, std::size_t n);
inline std::string trim_cstr_field(std::span<const std::int8_t> field) {
  return trim_cstr_field(reinterpret_cast<const char*>(field.data()), field.size());
}

std::string to_lower_ascii(std::string_view s);
std::string to_upper_ascii(std::string_view s);
bool contains_ci(std::string_view haystack, std::string_view needle) noexcept;

int hex_nibble(unsigned char c) noexcept;
bool parse_md5_hex(std::string_view hex32, std::array<unsigned char, 16>& out) noexcept;
std::string format_hex_lower(std::span<const unsigned char> data);

template <class T>
  requires std::is_integral_v<T>
inline bool parse_int_exact(std::string_view sv, T& out, int base = 10) noexcept {
  if (sv.empty()) return false;
  const char* begin = sv.data();
  const char* end = sv.data() + sv.size();
  auto [ptr, ec] = std::from_chars(begin, end, out, base);
  return ec == std::errc{} && ptr == end;
}

template <class T>
  requires std::is_integral_v<T>
inline std::optional<T> parse_int_trimmed(std::string_view sv, int base = 10) noexcept {
  sv = ltrim_ascii_blank(rtrim_ascii_ws(sv));
  T out{};
  if (!parse_int_exact(sv, out, base)) return std::nullopt;
  return out;
}

std::optional<std::uint32_t> parse_u32_auto(std::string_view s) noexcept;

constexpr std::uint32_t read_u32_le(std::span<const std::byte, 4> b) noexcept {
  const auto u0 = static_cast<std::uint32_t>(static_cast<unsigned char>(b[0]));
  const auto u1 = static_cast<std::uint32_t>(static_cast<unsigned char>(b[1]));
  const auto u2 = static_cast<std::uint32_t>(static_cast<unsigned char>(b[2]));
  const auto u3 = static_cast<std::uint32_t>(static_cast<unsigned char>(b[3]));
  return u0 | (u1 << 8) | (u2 << 16) | (u3 << 24);
}

constexpr std::uint64_t read_u64_le(std::span<const std::byte, 8> b) noexcept {
  std::uint64_t v = 0;
  for (std::size_t i = 0; i < 8; ++i) {
    v |= (static_cast<std::uint64_t>(static_cast<unsigned char>(b[i])) << (8u * i));
  }
  return v;
}

constexpr std::uint64_t round_up_u64(std::uint64_t n, std::uint64_t align) noexcept {
  if (align == 0) return n;
  const auto r = n % align;
  return r ? (n + (align - r)) : n;
}

template <class T>
inline bool matches_filter_or_all(std::span<const T> allowed, const T& value) noexcept {
  return allowed.empty() || std::find(allowed.begin(), allowed.end(), value) != allowed.end();
}

constexpr std::size_t hash_combine(std::size_t seed, std::size_t value) noexcept {
  return static_cast<std::size_t>(seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2)));
}

} // namespace brokkr::core
