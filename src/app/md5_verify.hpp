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

#include "app/interface.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace brokkr::app {

struct Md5Job {
  std::filesystem::path path;
  std::uint64_t bytes_to_hash = 0;
  std::array<unsigned char, 16> expected{};
};

std::vector<Md5Job> md5_jobs(const std::vector<std::filesystem::path> &inputs);
bool md5_verify(const std::vector<Md5Job> &jobs, FlashInterface &ui);

} // namespace brokkr::app
