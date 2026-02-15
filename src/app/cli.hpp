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

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace brokkr::app {

struct Options {
    bool help = false;
    bool version = false;
    bool print_connected = false;
    bool print_pit = false;
    std::optional<std::filesystem::path> pit_print_in;

    bool wireless = false; // Send bytes over tcp. Just listen on :13579 for connection. Protocol is exactly the same.

    bool reboot_only = false;
    bool redownload = false;

    std::optional<std::string> target_sysname;

    std::optional<std::filesystem::path> pit_get_out;
    std::optional<std::filesystem::path> pit_set_in;

    bool reboot_after_flash = true;

    std::optional<std::filesystem::path> file_a;
    std::optional<std::filesystem::path> file_b;
    std::optional<std::filesystem::path> file_c;
    std::optional<std::filesystem::path> file_s;
    std::optional<std::filesystem::path> file_u;
};

Options parse_cli(int argc, char** argv);
std::string usage_text();

} // namespace brokkr::app
