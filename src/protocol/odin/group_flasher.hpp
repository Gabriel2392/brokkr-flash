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
#include "platform/platform_all.hpp"
#include "protocol/odin/flash.hpp"
#include "protocol/odin/odin_cmd.hpp"
#include "protocol/odin/pit.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace brokkr::odin {

struct UsbTarget {
    std::string devnode;
    brokkr::platform::UsbFsDevice dev;
    brokkr::platform::UsbFsConnection conn;

    InitTargetInfo init{};
    ProtocolVersion proto = ProtocolVersion::PROTOCOL_NONE;

    std::vector<std::byte> pit_bytes{};
    pit::PitTable pit_table{};

    explicit UsbTarget(std::string devnode_path)
        : devnode(std::move(devnode_path)), dev(devnode), conn(dev) {}
};

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
    std::size_t pkt_any_old    = 128ull * 1024;

    int preflash_timeout_ms = 1000;
    unsigned preflash_retries = 2;

    int flash_timeout_ms = 45'000;

    bool reboot_after = true;
    bool redownload_after = false;
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

enum class Mode { Flash, RebootOnly, PitSetOnly };

void flash(std::vector<Target*>& devs,
           const std::vector<ImageSpec>& sources,
           std::shared_ptr<const std::vector<std::byte>> pit_to_upload,
           const Cfg& cfg,
           Ui ui,
           Mode mode);

void flash(std::vector<UsbTarget*>& devs,
           const std::vector<ImageSpec>& sources,
           std::shared_ptr<const std::vector<std::byte>> pit_to_upload,
           const Cfg& cfg,
           Ui ui,
           Mode mode);

} // namespace brokkr::odin
