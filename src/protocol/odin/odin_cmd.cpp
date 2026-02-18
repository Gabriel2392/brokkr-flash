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

#include <spdlog/spdlog.h>

namespace brokkr::odin {

namespace {

constexpr std::int32_t BOOTLOADER_FAIL = static_cast<std::int32_t>(0xffffffff);

inline brokkr::core::Status require_connected(brokkr::core::IByteTransport& c) noexcept {
  return c.connected() ? brokkr::core::Status{} : brokkr::core::fail("transport not connected");
}

inline brokkr::core::Status check_resp(std::int32_t expected_id, const ResponseBox& r, std::int32_t* out_ack) noexcept {
  if (r.id == BOOTLOADER_FAIL) return brokkr::core::fail("Bootloader returned FAIL");
  if (r.id == std::numeric_limits<std::int32_t>::min()) return brokkr::core::fail("Invalid response id (INT_MIN)");
  if (r.id != expected_id) return brokkr::core::fail("Unexpected response id");
  if (out_ack) *out_ack = r.ack;
  else if (r.ack < 0) return brokkr::core::fail("Operation failed (negative ack)");
  return {};
}

static std::int32_t lo32(std::uint64_t v) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(v & 0xFFFFFFFFull)); }
static std::int32_t hi32(std::uint64_t v) { return static_cast<std::int32_t>(static_cast<std::uint32_t>((v >> 32) & 0xFFFFFFFFull)); }

static brokkr::core::Result<std::int32_t> require_i32_total(std::uint64_t v) noexcept {
  constexpr std::uint64_t max = static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
  if (v > max) return brokkr::core::fail("TOTALSIZE exceeds ODIN int32 limit on protocol v0/v1");
  return static_cast<std::int32_t>(v);
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
  return send_raw(std::as_bytes(std::span{&rq, 1}), retries);
}

brokkr::core::Result<ResponseBox>
OdinCommands::recv_checked_response(std::int32_t expected_id, std::int32_t* out_ack, unsigned retries) noexcept {
  ResponseBox r{};
  auto st = recv_raw(std::as_writable_bytes(std::span{&r, 1}), retries);
  if (!st) return brokkr::core::fail(std::move(st.error()));

  st = check_resp(expected_id, r, out_ack);
  if (!st) return brokkr::core::fail(std::move(st.error()));

  return r;
}

brokkr::core::Result<ResponseBox>
OdinCommands::rpc_(RqtCommandType type,
                   RqtCommandParam param,
                   std::span<const std::int32_t> ints,
                   std::span<const std::int8_t> chars,
                   std::int32_t* out_ack,
                   unsigned retries) noexcept
{
  auto st = send_request(make_request(type, param, ints, chars), retries);
  if (!st) return brokkr::core::fail(std::move(st.error()));
  return recv_checked_response(static_cast<std::int32_t>(type), out_ack, retries);
}

