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

#include "app/download_mode.hpp"

#include "app/samsung_usb.hpp"
#include "core/status.hpp"
#include "core/thread_pool.hpp"
#include "platform/serial_port.hpp"

#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace brokkr::app {

namespace {

constexpr std::string_view kDownloadModeCmd = "AT+SUDDLMOD=0,0\r";

std::vector<std::string> collect_serial_nodes() {
  std::set<std::string> unique;
  for (const auto& d : enumerate_samsung_targets()) {
    if (is_odin_product(d.product)) continue;
    unique.insert(d.serial_nodes.begin(), d.serial_nodes.end());
  }

  return {unique.begin(), unique.end()};
}

} // namespace

AtCmdResult reboot_to_download_mode() {
  AtCmdResult out;

  const auto nodes = collect_serial_nodes();
  out.ports_seen = static_cast<int>(nodes.size());
  if (nodes.empty()) {
    spdlog::debug("No Samsung serial port found for download-mode reboot");
    return out;
  }

  std::vector<brokkr::core::Status> results(nodes.size(), brokkr::core::fail("not attempted"));
  brokkr::core::ThreadPool pool(nodes.size());

  for (std::size_t i = 0; i < nodes.size(); ++i) {
    auto submitted = pool.submit([&results, &nodes, i]() -> brokkr::core::Status {
      spdlog::debug("Sending download-mode command to {}", nodes[i]);
      results[i] = brokkr::platform::write_serial_port(nodes[i], kDownloadModeCmd);
      return {};
    });
    if (!submitted) results[i] = std::move(submitted);
  }

  (void)pool.wait();

  for (std::size_t i = 0; i < nodes.size(); ++i) {
    if (results[i]) {
      ++out.sent_ok;
      continue;
    }
    out.failures.push_back(nodes[i] + ": " + results[i].error());
  }

  return out;
}

} // namespace brokkr::app
