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
#include <string_view>

namespace brokkr::odin {

namespace {

constexpr std::int32_t BOOTLOADER_FAIL = static_cast<std::int32_t>(0xffffffff);

inline void require_connected(brokkr::core::IByteTransport& c) {
    if (!c.connected()) throw OdinIoError("transport not connected");
}

inline void check_resp(std::int32_t expected_id, const ResponseBox& r, std::int32_t* out_ack) {
    if (r.id == BOOTLOADER_FAIL) throw OdinProtocolError("Bootloader returned FAIL");
    if (r.id == std::numeric_limits<std::int32_t>::min()) throw OdinProtocolError("Invalid response id (INT_MIN)");
    if (r.id != expected_id) throw OdinProtocolError("Unexpected response id");
    if (out_ack) *out_ack = r.ack;
    else if (r.ack < 0) throw OdinProtocolError("Operation failed (negative ack)");
}

static std::int32_t lo32(std::uint64_t v) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(v & 0xFFFFFFFFull)); }
static std::int32_t hi32(std::uint64_t v) { return static_cast<std::int32_t>(static_cast<std::uint32_t>((v >> 32) & 0xFFFFFFFFull)); }

static std::int32_t require_i32_total(std::uint64_t v) {
    constexpr std::uint64_t max = static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
    if (v > max) throw OdinProtocolError("TOTALSIZE exceeds ODIN int32 limit on protocol v0/v1");
    return static_cast<std::int32_t>(v);
}

} // namespace

void OdinCommands::send_raw(std::span<const std::byte> data, unsigned retries) {
    require_connected(conn_);
    std::size_t off = 0;
    while (off < data.size()) {
        const int sent = conn_.send(brokkr::core::u8(data.subspan(off)), retries);
        if (sent <= 0) throw OdinIoError("send failed");
        off += static_cast<std::size_t>(sent);
    }
}

void OdinCommands::recv_raw(std::span<std::byte> data, unsigned retries) {
    require_connected(conn_);
    std::size_t off = 0;
    while (off < data.size()) {
        const int got = conn_.recv(brokkr::core::u8(data.subspan(off)), retries);
        if (got <= 0) throw OdinIoError("receive failed");
        off += static_cast<std::size_t>(got);
    }
}

void OdinCommands::send_request(const RequestBox& rq, unsigned retries) {
    send_raw(std::as_bytes(std::span{&rq, 1}), retries);
}

ResponseBox OdinCommands::recv_checked_response(std::int32_t expected_id, std::int32_t* out_ack, unsigned retries) {
    ResponseBox r{};
    recv_raw(std::as_writable_bytes(std::span{&r, 1}), retries);
    check_resp(expected_id, r, out_ack);
    return r;
}

ResponseBox OdinCommands::rpc_(RqtCommandType type,
                              RqtCommandParam param,
                              std::span<const std::int32_t> ints,
                              std::span<const std::int8_t> chars,
                              std::int32_t* out_ack,
                              unsigned retries)
{
    send_request(make_request(type, param, ints, chars), retries);
    return recv_checked_response(static_cast<std::int32_t>(type), out_ack, retries);
}

void OdinCommands::handshake(unsigned retries) {
    require_connected(conn_);

    if (conn_.kind() == brokkr::core::IByteTransport::Kind::UsbBulk) {
        static constexpr std::array<std::byte, 5> ping{std::byte{'O'},std::byte{'D'},std::byte{'I'},std::byte{'N'},std::byte{0}};
        send_raw(ping, retries);
    } else {
        static constexpr std::array<std::byte, 4> ping{std::byte{'O'},std::byte{'D'},std::byte{'I'},std::byte{'N'}};
        send_raw(ping, retries);
    }

    constexpr std::string_view expected = "LOKE";
    std::array<std::byte, 64> resp{};
    std::size_t have = 0;

    while (have < expected.size()) {
        const int got = conn_.recv(brokkr::core::u8(std::span<std::byte>(resp.data() + have, resp.size() - have)), retries);
        if (got <= 0) throw OdinIoError("Handshake receive failed");
        have += static_cast<std::size_t>(got);
    }

    if (std::memcmp(resp.data(), expected.data(), expected.size()) != 0) {
        throw OdinProtocolError("Handshake failed (expected LOKE)");
    }
}

