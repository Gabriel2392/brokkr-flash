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

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fmt {

template <typename... Args>
using format_string = std::string_view;

namespace detail {

struct FormatSpec {
  bool zero_pad = false;
  int width = 0;
  char presentation = 0;
};

inline FormatSpec parse_spec(std::string_view spec) {
  FormatSpec parsed;

  if (!spec.empty() && spec.front() == ':') {
    spec.remove_prefix(1);
  }

  if (!spec.empty() && spec.front() == '0') {
    parsed.zero_pad = true;
    spec.remove_prefix(1);
  }

  while (!spec.empty() && std::isdigit(static_cast<unsigned char>(spec.front())) != 0) {
    parsed.width = (parsed.width * 10) + (spec.front() - '0');
    spec.remove_prefix(1);
  }

  if (!spec.empty()) {
    parsed.presentation = spec.front();
  }

  return parsed;
}

inline std::string pad_string(std::string value, const FormatSpec& spec) {
  if (spec.width > 0 && static_cast<int>(value.size()) < spec.width) {
    value.insert(value.begin(), spec.width - static_cast<int>(value.size()), spec.zero_pad ? '0' : ' ');
  }
  return value;
}

template <typename Integer>
std::string format_integer(Integer value, const FormatSpec& spec) {
  std::ostringstream stream;
  if (spec.width > 0) {
    stream << std::setw(spec.width) << std::setfill(spec.zero_pad ? '0' : ' ');
  }

  switch (spec.presentation) {
    case 'x':
    case 'X':
      if (spec.presentation == 'X') {
        stream << std::uppercase;
      }
      stream << std::hex;
      if constexpr (std::is_signed_v<Integer>) {
        using Unsigned = std::make_unsigned_t<Integer>;
        stream << static_cast<Unsigned>(value);
      } else {
        stream << value;
      }
      break;
    default:
      stream << value;
      break;
  }

  return stream.str();
}

inline std::string format_value(const std::string& value, const FormatSpec& spec) { return pad_string(value, spec); }

inline std::string format_value(std::string_view value, const FormatSpec& spec) {
  return pad_string(std::string(value), spec);
}

inline std::string format_value(const char* value, const FormatSpec& spec) {
  return pad_string(value != nullptr ? std::string(value) : std::string("(null)"), spec);
}

inline std::string format_value(char value, const FormatSpec& spec) {
  return pad_string(std::string(1, value), spec);
}

inline std::string format_value(bool value, const FormatSpec& spec) {
  return pad_string(value ? std::string("true") : std::string("false"), spec);
}

inline std::string format_value(std::byte value, const FormatSpec& spec) {
  return format_integer(std::to_integer<unsigned int>(value), spec);
}

inline std::string format_value(const std::filesystem::path& value, const FormatSpec& spec) {
  return pad_string(value.string(), spec);
}

template <typename T>
std::string format_value(const T& value, const FormatSpec& spec) {
  if constexpr (std::is_same_v<std::remove_cv_t<T>, signed char> ||
                std::is_same_v<std::remove_cv_t<T>, unsigned char>) {
    return format_integer(static_cast<int>(value), spec);
  } else if constexpr (std::is_enum_v<T>) {
    return format_integer(static_cast<std::underlying_type_t<T>>(value), spec);
  } else if constexpr (std::is_integral_v<T>) {
    return format_integer(value, spec);
  } else {
    std::ostringstream stream;
    stream << value;
    return pad_string(stream.str(), spec);
  }
}

template <typename Tuple, std::size_t... Indexes>
std::string format_arg_at(const Tuple& args, std::size_t index, std::string_view spec,
                          std::index_sequence<Indexes...>) {
  std::string formatted = "{}";
  const auto parsed = parse_spec(spec);

  (void)((index == Indexes ? (formatted = format_value(std::get<Indexes>(args), parsed), true) : false) || ...);

  return formatted;
}

} // namespace detail

template <typename... Args>
std::string format(std::string_view pattern, Args&&... args) {
  const auto packed_args = std::forward_as_tuple(std::forward<Args>(args)...);

  std::string out;
  out.reserve(pattern.size() + sizeof...(Args) * 8);

  std::size_t arg_index = 0;
  for (std::size_t i = 0; i < pattern.size(); ++i) {
    const char current = pattern[i];
    if (current == '{') {
      if ((i + 1) < pattern.size() && pattern[i + 1] == '{') {
        out.push_back('{');
        ++i;
        continue;
      }

      const auto close = pattern.find('}', i + 1);
      if (close == std::string_view::npos) {
        out.append(pattern.substr(i));
        break;
      }

      out += detail::format_arg_at(packed_args, arg_index++, pattern.substr(i + 1, close - i - 1),
                                   std::index_sequence_for<Args...>{});
      i = close;
      continue;
    }

    if (current == '}' && (i + 1) < pattern.size() && pattern[i + 1] == '}') {
      out.push_back('}');
      ++i;
      continue;
    }

    out.push_back(current);
  }

  return out;
}

template <typename Iterator>
std::string join(Iterator begin, Iterator end, std::string_view separator) {
  std::string out;
  bool first = true;
  for (auto it = begin; it != end; ++it) {
    if (!first) {
      out.append(separator);
    }
    first = false;
    out.append(detail::format_value(*it, detail::FormatSpec{}));
  }
  return out;
}

template <typename Range>
std::string join(const Range& range, std::string_view separator) {
  return join(std::begin(range), std::end(range), separator);
}

} // namespace fmt