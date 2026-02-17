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

#include "protocol/odin/pit_transfer.hpp"

#include <spdlog/spdlog.h>

namespace brokkr::odin {

brokkr::core::Result<std::vector<std::byte>> download_pit_bytes(OdinCommands& odin, unsigned retries) noexcept {
  auto szr = odin.get_pit_size(retries);
  if (!szr) return brokkr::core::Result<std::vector<std::byte>>::Fail(std::move(szr.st.msg));

  const auto pit_size = szr.value;
  if (pit_size <= 0) {
    return brokkr::core::Result<std::vector<std::byte>>::Fail("Device returned invalid PIT size");
  }

  std::vector<std::byte> buf(static_cast<std::size_t>(pit_size));
  auto st = odin.get_pit(std::span<std::byte>(buf.data(), buf.size()), retries);
  if (!st.ok) return brokkr::core::Result<std::vector<std::byte>>::Fail(std::move(st.msg));

  spdlog::debug("Downloaded PIT bytes: {}", buf.size());
  return brokkr::core::Result<std::vector<std::byte>>::Ok(std::move(buf));
}

brokkr::core::Result<pit::PitTable> download_pit_table(OdinCommands& odin, unsigned retries) noexcept {
  auto br = download_pit_bytes(odin, retries);
  if (!br) return brokkr::core::Result<pit::PitTable>::Fail(std::move(br.st.msg));

  auto pr = pit::parse(std::span<const std::byte>(br.value.data(), br.value.size()));
  if (!pr) return brokkr::core::Result<pit::PitTable>::Fail(std::move(pr.st.msg));

  return brokkr::core::Result<pit::PitTable>::Ok(std::move(pr.value));
}

} // namespace brokkr::odin
