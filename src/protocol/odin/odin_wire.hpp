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

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

namespace brokkr::odin {

enum class RqtCommandType : std::int32_t {
  RQT_INIT  = 100,
  RQT_PIT   = 101,
  RQT_XMIT  = 102,
  RQT_CLOSE = 103,
  RQT_EMPTY = 0
};

enum class RqtCommandParam : std::int32_t {
  // INIT
  RQT_INIT_TARGET     = 0,
  RQT_INIT_RESETTIME  = 1,
  RQT_INIT_TOTALSIZE  = 2,
  RQT_INIT_OEMSTATE   = 3,
  RQT_INIT_NOOEMSTATE = 4,
  RQT_INIT_PACKETSIZE = 5,
  RQT_INIT_XMIT_SIZE  = 6,

  // PIT
  RQT_PIT_SET      = 0,
  RQT_PIT_GET      = 1,
  RQT_PIT_START    = 2,
  RQT_PIT_COMPLETE = 3,

  // XMIT (uncompressed)
  RQT_XMIT_DOWNLOAD  = 0,
  RQT_XMIT_DUMP      = 1,
  RQT_XMIT_START     = 2,
  RQT_XMIT_COMPLETE  = 3,
  RQT_XMIT_SMD       = 4,

  // XMIT (compressed)
  RQT_XMIT_COMPRESSED_DOWNLOAD = 5,
  RQT_XMIT_COMPRESSED_START    = 6,
  RQT_XMIT_COMPRESSED_COMPLETE = 7,

  // CLOSE
  RQT_CLOSE_END            = 0,
  RQT_CLOSE_REBOOT         = 1,
  RQT_CLOSE_DISCONNECT     = 2,
  RQT_CLOSE_REBOOT_RECOVERY= 3,
  RQT_CLOSE_REDOWNLOAD     = 4,
};

enum class ProtocolVersion : std::int16_t {
  PROTOCOL_NONE = 0,
  PROTOCOL_VER1 = 1,
  PROTOCOL_VER2 = 2,
  PROTOCOL_VER3 = 3,
  PROTOCOL_VER4 = 4,
  PROTOCOL_VER5 = 5,
};

#pragma pack(push, 1)
struct ResponseBox {
  std::int32_t id;
  std::int32_t ack;
};
#pragma pack(pop)
static_assert(sizeof(ResponseBox) == 8);

#pragma pack(push, 1)
struct RequestBox {
  static constexpr std::size_t DATA_INT_SIZE  = 9;
  static constexpr std::size_t DATA_CHAR_SIZE = 128;
  static constexpr std::size_t MD5_SIZE       = 32;

  std::int32_t id;
  std::int32_t data;
  std::int32_t intData[DATA_INT_SIZE];
  std::int8_t  charData[DATA_CHAR_SIZE];
  std::int8_t  md5[MD5_SIZE];

  std::int8_t  dummy[1024 - (2*4 + DATA_INT_SIZE*4 + DATA_CHAR_SIZE + MD5_SIZE)];
};
#pragma pack(pop)
static_assert(sizeof(RequestBox) == 1024);

inline RequestBox make_request(RqtCommandType type,
                               RqtCommandParam param,
                               std::span<const std::int32_t> ints = {},
                               std::span<const std::int8_t> chars = {})
{
  static_assert(std::endian::native == std::endian::little,
                "ODIN structs are assumed little-endian.");

  RequestBox r{};
  r.id   = static_cast<std::int32_t>(type);
  r.data = static_cast<std::int32_t>(param);

  if (!ints.empty()) {
    const auto n = (ints.size() > RequestBox::DATA_INT_SIZE) ? RequestBox::DATA_INT_SIZE : ints.size();
    std::memcpy(r.intData, ints.data(), n * sizeof(std::int32_t));
  }

  if (!chars.empty()) {
    const auto n = (chars.size() > RequestBox::DATA_CHAR_SIZE) ? RequestBox::DATA_CHAR_SIZE : chars.size();
    std::memcpy(r.charData, chars.data(), n * sizeof(std::int8_t));
  }

  return r;
}

} // namespace brokkr::odin
