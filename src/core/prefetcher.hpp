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

#include "core/status.hpp"

#include <condition_variable>
#include <exception>
#include <functional> // std::move_only_function
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>

#include <spdlog/spdlog.h>

namespace brokkr::core {

template <class Slot>
class TwoSlotPrefetcher {
public:
  using InitFn = std::move_only_function<void(Slot&)>;
  using FillFn = std::move_only_function<Result<bool>(Slot&, std::stop_token)>;

  class Lease {
  public:
    Lease() = default;

    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;

    Lease(Lease&& o) noexcept : owner_(std::exchange(o.owner_, nullptr)), idx_(o.idx_) {}

    Lease& operator=(Lease&& o) noexcept {
      if (this == &o) return *this;
      release_();
      owner_ = std::exchange(o.owner_, nullptr);
      idx_ = o.idx_;
      return *this;
    }

    ~Lease() { release_(); }

    Slot& get() const noexcept { return owner_->slots_[idx_]; }
    Slot* operator->() const noexcept { return &get(); }
    Slot& operator*() const noexcept { return get(); }

  private:
    friend class TwoSlotPrefetcher;

    Lease(TwoSlotPrefetcher* owner, int idx) : owner_(owner), idx_(idx) {}

    void release_() noexcept {
      if (!owner_) return;
      owner_->release_(idx_);
      owner_ = nullptr;
    }

    TwoSlotPrefetcher* owner_ = nullptr;
    int idx_ = 0;
  };

public:
  explicit TwoSlotPrefetcher(FillFn fill, InitFn init = {})
    : init_(std::move(init)), fill_(std::move(fill))
  {
    if (init_) { init_(slots_[0]); init_(slots_[1]); }
    reader_ = std::jthread([this](std::stop_token st) { reader_loop_(st); });
  }

  ~TwoSlotPrefetcher() { request_stop(); }

  TwoSlotPrefetcher(const TwoSlotPrefetcher&) = delete;
  TwoSlotPrefetcher& operator=(const TwoSlotPrefetcher&) = delete;

  void request_stop() noexcept {
    {
      std::lock_guard lk(m_);
      stopping_ = true;
    }
    cv_can_fill_.notify_all();
    cv_can_take_.notify_all();

    reader_.request_stop();
    if (reader_.joinable()) reader_.join();
  }

  std::optional<Lease> next() noexcept {
    std::unique_lock lk(m_);
    cv_can_take_.wait(lk, [&] {
      return stopping_ || error_.has_value() || filled_[read_idx_] || (done_ && !filled_[read_idx_]);
    });

    if (stopping_ || error_.has_value() || !filled_[read_idx_]) return std::nullopt;

    const int idx = read_idx_;
    read_idx_ ^= 1;
    return Lease{this, idx};
  }

  Status status() const noexcept {
    std::lock_guard lk(m_);
    return error_ ? Status{std::unexpect, *error_} : Status{};
  }

private:
  void release_(int idx) noexcept {
    {
      std::lock_guard lk(m_);
      filled_[idx] = false;
    }
    cv_can_fill_.notify_all();
  }

  void reader_loop_(std::stop_token st) noexcept {
    try {
      for (;;) {
        {
          std::unique_lock lk(m_);
          cv_can_fill_.wait(lk, [&] { return stopping_ || !filled_[write_idx_]; });
          if (stopping_ || st.stop_requested()) { done_ = true; cv_can_take_.notify_all(); return; }
        }

        auto r = fill_(slots_[write_idx_], st);

        {
          std::lock_guard lk(m_);
          if (stopping_ || st.stop_requested()) { done_ = true; cv_can_take_.notify_all(); return; }

          if (!r) { error_ = std::move(r.error()); done_ = true; cv_can_take_.notify_all(); return; }
          if (!*r) { done_ = true; cv_can_take_.notify_all(); return; }

          filled_[write_idx_] = true;
          write_idx_ ^= 1;
        }

        cv_can_take_.notify_all();
      }
    } catch (const std::exception& e) {
      spdlog::debug("TwoSlotPrefetcher reader threw: {}", e.what());
      {
        std::lock_guard lk(m_);
        error_ = e.what();
        done_ = true;
      }
      cv_can_take_.notify_all();
      cv_can_fill_.notify_all();
    } catch (...) {
      spdlog::debug("TwoSlotPrefetcher reader threw unknown exception");
      {
        std::lock_guard lk(m_);
        error_ = "Unknown exception in TwoSlotPrefetcher reader thread";
        done_ = true;
      }
      cv_can_take_.notify_all();
      cv_can_fill_.notify_all();
    }
  }

private:
  Slot slots_[2]{};

  mutable std::mutex m_;
  std::condition_variable cv_can_fill_;
  std::condition_variable cv_can_take_;

  bool filled_[2]{false, false};
  bool done_ = false;
  bool stopping_ = false;

  int write_idx_ = 0;
  int read_idx_ = 0;

  std::optional<Error> error_{};

  std::jthread reader_{};

  InitFn init_{};
  FillFn fill_{};
};

} // namespace brokkr::core
