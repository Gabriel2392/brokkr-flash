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

#include "platform/platform_all.hpp"

#include <string_view>

namespace brokkr::app {

// Reads the first expected.size() bytes a wireless peer sends and compares them
// to `expected` in constant time. An empty `expected` disables the check and
// returns true. Returns false if the peer closes, times out, or sends different bytes.
bool wireless_token_matches(brokkr::platform::TcpConnection& conn, std::string_view expected);

} // namespace brokkr::app