InitTargetInfo OdinCommands::get_version(unsigned retries) {
    const std::int32_t ints[] = { static_cast<std::int32_t>(ProtocolVersion::PROTOCOL_VER5) };

    std::int32_t ack_i32 = 0;
    (void)rpc_(RqtCommandType::RQT_INIT, RqtCommandParam::RQT_INIT_TARGET, ints, {}, &ack_i32, retries);

    InitTargetInfo out;
    out.ack_word = static_cast<std::uint32_t>(ack_i32);
    return out;
}

void OdinCommands::setup_transfer_options(std::int32_t packet_size, unsigned retries) {
    const std::int32_t ints[] = { packet_size };
    (void)rpc_(RqtCommandType::RQT_INIT, RqtCommandParam::RQT_INIT_PACKETSIZE, ints, {}, nullptr, retries);
}

void OdinCommands::send_total_size(std::uint64_t total_size, ProtocolVersion proto, unsigned retries) {
    if (proto <= ProtocolVersion::PROTOCOL_VER1) {
        const std::int32_t v = require_i32_total(total_size);
        const std::int32_t ints[] = { v };
        (void)rpc_(RqtCommandType::RQT_INIT, RqtCommandParam::RQT_INIT_TOTALSIZE, ints, {}, nullptr, retries);
    } else {
        const std::int32_t ints[] = { lo32(total_size), hi32(total_size) };
        (void)rpc_(RqtCommandType::RQT_INIT, RqtCommandParam::RQT_INIT_TOTALSIZE, ints, {}, nullptr, retries);
    }
}

std::int32_t OdinCommands::get_pit_size(unsigned retries) {
    std::int32_t pitSize = 0;
    (void)rpc_(RqtCommandType::RQT_PIT, RqtCommandParam::RQT_PIT_GET, {}, {}, &pitSize, retries);
    return pitSize;
}

void OdinCommands::get_pit(std::span<std::byte> out, unsigned retries) {
    constexpr std::size_t PIT_TRANSMIT_UNIT = 500;
    if (out.empty()) throw OdinProtocolError("PIT output buffer empty");

    const std::size_t pitSize = out.size();
    const std::size_t parts = ((pitSize - 1) / PIT_TRANSMIT_UNIT) + 1;

    for (std::size_t idx = 0; idx < parts; ++idx) {
        const std::int32_t pitIndex = static_cast<std::int32_t>(idx);

        send_request(make_request(RqtCommandType::RQT_PIT,
                                  RqtCommandParam::RQT_PIT_START,
                                  std::span{&pitIndex, 1}),
                     retries);

        const std::size_t sizeToDownload = std::min<std::size_t>(PIT_TRANSMIT_UNIT, pitSize - (PIT_TRANSMIT_UNIT * idx));
        const std::size_t off = idx * PIT_TRANSMIT_UNIT;
        recv_raw(out.subspan(off, sizeToDownload), retries);
    }

    (void)conn_.recv_zlp();
    (void)rpc_(RqtCommandType::RQT_PIT, RqtCommandParam::RQT_PIT_COMPLETE, {}, {}, nullptr, retries);
}

void OdinCommands::set_pit(std::span<const std::byte> pit, unsigned retries) {
    if (pit.empty()) throw OdinProtocolError("PIT buffer empty");
    if (pit.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        throw OdinProtocolError("PIT too large for ODIN int32");
    }

    (void)rpc_(RqtCommandType::RQT_PIT, RqtCommandParam::RQT_PIT_SET, {}, {}, nullptr, retries);

    const auto pitSize32 = static_cast<std::int32_t>(pit.size());
    (void)rpc_(RqtCommandType::RQT_PIT, RqtCommandParam::RQT_PIT_START, std::span{&pitSize32, 1}, {}, nullptr, retries);

    send_raw(pit, retries);

    ResponseBox ack{};
    recv_raw(std::as_writable_bytes(std::span{&ack, 1}), retries);

    (void)rpc_(RqtCommandType::RQT_PIT, RqtCommandParam::RQT_PIT_COMPLETE, std::span{&pitSize32, 1}, {}, nullptr, retries);
}