brokkr::core::Status OdinCommands::handshake(unsigned retries) noexcept {
  auto st = require_connected(conn_);
  if (!st) return st;

  if (conn_.kind() == brokkr::core::IByteTransport::Kind::UsbBulk) {
    static constexpr std::array<std::byte, 5> ping{std::byte{'O'},std::byte{'D'},std::byte{'I'},std::byte{'N'},std::byte{0}};
    st = send_raw(ping, retries);
  } else {
    static constexpr std::array<std::byte, 4> ping{std::byte{'O'},std::byte{'D'},std::byte{'I'},std::byte{'N'}};
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

  if (std::memcmp(resp.data(), expected.data(), expected.size()) != 0) return brokkr::core::fail("Handshake failed (expected LOKE)");

  spdlog::debug("ODIN handshake OK");
  return {};
}

brokkr::core::Result<InitTargetInfo> OdinCommands::get_version(unsigned retries) noexcept {
  const std::int32_t ints[] = { static_cast<std::int32_t>(ProtocolVersion::PROTOCOL_VER5) };

  std::int32_t ack_i32 = 0;
  auto r = rpc_(RqtCommandType::RQT_INIT, RqtCommandParam::RQT_INIT_TARGET, ints, {}, &ack_i32, retries);
  if (!r) return brokkr::core::fail(std::move(r.error()));

  InitTargetInfo out;
  out.ack_word = static_cast<std::uint32_t>(ack_i32);
  return out;
}

brokkr::core::Status OdinCommands::setup_transfer_options(std::int32_t packet_size, unsigned retries) noexcept {
  const std::int32_t ints[] = { packet_size };
  auto r = rpc_(RqtCommandType::RQT_INIT, RqtCommandParam::RQT_INIT_PACKETSIZE, ints, {}, nullptr, retries);
  return r ? brokkr::core::Status{} : brokkr::core::fail(std::move(r.error()));
}

brokkr::core::Status OdinCommands::send_total_size(std::uint64_t total_size, ProtocolVersion proto, unsigned retries) noexcept {
  if (proto <= ProtocolVersion::PROTOCOL_VER1) {
    auto v = require_i32_total(total_size);
    if (!v) return brokkr::core::fail(std::move(v.error()));
    const std::int32_t ints[] = { *v };
    auto r = rpc_(RqtCommandType::RQT_INIT, RqtCommandParam::RQT_INIT_TOTALSIZE, ints, {}, nullptr, retries);
    return r ? brokkr::core::Status{} : brokkr::core::fail(std::move(r.error()));
  }

  const std::int32_t ints[] = { lo32(total_size), hi32(total_size) };
  auto r = rpc_(RqtCommandType::RQT_INIT, RqtCommandParam::RQT_INIT_TOTALSIZE, ints, {}, nullptr, retries);
  return r ? brokkr::core::Status{} : brokkr::core::fail(std::move(r.error()));
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

    auto st = send_request(make_request(RqtCommandType::RQT_PIT,
                                        RqtCommandParam::RQT_PIT_START,
                                        std::span{&pitIndex, 1}),
                           retries);
    if (!st) return st;

    const std::size_t sizeToDownload = std::min<std::size_t>(PIT_TRANSMIT_UNIT, pitSize - (PIT_TRANSMIT_UNIT * idx));
    const std::size_t off = idx * PIT_TRANSMIT_UNIT;

    st = recv_raw(out.subspan(off, sizeToDownload), retries);
    if (!st) return st;
  }

  (void)conn_.recv_zlp();
  auto r = rpc_(RqtCommandType::RQT_PIT, RqtCommandParam::RQT_PIT_COMPLETE, {}, {}, nullptr, retries);
  return r ? brokkr::core::Status{} : brokkr::core::fail(std::move(r.error()));
}

brokkr::core::Status OdinCommands::set_pit(std::span<const std::byte> pit, unsigned retries) noexcept {
  if (pit.empty()) return brokkr::core::fail("PIT buffer empty");
  if (pit.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) return brokkr::core::fail("PIT too large for ODIN int32");

  auto r1 = rpc_(RqtCommandType::RQT_PIT, RqtCommandParam::RQT_PIT_SET, {}, {}, nullptr, retries);
  if (!r1) return brokkr::core::fail(std::move(r1.error()));

  const auto pitSize32 = static_cast<std::int32_t>(pit.size());
  auto r2 = rpc_(RqtCommandType::RQT_PIT, RqtCommandParam::RQT_PIT_START, std::span{&pitSize32, 1}, {}, nullptr, retries);
  if (!r2) return brokkr::core::fail(std::move(r2.error()));

  auto st = send_raw(pit, retries);
  if (!st) return st;

  ResponseBox ack{};
  st = recv_raw(std::as_writable_bytes(std::span{&ack, 1}), retries);
  if (!st) return st;

  auto r3 = rpc_(RqtCommandType::RQT_PIT, RqtCommandParam::RQT_PIT_COMPLETE, std::span{&pitSize32, 1}, {}, nullptr, retries);
  return r3 ? brokkr::core::Status{} : brokkr::core::fail(std::move(r3.error()));
}

brokkr::core::Status OdinCommands::begin_download(std::int32_t rounded_total_size, unsigned retries) noexcept {
  auto r1 = rpc_(RqtCommandType::RQT_XMIT, RqtCommandParam::RQT_XMIT_DOWNLOAD, {}, {}, nullptr, retries);
  if (!r1) return brokkr::core::fail(std::move(r1.error()));
  auto r2 = rpc_(RqtCommandType::RQT_XMIT, RqtCommandParam::RQT_XMIT_START, std::span{&rounded_total_size, 1}, {}, nullptr, retries);
  return r2 ? brokkr::core::Status{} : brokkr::core::fail(std::move(r2.error()));
}

