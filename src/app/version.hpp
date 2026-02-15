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

#include <string>

#define BROKKR_VERSION_BASE "0.3.0"
#define BROKKR_VERSION_STAGE "release"

#ifndef BROKKR_COMMIT_COUNT
#define BROKKR_COMMIT_COUNT "0"
#endif

namespace brokkr::app {

inline const std::string& version_string() {
    static const std::string v = [] {
        const std::string base  = BROKKR_VERSION_BASE;
        const std::string stage = BROKKR_VERSION_STAGE;
        const std::string cnt   = BROKKR_COMMIT_COUNT;

        if (stage == "release") {
            return base + "+" + cnt;
        }
        return base + "-" + stage + "+" + cnt;
    }();
    return v;
}

} // namespace brokkr::app
