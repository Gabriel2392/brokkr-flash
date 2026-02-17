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

#include <spdlog/spdlog.h>

int main(int argc, char **argv) {
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

  try {
    auto opt = brokkr::app::parse_cli(argc, argv);
    if (!opt) {
      spdlog::error("Failed to parse command line arguments. Use --help to see usage.");
      return EXIT_FAILURE;
	}

    if (opt->gui_mode) {
		// Change log pattern to exclude some information and making it compact.
        // This is because in GUI mode, the logs are meant to be parsed by brokkr-gui and shown to the user 
        // in a more user-friendly way, so we want to avoid redundant information like log levels and date.
		spdlog::set_pattern("%H:%M:%S %v");
    }
    if (opt->_no_args) {
        if (opt->gui_mode) {
			spdlog::error("Please give some inputs to perform actions.");
        } else {
		    spdlog::error("No arguments provided. Use --help to see usage.");
        }
		return EXIT_FAILURE;
    }
    if (opt->help) {
      spdlog::info(brokkr::app::usage_text());
      return EXIT_SUCCESS;
    }
    if (opt->version) {
      spdlog::info("Brokkr Flash v{}", brokkr::app::version_string());
      return EXIT_SUCCESS;
    }

	auto ret = opt->wireless ? brokkr::app::run_wireless(*opt) : brokkr::app::run(*opt);
	return ret == brokkr::app::RunResult::Success ? EXIT_SUCCESS : static_cast<int>(ret);
  } catch (const std::exception &e) {
    spdlog::error(e.what());
    return EXIT_FAILURE;
  }
}
