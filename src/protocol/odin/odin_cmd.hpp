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
#include "protocol/odin/odin_wire.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace brokkr::odin {

struct OdinError : std::runtime_error {
  using std::runtime_error::runtime_error;
};
struct OdinIoError : OdinError {
  using OdinError::OdinError;
};
struct OdinProtocolError : OdinError {
  using OdinError::OdinError;
};

struct InitTargetInfo {
  std::uint32_t ack_word = 0;

  std::uint16_t proto_raw() const noexcept {
    return static_cast<std::uint16_t>((ack_word >> 16) & 0xFFFFu);
  }

  ProtocolVersion protocol() const noexcept {
    const auto p = proto_raw();
    if (p == 0)
      return ProtocolVersion::PROTOCOL_VER1;
    return static_cast<ProtocolVersion>(static_cast<std::int16_t>(p));
  }

  bool supports_compressed_download() const noexcept {
    return (ack_word & 0x8000u) != 0;
  }
};

class OdinCommands {
public:
  enum class ShutdownMode { NoReboot, Reboot, ReDownload };

  explicit OdinCommands(brokkr::core::IByteTransport &c) : conn_(c) {}

  void handshake(unsigned retries = 8);
  InitTargetInfo get_version(unsigned retries = 8);

  void setup_transfer_options(std::int32_t packet_size, unsigned retries = 8);

  void send_total_size(std::uint64_t total_size, ProtocolVersion proto,
                       unsigned retries = 8);

  std::int32_t get_pit_size(unsigned retries = 8);
  void get_pit(std::span<std::byte> out, unsigned retries = 8);
  void set_pit(std::span<const std::byte> pit, unsigned retries = 8);

  void begin_download(std::int32_t rounded_total_size, unsigned retries = 8);
  void begin_download_compressed(std::int32_t comp_size, unsigned retries = 8);

  void end_download(std::int32_t size_to_flash, std::int32_t part_id,
                    std::int32_t dev_type, bool is_last,
                    std::int32_t bin_type = 0, bool efs_clear = false,
                    bool boot_update = false, unsigned retries = 8);

  void end_download_compressed(std::int32_t decomp_size_to_flash,
                               std::int32_t part_id, std::int32_t dev_type,
                               bool is_last, std::int32_t bin_type = 0,
                               bool efs_clear = false, bool boot_update = false,
                               unsigned retries = 8);

  void shutdown(ShutdownMode mode, unsigned retries = 8);
  void shutdown(bool reboot, unsigned retries = 8) {
    shutdown(reboot ? ShutdownMode::Reboot : ShutdownMode::NoReboot, retries);
  }

  void send_raw(std::span<const std::byte> data, unsigned retries = 8);
  void recv_raw(std::span<std::byte> data, unsigned retries = 8);
  ResponseBox recv_checked_response(std::int32_t expected_id,
                                    std::int32_t *out_ack = nullptr,
                                    unsigned retries = 8);
  void send_request(const RequestBox &rq, unsigned retries = 8);

private:
  ResponseBox rpc_(RqtCommandType type, RqtCommandParam param,
                   std::span<const std::int32_t> ints = {},
                   std::span<const std::int8_t> chars = {},
                   std::int32_t *out_ack = nullptr, unsigned retries = 8);

  void end_download_impl_(RqtCommandParam complete_param,
                          std::int32_t size_to_flash, std::int32_t part_id,
                          std::int32_t dev_type, bool is_last,
                          std::int32_t bin_type, bool efs_clear,
                          bool boot_update, unsigned retries);

private:
  brokkr::core::IByteTransport &conn_;
};

} // namespace brokkr::odin
