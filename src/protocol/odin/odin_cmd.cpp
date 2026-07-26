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

#include "protocol/odin/odin_cmd.hpp"
#include "protocol/odin/odin_wire.hpp"

#include "core/bytes.hpp"

#include <array>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>
#include <fmt/ranges.h>

namespace brokkr::odin {

namespace {

constexpr std::int32_t BOOTLOADER_FAIL = static_cast<std::int32_t>(0xffffffff);

inline brokkr::core::Status require_connected(brokkr::core::IByteTransport& c) noexcept {
  return c.connected() ? brokkr::core::Status{} : brokkr::core::fail("transport not connected");
}

static std::string_view bootloader_fail_text(std::int32_t ack) noexcept {
  switch (ack) {
    case -1: return "unsupported binary";
    case -2: return "write protection failure";
    case -3: return "failed to clear write protection";
    case -4: return "write failed";
    case -5: return "secure check failed";
    case -6: return "size check failed";
    case -7: return "ext4 image handling failed";
    case -8: return "image exceeds the 112MiB staging limit";

    case -127: return "unknown partition";
    case -126: return "PIT entry has no file name";
    case -125: return "m9kefs3 cannot be downloaded";
    case -124: return "image is too big for the partition";
    case -123: return "BL1 check failed";
    case -122: return "partition ID mismatch, or SUPER write rejected (upload a PIT first)";
    case -121: return "partition is not UFS on a UFS device";
    case -120: return "partition device type is not supported";
    case -119: return "block write failed";
    case -118: return "block read failed";
    case -117: return "block erase failed";
    case -116: return "unsupported filesystem type";
    case -115: return "filesystem is not supported";
    case -114: return "all binaries are not allowed to be flashed due to KG locked";
    case -113: return "MDM MODE, cannot download";
    case -112: return "Knox Guard prevents flashing factory binaries";
    case -111: return "SECURE CHECK FAIL (signature verification failed)";
    case -110: return "only official released binaries are allowed to be flashed (Knox Guard)";
    case -109: return "only official released binaries are allowed to be flashed (Knox Guard v2.0)";
    case -108: return "custom binary blocked by R/L";
    case -107: return "custom binary blocked by OEM lock";
    case -106: return "kernel rollback protection check failed";
    case -105: return "cross-verify (kill switch) lock";
    case -104: return "system rollback protection check failed";
    case -103: return "this is an ENG binary, please use a USER binary";
    case -102: return "TOKEN size is too big";
    case -101: return "unsupported version (rollback protection)";
    case -100: return "LDFW region erase failed";
    case -99: return "LDFW region write failed";
    case -98: return "UL_KEYS write failed";
    case -97: return "bootloader token install failed";
    case -96: return "image write failed";
    case -94: return "AUTO BLOCKER is on, cannot download";

    default: return {};
  }
}

inline brokkr::core::Status check_resp(std::int32_t expected_id, const ResponseBox& r, std::int32_t* out_ack) noexcept {
  if (r.id == BOOTLOADER_FAIL) {
    const auto why = bootloader_fail_text(r.ack);
    if (!why.empty()) return brokkr::core::failf("{} ({})", why, r.ack);
    return brokkr::core::failf("bootloader error (ack={} / 0x{:08X})", r.ack, static_cast<std::uint32_t>(r.ack));
  }
  if (r.id == std::numeric_limits<std::int32_t>::min()) return brokkr::core::fail("Invalid response id (INT_MIN)");
  if (r.id != expected_id) {
#ifndef NDEBUG
    return brokkr::core::failf("Unexpected response id (expected {}, got {}, ack={} / 0x{:08X})", expected_id, r.id,
                               r.ack, static_cast<std::uint32_t>(r.ack));
#else
    return brokkr::core::fail("Unexpected response id");
#endif
  }
  if (out_ack)
    *out_ack = r.ack;
  else if (r.ack < 0)
    return brokkr::core::failf("Operation failed ({})", r.ack);
  return {};
}

static brokkr::core::Status to_status(brokkr::core::Result<ResponseBox> r) noexcept {
  if (r) return {};
  return brokkr::core::fail(std::move(r.error()));
}

} // namespace

brokkr::core::Status OdinCommands::send_raw(std::span<const std::byte> data, unsigned retries) noexcept {
  auto st = require_connected(conn_);
  if (!st) return st;

  std::size_t off = 0;
  while (off < data.size()) {
    const int sent = conn_.send(brokkr::core::u8(data.subspan(off)), retries);
    if (sent <= 0) return brokkr::core::fail("send failed");
    off += static_cast<std::size_t>(sent);
  }
  return {};
}

brokkr::core::Status OdinCommands::recv_raw(std::span<std::byte> data, unsigned retries) noexcept {
  auto st = require_connected(conn_);
  if (!st) return st;

  std::size_t off = 0;
  while (off < data.size()) {
    const int got = conn_.recv(brokkr::core::u8(data.subspan(off)), retries);
    if (got <= 0) return brokkr::core::fail("receive failed");
    off += static_cast<std::size_t>(got);
  }
  return {};
}

brokkr::core::Status OdinCommands::send_request(const RequestBox& rq, unsigned retries) noexcept {
  if (spdlog::should_log(spdlog::level::debug)) {
    std::array<std::int32_t, RequestBox::DATA_INT_SIZE> ints{};
    for (std::size_t i = 0; i < RequestBox::DATA_INT_SIZE; ++i) ints[i] = brokkr::core::le_to_host(rq.intData[i]);
    spdlog::debug("ODIN >> id={} data={} ints=[{}]", brokkr::core::le_to_host(rq.id), brokkr::core::le_to_host(rq.data),
                  fmt::join(ints, ", "));
  }
  return send_raw(std::as_bytes(std::span{&rq, 1}), retries);
}

brokkr::core::Result<ResponseBox> OdinCommands::recv_checked_response(std::int32_t expected_id, std::int32_t* out_ack,
                                                                      unsigned retries) noexcept {
  ResponseBox r{};
  auto st = recv_raw(std::as_writable_bytes(std::span{&r, 1}), retries);
  if (!st) return brokkr::core::fail(std::move(st.error()));

  response_from_le(r);

  spdlog::debug("ODIN << id={} ack={} (0x{:08X}), expected id={}", r.id, r.ack, static_cast<std::uint32_t>(r.ack),
                expected_id);

  st = check_resp(expected_id, r, out_ack);
  if (!st) return brokkr::core::fail(std::move(st.error()));

  return r;
}

brokkr::core::Status OdinCommands::recv_data_ack(unsigned retries) noexcept {
  ResponseBox r{};
  auto st = recv_raw(std::as_writable_bytes(std::span{&r, 1}), retries);
  if (!st) return st;

  response_from_le(r);

  if (r.id != static_cast<std::int32_t>(RqtCommandType::RQT_EMPTY) || r.ack < 0)
    spdlog::debug("ODIN << data ack id={} ack={} (0x{:08X})", r.id, r.ack, static_cast<std::uint32_t>(r.ack));

  return {};
}

brokkr::core::Result<ResponseBox> OdinCommands::rpc_(RqtCommandType type, RqtCommandParam param,
                                                     std::span<const std::int32_t> ints,
                                                     std::span<const std::int8_t> chars, std::int32_t* out_ack,
                                                     unsigned retries) noexcept {
  auto st = send_request(make_request(type, param, ints, chars), retries);
  if (!st) return brokkr::core::fail(std::move(st.error()));
  return recv_checked_response(static_cast<std::int32_t>(type), out_ack, retries);
}

brokkr::core::Status OdinCommands::handshake(unsigned retries) noexcept {
  auto st = require_connected(conn_);
  if (!st) return st;

  if (conn_.kind() == brokkr::core::IByteTransport::Kind::UsbBulk) {
    static constexpr std::array<std::byte, 5> ping{std::byte{'O'}, std::byte{'D'}, std::byte{'I'}, std::byte{'N'},
                                                   std::byte{0}};
    spdlog::debug("ODIN >> handshake ping 'ODIN\\0' (5 bytes, USB)");
    st = send_raw(ping, retries);
  } else {
    static constexpr std::array<std::byte, 4> ping{std::byte{'O'}, std::byte{'D'}, std::byte{'I'}, std::byte{'N'}};
    spdlog::debug("ODIN >> handshake ping 'ODIN' (4 bytes, TCP)");
    st = send_raw(ping, retries);
  }
  if (!st) return st;

  constexpr std::string_view expected = "LOKE";
  std::array<std::byte, 64> resp{};
  std::size_t have = 0;

  while (have < expected.size()) {
    const int got = conn_.recv(brokkr::core::u8(std::span<std::byte>(resp.data() + have, resp.size() - have)), retries);
    if (got <= 0) return brokkr::core::fail("Handshake receive failed");
    have += static_cast<std::size_t>(got);
  }

  if (std::memcmp(resp.data(), expected.data(), expected.size()) != 0) {
    spdlog::error("Dump of handshake response ({} bytes):", have);
    spdlog::error("{}", fmt::join(resp.begin(), resp.begin() + have, " "));
#ifndef NDEBUG
    std::array<char, 65> as_str{};
    for (std::size_t i = 0; i < have && i < as_str.size() - 1; ++i) {
      const std::byte b = resp[i];
      as_str[i] = (b >= std::byte{32} && b <= std::byte{126}) ? static_cast<char>(b) : '.';
    }
    spdlog::error("Trying it as a string: {}", as_str.data());
#endif
    return brokkr::core::fail("Handshake failed (expected LOKE)");
  }

  spdlog::debug("ODIN handshake OK");
  return {};
}

brokkr::core::Result<InitTargetInfo> OdinCommands::get_version(unsigned retries) noexcept {
  const std::int32_t ints[] = {static_cast<std::int32_t>(ProtocolVersion::PROTOCOL_VER5)};

  std::int32_t ack_i32 = 0;
  auto r = rpc_(RqtCommandType::RQT_INIT, RqtCommandParam::RQT_INIT_TARGET, ints, {}, &ack_i32, retries);
  if (!r) return brokkr::core::fail(std::move(r.error()));

  InitTargetInfo out;
  out.ack_word = static_cast<std::uint32_t>(ack_i32);
  spdlog::debug("ODIN target ack word: 0x{:08X} (protocol v{}, compressed download {})", out.ack_word,
                static_cast<int>(out.protocol()), out.supports_compressed_download());
  return out;
}

brokkr::core::Status OdinCommands::setup_transfer_options(std::int32_t packet_size, unsigned retries) noexcept {
  const std::int32_t ints[] = {packet_size};
  auto r = rpc_(RqtCommandType::RQT_INIT, RqtCommandParam::RQT_INIT_PACKETSIZE, ints, {}, nullptr, retries);
  if (!r) return brokkr::core::fail(std::move(r.error()));

  if (packet_size > 0)
    conn_.set_packet_size_hint(static_cast<std::size_t>(static_cast<std::uint32_t>(packet_size)));

  return {};
}

brokkr::core::Status OdinCommands::send_total_size(std::uint64_t total_size, unsigned retries) noexcept {
  constexpr std::uint64_t kSplit = 0xFFFFFFFFull;
  const std::int32_t ints[] = {
      static_cast<std::int32_t>(static_cast<std::uint32_t>(total_size % kSplit)),
      static_cast<std::int32_t>(static_cast<std::uint32_t>(total_size / kSplit)),
  };
  return to_status(rpc_(RqtCommandType::RQT_INIT, RqtCommandParam::RQT_INIT_TOTALSIZE, ints, {}, nullptr, retries));
}

brokkr::core::Result<std::int32_t> OdinCommands::get_pit_size(unsigned retries) noexcept {
  std::int32_t pitSize = 0;
  auto r = rpc_(RqtCommandType::RQT_PIT, RqtCommandParam::RQT_PIT_GET, {}, {}, &pitSize, retries);
  if (!r) return brokkr::core::fail(std::move(r.error()));
  return pitSize;
}

brokkr::core::Status OdinCommands::get_pit(std::span<std::byte> out, unsigned retries) noexcept {
  constexpr std::size_t PIT_TRANSMIT_UNIT = 500;
  if (out.empty()) return brokkr::core::fail("PIT output buffer empty");

  const std::size_t pitSize = out.size();
  const std::size_t parts = ((pitSize - 1) / PIT_TRANSMIT_UNIT) + 1;

  for (std::size_t idx = 0; idx < parts; ++idx) {
    const std::int32_t pitIndex = static_cast<std::int32_t>(idx);

    auto st = send_request(
        make_request(RqtCommandType::RQT_PIT, RqtCommandParam::RQT_PIT_START, std::span{&pitIndex, 1}), retries);
    if (!st) return st;

    const std::size_t sizeToDownload = std::min<std::size_t>(PIT_TRANSMIT_UNIT, pitSize - (PIT_TRANSMIT_UNIT * idx));
    const std::size_t off = idx * PIT_TRANSMIT_UNIT;

    st = recv_raw(out.subspan(off, sizeToDownload), retries);
    if (!st) return st;
  }

  (void)conn_.recv_zlp();
  return to_status(rpc_(RqtCommandType::RQT_PIT, RqtCommandParam::RQT_PIT_COMPLETE, {}, {}, nullptr, retries));
}

brokkr::core::Status OdinCommands::set_pit(std::span<const std::byte> pit, unsigned retries) noexcept {
  if (pit.empty()) return brokkr::core::fail("PIT buffer empty");
  if (pit.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
    return brokkr::core::fail("PIT too large for ODIN int32");

  auto r1 = rpc_(RqtCommandType::RQT_PIT, RqtCommandParam::RQT_PIT_SET, {}, {}, nullptr, retries);
  if (!r1) return brokkr::core::fail(std::move(r1.error()));

  const auto pitSize32 = static_cast<std::int32_t>(pit.size());
  auto r2 = rpc_(RqtCommandType::RQT_PIT, RqtCommandParam::RQT_PIT_START, std::span{&pitSize32, 1}, {}, nullptr,
                 retries);
  if (!r2) return brokkr::core::fail(std::move(r2.error()));

  auto st = send_raw(pit, retries);
  if (!st) return st;

  st = recv_data_ack(retries);
  if (!st) return st;

  return to_status(rpc_(RqtCommandType::RQT_PIT, RqtCommandParam::RQT_PIT_COMPLETE, std::span{&pitSize32, 1}, {},
                        nullptr, retries));
}

brokkr::core::Status OdinCommands::declare_super_used_blocks(std::int32_t blocks, unsigned retries) noexcept {
  const std::int32_t ints[] = {blocks};
  return to_status(rpc_(RqtCommandType::RQT_SUPER, RqtCommandParam::RQT_SUPER_USED_BLOCKS, ints, {}, nullptr, retries));
}

brokkr::core::Status OdinCommands::begin_download(std::int32_t rounded_total_size, unsigned retries) noexcept {
  auto r1 = rpc_(RqtCommandType::RQT_XMIT, RqtCommandParam::RQT_XMIT_DOWNLOAD, {}, {}, nullptr, retries);
  if (!r1) return brokkr::core::fail(std::move(r1.error()));
  return to_status(rpc_(RqtCommandType::RQT_XMIT, RqtCommandParam::RQT_XMIT_START, std::span{&rounded_total_size, 1},
                        {}, nullptr, retries));
}

brokkr::core::Status OdinCommands::begin_download_compressed(std::int32_t comp_size, std::int32_t decomp_size,
                                                             unsigned retries) noexcept {
  auto r1 = rpc_(RqtCommandType::RQT_XMIT, RqtCommandParam::RQT_XMIT_COMPRESSED_DOWNLOAD, {}, {}, nullptr, retries);
  if (!r1) return brokkr::core::fail(std::move(r1.error()));

  const std::int32_t ints[] = {comp_size, decomp_size};
  return to_status(
      rpc_(RqtCommandType::RQT_XMIT, RqtCommandParam::RQT_XMIT_COMPRESSED_START, ints, {}, nullptr, retries));
}

brokkr::core::Status OdinCommands::end_download_impl_(RqtCommandParam complete_param, std::int32_t size_to_flash,
                                                      std::int32_t part_id, std::int32_t dev_type, bool is_last,
                                                      std::int32_t bin_type, bool efs_clear, bool boot_update,
                                                      unsigned retries) noexcept {
  std::int32_t data[8]{};
  data[0] = 0;
  data[1] = size_to_flash;
  data[2] = bin_type;
  data[3] = dev_type;
  data[4] = part_id;
  data[5] = is_last ? 1 : 0;
  data[6] = efs_clear ? 1 : 0;
  data[7] = boot_update ? 1 : 0;

  return to_status(rpc_(RqtCommandType::RQT_XMIT, complete_param, data, {}, nullptr, retries));
}

brokkr::core::Status OdinCommands::end_download(std::int32_t size_to_flash, std::int32_t part_id, std::int32_t dev_type,
                                                bool is_last, std::int32_t bin_type, bool efs_clear, bool boot_update,
                                                unsigned retries) noexcept {
  return end_download_impl_(RqtCommandParam::RQT_XMIT_COMPLETE, size_to_flash, part_id, dev_type, is_last, bin_type,
                            efs_clear, boot_update, retries);
}

brokkr::core::Status OdinCommands::end_download_compressed(std::int32_t decomp_size_to_flash, std::int32_t part_id,
                                                           std::int32_t dev_type, bool is_last, std::int32_t bin_type,
                                                           bool efs_clear, bool boot_update,
                                                           unsigned retries) noexcept {
  return end_download_impl_(RqtCommandParam::RQT_XMIT_COMPRESSED_COMPLETE, decomp_size_to_flash, part_id, dev_type,
                            is_last, bin_type, efs_clear, boot_update, retries);
}

brokkr::core::Status OdinCommands::shutdown(ShutdownMode mode, unsigned retries) noexcept {
  auto st = require_connected(conn_);
  if (!st) return st;

  auto close_cmd = [&](RqtCommandParam p, const char* name) -> brokkr::core::Status {
    auto r = rpc_(RqtCommandType::RQT_CLOSE, p, {}, {}, nullptr, retries);
    if (!r) {
      if (p == RqtCommandParam::RQT_CLOSE_REBOOT) {
        spdlog::debug("Failed to send shutdown command {}: {}", name, r.error());
      } else {
        spdlog::error("Failed to send shutdown command {}: {}", name, r.error());
      }
    } else {
      spdlog::debug("Sent shutdown command {}", name);
    }
    return to_status(std::move(r));
  };

  if (mode == ShutdownMode::NoReboot) {
    return close_cmd(RqtCommandParam::RQT_CLOSE_END, "RQT_CLOSE_END");
  }
  if (mode == ShutdownMode::Reboot) {
    st = close_cmd(RqtCommandParam::RQT_CLOSE_END, "RQT_CLOSE_END");
    if (!st) return st;
    auto reboot_st = close_cmd(RqtCommandParam::RQT_CLOSE_REBOOT, "RQT_CLOSE_REBOOT");
    if (!reboot_st)
      spdlog::debug("Reboot command failed (device likely already rebooting): {}", reboot_st.error());
    return {};
  }

  return brokkr::core::fail("Invalid shutdown mode");
}

} // namespace brokkr::odin
