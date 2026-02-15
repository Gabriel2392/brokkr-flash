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
#include <cstddef>
#include <string_view>

namespace brokkr::core {

constexpr unsigned char ascii_lower(unsigned char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c - 'A' + 'a') : c;
}

constexpr bool ends_with_ci(std::string_view s, std::string_view suf) noexcept {
    if (s.size() < suf.size()) return false;
    const std::size_t off = s.size() - suf.size();
    for (std::size_t i = 0; i < suf.size(); ++i)
        if (ascii_lower(static_cast<unsigned char>(s[off + i])) != ascii_lower(static_cast<unsigned char>(suf[i])))
            return false;
    return true;
}

} // namespace brokkr::core
