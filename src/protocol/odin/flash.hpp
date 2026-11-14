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

#include "core/byte_transport.hpp"
#include "core/status.hpp"
#include "platform/platform_all.hpp"
#include "core/utils.hpp"
#include "protocol/odin/cmd.hpp"
#include "protocol/odin/image.hpp"

#include <optional>
#include <string>
#include <string_view>
#include "protocol/odin/pit.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace brokkr::odin {

#if !defined(BROKKR_PLATFORM_ANDROID)
struct UsbTarget {
  std::string devnode;
  brokkr::platform::UsbFsDevice dev;
  brokkr::platform::UsbFsConnection conn;

  InitTargetInfo init{};
  ProtocolVersion proto = ProtocolVersion::PROTOCOL_NONE;

  std::vector<std::byte> pit_bytes{};
  pit::PitTable pit_table{};

  explicit UsbTarget(std::string devnode_path) : devnode(std::move(devnode_path)), dev(devnode), conn(dev) {}

  brokkr::core::Status open_and_connect(int timeout_ms) noexcept {
    BRK_TRY(dev.open_and_init());
    BRK_TRY(conn.open());
    conn.set_timeout_ms(timeout_ms);
    return {};
  }
};
#endif

struct Target {
  std::string id;
  brokkr::core::IByteTransport* link = nullptr;

  InitTargetInfo init{};
  ProtocolVersion proto = ProtocolVersion::PROTOCOL_NONE;

  std::vector<std::byte> pit_bytes{};
  pit::PitTable pit_table{};
};

struct PlanItem {
  enum class Kind { Pit, Part };
  Kind kind = Kind::Part;

  std::int32_t part_id = -1;
  std::int32_t dev_type = 0;

  std::string part_name, pit_file_name, source_base;
  std::uint64_t size = 0;
};

struct Cfg {
  std::size_t buffer_bytes = 30ull * 1024 * 1024;
  std::size_t pkt_all_v2plus = 1ull * 1024 * 1024;
  std::size_t pkt_any_old = 128ull * 1024;

  int preflash_timeout_ms = 1000;
  unsigned preflash_retries = 2;

  int flash_timeout_ms = 45'000;

  int step_delay_ms = 100;
  int post_close_delay_ms = 1000;

  bool reboot_after = true;
};

struct Ui {
  std::function<void(std::size_t, const std::vector<std::string>&)> on_devices;
  std::function<void(const std::string&)> on_model;
  std::function<void(const std::string&)> on_stage;

  std::function<void(const std::vector<PlanItem>&, std::uint64_t)> on_plan;
  std::function<void(std::size_t)> on_item_active;
  std::function<void(std::size_t)> on_item_done;

  std::function<void(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t)> on_progress;

  std::function<void(const std::string&)> on_error;
  std::function<void()> on_done;
};

struct DevFail {
  int index = -1;
  bool has_reason = false;
  std::string_view reason;
};

inline std::string format_devfail(std::size_t idx, std::string_view reason) {
  return "DEVFAIL idx=" + std::to_string(idx) + " " + std::string(reason);
}

inline std::optional<DevFail> parse_devfail(std::string_view message) noexcept {
  constexpr std::string_view kPrefix = "DEVFAIL idx=";
  if (!message.starts_with(kPrefix)) return std::nullopt;
  const auto rest = message.substr(kPrefix.size());
  const auto sp = rest.find(' ');
  const auto idx_sv = (sp == std::string_view::npos) ? rest : rest.substr(0, sp);
  int idx = -1;
  if (!brokkr::core::parse_int_exact(idx_sv, idx) || idx < 0) idx = -1;
  DevFail out;
  out.index = idx;
  if (sp != std::string_view::npos) {
    out.has_reason = true;
    out.reason = rest.substr(sp + 1);
  }
  return out;
}

brokkr::core::Status flash(std::vector<Target*>& devs, const std::vector<ImageSpec>& sources,
                           std::shared_ptr<const std::vector<std::byte>> pit_to_upload, const Cfg& cfg, Ui ui) noexcept;

} // namespace brokkr::odin
