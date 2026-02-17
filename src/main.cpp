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

#include <cstdlib>

#include <spdlog/spdlog.h>

int main(int argc, char** argv) {
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

  auto orr = brokkr::app::parse_cli(argc, argv);
  if (!orr) {
    spdlog::error("{}", orr.st.msg);
    return EXIT_FAILURE;
  }

  auto opt = std::move(orr.value);

  if (opt.gui_mode) spdlog::set_pattern("%H:%M:%S %v");

  if (opt._no_args) {
    spdlog::error(opt.gui_mode ? "Please give some inputs to perform actions." : "No arguments provided. Use --help to see usage.");
    return EXIT_FAILURE;
  }

  if (opt.help) {
    spdlog::info(brokkr::app::usage_text());
    return EXIT_SUCCESS;
  }

  if (opt.version) {
    spdlog::info("Brokkr Flash v{}", brokkr::app::version_string());
    return EXIT_SUCCESS;
  }

  const auto ret = opt.wireless ? brokkr::app::run_wireless(opt) : brokkr::app::run(opt);
  return ret == brokkr::app::RunResult::Success ? EXIT_SUCCESS : static_cast<int>(ret);
}
