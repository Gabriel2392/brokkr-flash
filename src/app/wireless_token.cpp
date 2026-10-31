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

#include "app/wireless_token.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace brokkr::app {

bool wireless_token_matches(brokkr::platform::TcpConnection& conn, std::string_view expected) {
  if (expected.empty()) return true;

  std::vector<std::uint8_t> got(expected.size());
  std::size_t off = 0;
  while (off < got.size()) {
    const int n = conn.recv(std::span<std::uint8_t>(got.data() + off, got.size() - off));
    if (n <= 0) return false;
    off += static_cast<std::size_t>(n);
  }

  // Compare every byte so the time taken does not show how many bytes matched.
  std::uint8_t diff = 0;
  for (std::size_t i = 0; i < got.size(); ++i)
    diff |= static_cast<std::uint8_t>(got[i] ^ static_cast<std::uint8_t>(expected[i]));
  return diff == 0;
}

} // namespace brokkr::app
