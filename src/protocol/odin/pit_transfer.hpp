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

#include "protocol/odin/odin_cmd.hpp"
#include "protocol/odin/pit.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace brokkr::odin {

std::vector<std::byte> download_pit_bytes(OdinCommands &odin,
                                          unsigned retries = 8);
pit::PitTable download_pit_table(OdinCommands &odin, unsigned retries = 8);

} // namespace brokkr::odin