brokkr::core::Status OdinCommands::begin_download_compressed(std::int32_t comp_size, unsigned retries) noexcept {
  auto r1 = rpc_(RqtCommandType::RQT_XMIT, RqtCommandParam::RQT_XMIT_COMPRESSED_DOWNLOAD, {}, {}, nullptr, retries);
  if (!r1) return brokkr::core::fail(std::move(r1.error()));
  auto r2 = rpc_(RqtCommandType::RQT_XMIT, RqtCommandParam::RQT_XMIT_COMPRESSED_START, std::span{&comp_size, 1}, {}, nullptr, retries);
  return r2 ? brokkr::core::Status{} : brokkr::core::fail(std::move(r2.error()));
}

brokkr::core::Status OdinCommands::end_download_impl_(RqtCommandParam complete_param,
                                                      std::int32_t size_to_flash,
                                                      std::int32_t part_id,
                                                      std::int32_t dev_type,
                                                      bool is_last,
                                                      std::int32_t bin_type,
                                                      bool efs_clear,
                                                      bool boot_update,
                                                      unsigned retries) noexcept
{
  std::int32_t data[8]{};
  data[0] = 0;
  data[1] = size_to_flash;
  data[2] = bin_type;
  data[3] = dev_type;
  data[4] = part_id;
  data[5] = is_last ? 1 : 0;
  data[6] = efs_clear ? 1 : 0;
  data[7] = boot_update ? 1 : 0;

  auto r = rpc_(RqtCommandType::RQT_XMIT, complete_param, data, {}, nullptr, retries);
  return r ? brokkr::core::Status{} : brokkr::core::fail(std::move(r.error()));
}

brokkr::core::Status OdinCommands::end_download(std::int32_t size_to_flash,
                                                std::int32_t part_id,
                                                std::int32_t dev_type,
                                                bool is_last,
                                                std::int32_t bin_type,
                                                bool efs_clear,
                                                bool boot_update,
                                                unsigned retries) noexcept
{
  return end_download_impl_(RqtCommandParam::RQT_XMIT_COMPLETE, size_to_flash, part_id, dev_type, is_last,
                            bin_type, efs_clear, boot_update, retries);
}

brokkr::core::Status OdinCommands::end_download_compressed(std::int32_t decomp_size_to_flash,
                                                           std::int32_t part_id,
                                                           std::int32_t dev_type,
                                                           bool is_last,
                                                           std::int32_t bin_type,
                                                           bool efs_clear,
                                                           bool boot_update,
                                                           unsigned retries) noexcept
{
  return end_download_impl_(RqtCommandParam::RQT_XMIT_COMPRESSED_COMPLETE, decomp_size_to_flash, part_id, dev_type, is_last,
                            bin_type, efs_clear, boot_update, retries);
}

brokkr::core::Status OdinCommands::shutdown(ShutdownMode mode, unsigned retries) noexcept {
  auto st = require_connected(conn_);
  if (!st) return st;

  auto close_cmd = [&](RqtCommandParam p) -> brokkr::core::Status {
    auto r = rpc_(RqtCommandType::RQT_CLOSE, p, {}, {}, nullptr, retries);
    return r ? brokkr::core::Status{} : brokkr::core::fail(std::move(r.error()));
  };

  if (mode == ShutdownMode::NoReboot) return close_cmd(RqtCommandParam::RQT_CLOSE_END);
  if (mode == ShutdownMode::Reboot) {
    st = close_cmd(RqtCommandParam::RQT_CLOSE_END);
    if (!st) return st;
    return close_cmd(RqtCommandParam::RQT_CLOSE_REBOOT);
  }

  st = close_cmd(RqtCommandParam::RQT_CLOSE_REDOWNLOAD);
  if (!st) return st;

  static constexpr std::string_view kAutoTest = "@#AuToTEstRst@#";
  std::array<std::byte, kAutoTest.size()> msg{};
  for (std::size_t i = 0; i < kAutoTest.size(); ++i) msg[i] = static_cast<std::byte>(kAutoTest[i]);

  st = send_raw({msg.data(), msg.size()}, retries);
  if (!st) return st;

  const int old_to = conn_.timeout_ms();
  conn_.set_timeout_ms(500);
  std::array<std::uint8_t, 64> tmp{};
  (void)conn_.recv({tmp.data(), tmp.size()}, 0);
  conn_.set_timeout_ms(old_to);
  return {};
}

} // namespace brokkr::odin
