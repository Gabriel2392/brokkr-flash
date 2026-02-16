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
#include <optional>
#include <thread>

namespace brokkr::core {

class SignalShield {
public:
    using Callback = std::function<void(const char* sig_desc, int count)>;

    SignalShield() = default;
    ~SignalShield();

    SignalShield(const SignalShield&) = delete;
    SignalShield& operator=(const SignalShield&) = delete;

    SignalShield(SignalShield&& o) noexcept;
    SignalShield& operator=(SignalShield&& o) noexcept;

    static std::optional<SignalShield> enable(Callback cb);

private:
    explicit SignalShield(Callback cb);

    void stop_and_restore_() noexcept;

private:
    Callback cb_{};
    std::jthread watcher_{};
    bool active_ = false;
};

} // namespace brokkr::core
