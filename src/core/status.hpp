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

#include <expected>
#include <string>
#include <utility>

#include <fmt/format.h>

namespace brokkr::core {

using Error = std::string;
using Status = std::expected<void, Error>;

template <class T>
using Result = std::expected<T, Error>;

inline std::unexpected<Error> fail(Error msg) { return std::unexpected<Error>(std::move(msg)); }

template <class... Args>
inline std::unexpected<Error> failf(fmt::format_string<Args...> f, Args&&... args) {
  return fail(fmt::format(f, std::forward<Args>(args)...));
}

} // namespace brokkr::core

#define BRK_CAT2(a, b) a##b
#define BRK_CAT(a, b) BRK_CAT2(a, b)

#define BRK_TRY_IMPL(n, expr)                                                                                          \
  do {                                                                                                                 \
    auto BRK_CAT(_brk_r_, n) = (expr);                                                                                 \
    if (!BRK_CAT(_brk_r_, n)) return brokkr::core::fail(std::move(BRK_CAT(_brk_r_, n).error()));                       \
  } while (0)

#define BRK_TRY(expr) BRK_TRY_IMPL(__COUNTER__, expr)

#define BRK_TRYV_IMPL(n, lhs, expr)                                                                                    \
  auto BRK_CAT(_brk_r_, n) = (expr);                                                                                   \
  if (!BRK_CAT(_brk_r_, n)) return brokkr::core::fail(std::move(BRK_CAT(_brk_r_, n).error()));                         \
  auto lhs = std::move(*BRK_CAT(_brk_r_, n))

#define BRK_TRYV(lhs, expr) BRK_TRYV_IMPL(__COUNTER__, lhs, expr)
