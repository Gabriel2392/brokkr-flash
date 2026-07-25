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

#include <functional>

namespace brokkr::core {

#if defined(BROKKR_PLATFORM_ANDROID) && \
    !(defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L)
template <typename Signature>
using MoveOnlyFunction = std::function<Signature>;
#else
template <typename Signature>
using MoveOnlyFunction = std::move_only_function<Signature>;
#endif

} // namespace brokkr::core