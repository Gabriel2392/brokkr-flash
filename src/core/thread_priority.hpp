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

#if defined(BROKKR_PLATFORM_ANDROID)
#  include <sys/resource.h>
#  include <sys/syscall.h>
#  include <unistd.h>
#endif

namespace brokkr::core {

inline void bump_thread_priority(int nice_value = -8) noexcept {
#if defined(BROKKR_PLATFORM_ANDROID)
  const pid_t tid = static_cast<pid_t>(::syscall(SYS_gettid));
  ::setpriority(PRIO_PROCESS, static_cast<id_t>(tid), nice_value);
#else
  (void)nice_value;
#endif
}

} // namespace brokkr::core
