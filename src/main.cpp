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

#include "app/cli.hpp"
#include "app/run.hpp"
#include "app/version.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    try {
        auto opt = brokkr::app::parse_cli(argc, argv);

        if (opt.help) {
            std::cout << brokkr::app::usage_text();
            return EXIT_SUCCESS;
        }
        if (opt.version) {
            std::cout << "Brokkr Flash v" << brokkr::app::version_string() << "\n";
            return EXIT_SUCCESS;
        }

        return brokkr::app::run(opt);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