void OdinCommands::begin_download(std::int32_t rounded_total_size, unsigned retries) {
    (void)rpc_(RqtCommandType::RQT_XMIT, RqtCommandParam::RQT_XMIT_DOWNLOAD, {}, {}, nullptr, retries);
    (void)rpc_(RqtCommandType::RQT_XMIT, RqtCommandParam::RQT_XMIT_START, std::span{&rounded_total_size, 1}, {}, nullptr, retries);
}

void OdinCommands::begin_download_compressed(std::int32_t comp_size, unsigned retries) {
    (void)rpc_(RqtCommandType::RQT_XMIT, RqtCommandParam::RQT_XMIT_COMPRESSED_DOWNLOAD, {}, {}, nullptr, retries);
    (void)rpc_(RqtCommandType::RQT_XMIT, RqtCommandParam::RQT_XMIT_COMPRESSED_START, std::span{&comp_size, 1}, {}, nullptr, retries);
}

void OdinCommands::end_download_impl_(RqtCommandParam complete_param,
                                     std::int32_t size_to_flash,
                                     std::int32_t part_id,
                                     std::int32_t dev_type,
                                     bool is_last,
                                     std::int32_t bin_type,
                                     bool efs_clear,
                                     bool boot_update,
                                     unsigned retries)
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

    (void)rpc_(RqtCommandType::RQT_XMIT, complete_param, data, {}, nullptr, retries);
}

void OdinCommands::end_download(std::int32_t size_to_flash,
                               std::int32_t part_id,
                               std::int32_t dev_type,
                               bool is_last,
                               std::int32_t bin_type,
                               bool efs_clear,
                               bool boot_update,
                               unsigned retries)
{
    end_download_impl_(RqtCommandParam::RQT_XMIT_COMPLETE, size_to_flash, part_id, dev_type, is_last,
                       bin_type, efs_clear, boot_update, retries);
}

void OdinCommands::end_download_compressed(std::int32_t decomp_size_to_flash,
                                          std::int32_t part_id,
                                          std::int32_t dev_type,
                                          bool is_last,
                                          std::int32_t bin_type,
                                          bool efs_clear,
                                          bool boot_update,
                                          unsigned retries)
{
    end_download_impl_(RqtCommandParam::RQT_XMIT_COMPRESSED_COMPLETE, decomp_size_to_flash, part_id, dev_type, is_last,
                       bin_type, efs_clear, boot_update, retries);
}

void OdinCommands::shutdown(ShutdownMode mode, unsigned retries) {
    require_connected(conn_);

    auto close_cmd = [&](RqtCommandParam p) { (void)rpc_(RqtCommandType::RQT_CLOSE, p, {}, {}, nullptr, retries); };

    if (mode == ShutdownMode::NoReboot) { close_cmd(RqtCommandParam::RQT_CLOSE_END); return; }
    if (mode == ShutdownMode::Reboot) { close_cmd(RqtCommandParam::RQT_CLOSE_END); close_cmd(RqtCommandParam::RQT_CLOSE_REBOOT); return; }

    // HACK: bootloader calls sys_restart('N', "download")

    close_cmd(RqtCommandParam::RQT_CLOSE_REDOWNLOAD);
    static constexpr std::string_view kAutoTest = "@#AuToTEstRst@#";
    std::array<std::byte, kAutoTest.size()> msg{};
    for (std::size_t i = 0; i < kAutoTest.size(); ++i) msg[i] = static_cast<std::byte>(kAutoTest[i]);
    send_raw({msg.data(), msg.size()}, retries);

    const int old_to = conn_.timeout_ms();
    conn_.set_timeout_ms(500);
    std::array<std::uint8_t, 64> tmp{};
    (void)conn_.recv({tmp.data(), tmp.size()}, 0);
    conn_.set_timeout_ms(old_to);
}

} // namespace brokkr::odin
