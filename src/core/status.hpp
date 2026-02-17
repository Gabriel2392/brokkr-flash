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

#include <fmt/format.h>

#include <new>
#include <string>
#include <type_traits>
#include <utility>

namespace brokkr::core {

struct Status {
  bool ok = true;
  std::string msg;

  constexpr Status() = default;
  Status(bool ok_, std::string msg_) : ok(ok_), msg(std::move(msg_)) {}

  static Status Ok() { return {}; }

  static Status Fail(std::string msg) { return Status(false, std::move(msg)); }

  template <class... Args>
  static Status Failf(fmt::format_string<Args...> f, Args&&... args) {
    return Fail(fmt::format(f, std::forward<Args>(args)...));
  }

  explicit operator bool() const noexcept { return ok; }
};

template <class T>
struct Result {
  // NOTE: default construct = failure-without-value. This avoids requiring T{}.
  Status st{false, {}};
  bool has_value = false;

  struct Empty { };
  union {
    Empty empty;
    T value;
  };

  Result() noexcept : empty{} {}

  ~Result() { reset_(); }

  Result(const Result&) = delete;
  Result& operator=(const Result&) = delete;

  Result(Result&& o) noexcept(std::is_nothrow_move_constructible_v<T>)
    : st(std::move(o.st))
    , has_value(o.has_value)
  {
    if (has_value) {
      ::new (static_cast<void*>(std::addressof(value))) T(std::move(o.value));
      o.reset_();
    } else {
      empty = Empty{};
    }
  }

  Result& operator=(Result&& o) noexcept(std::is_nothrow_move_constructible_v<T>) {
    if (this == &o) return *this;
    reset_();

    st = std::move(o.st);
    has_value = o.has_value;

    if (has_value) {
      ::new (static_cast<void*>(std::addressof(value))) T(std::move(o.value));
      o.reset_();
    } else {
      empty = Empty{};
    }
    return *this;
  }

  static Result Ok(T v) {
    Result r;
    r.st = Status::Ok();
    ::new (static_cast<void*>(std::addressof(r.value))) T(std::move(v));
    r.has_value = true;
    return r;
  }

  static Result Fail(std::string msg) {
    Result r;
    r.st = Status::Fail(std::move(msg));
    return r;
  }

  template <class... Args>
  static Result Failf(fmt::format_string<Args...> f, Args&&... args) {
    return Fail(fmt::format(f, std::forward<Args>(args)...));
  }

  explicit operator bool() const noexcept { return st.ok; }

private:
  void reset_() noexcept {
    if (has_value) {
      value.~T();
      has_value = false;
    }
  }
};

} // namespace brokkr::core

#define BRK_TRY(expr) do { auto _st = (expr); if (!_st.ok) return _st; } while (0)
