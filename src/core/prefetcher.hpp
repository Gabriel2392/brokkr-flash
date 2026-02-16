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

#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>

namespace brokkr::core {

namespace detail {

template <class> class MoveOnlyFunction;

template <class R, class... Args> class MoveOnlyFunction<R(Args...)> {
public:
  MoveOnlyFunction() = default;

  MoveOnlyFunction(const MoveOnlyFunction &) = delete;
  MoveOnlyFunction &operator=(const MoveOnlyFunction &) = delete;

  MoveOnlyFunction(MoveOnlyFunction &&) noexcept = default;
  MoveOnlyFunction &operator=(MoveOnlyFunction &&) noexcept = default;

  template <class F>
    requires(!std::is_same_v<std::remove_cvref_t<F>, MoveOnlyFunction>)
  MoveOnlyFunction(F &&f) {
    using Fn = std::remove_cvref_t<F>;
    struct Model final : Concept {
      Fn fn;
      explicit Model(Fn &&x) : fn(std::move(x)) {}
      explicit Model(const Fn &x) : fn(x) {}
      R call(Args &&...a) override {
        return std::invoke(fn, std::forward<Args>(a)...);
      }
    };

    if constexpr (std::is_move_constructible_v<Fn>) {
      impl_ = std::make_unique<Model>(Fn(std::forward<F>(f)));
    } else {
      static_assert(std::is_copy_constructible_v<Fn>,
                    "MoveOnlyFunction target must be move-constructible or "
                    "copy-constructible");
      impl_ = std::make_unique<Model>(Fn(f));
    }
  }

  explicit operator bool() const noexcept { return static_cast<bool>(impl_); }

  R operator()(Args... args) {
    return impl_->call(std::forward<Args>(args)...);
  }

private:
  struct Concept {
    virtual ~Concept() = default;
    virtual R call(Args &&...a) = 0;
  };

  std::unique_ptr<Concept> impl_{};
};

} // namespace detail

template <class Slot> class TwoSlotPrefetcher {
public:
  using InitFn = detail::MoveOnlyFunction<void(Slot &)>;
  using FillFn = detail::MoveOnlyFunction<bool(Slot &, std::stop_token)>;

  class Lease {
  public:
    Lease() = default;

    Lease(const Lease &) = delete;
    Lease &operator=(const Lease &) = delete;

    Lease(Lease &&o) noexcept
        : owner_(std::exchange(o.owner_, nullptr)), idx_(o.idx_) {}

    Lease &operator=(Lease &&o) noexcept {
      if (this == &o)
        return *this;
      release_();
      owner_ = std::exchange(o.owner_, nullptr);
      idx_ = o.idx_;
      return *this;
    }

    ~Lease() { release_(); }

    Slot &get() const noexcept { return owner_->slots_[idx_]; }
    Slot *operator->() const noexcept { return &get(); }
    Slot &operator*() const noexcept { return get(); }

  private:
    friend class TwoSlotPrefetcher;

    Lease(TwoSlotPrefetcher *owner, int idx) : owner_(owner), idx_(idx) {}

    void release_() noexcept {
      if (!owner_)
        return;
      owner_->release_(idx_);
      owner_ = nullptr;
    }

    TwoSlotPrefetcher *owner_ = nullptr;
    int idx_ = 0;
  };

public:
  explicit TwoSlotPrefetcher(FillFn fill, InitFn init = {})
      : init_(std::move(init)), fill_(std::move(fill)) {
    if (init_) {
      init_(slots_[0]);
      init_(slots_[1]);
    }

    reader_ = std::jthread([this](std::stop_token st) { reader_loop_(st); });
  }

  ~TwoSlotPrefetcher() { request_stop(); }

  TwoSlotPrefetcher(const TwoSlotPrefetcher &) = delete;
  TwoSlotPrefetcher &operator=(const TwoSlotPrefetcher &) = delete;

  void request_stop() noexcept {
    {
      std::lock_guard lk(m_);
      stopping_ = true;
    }

    cv_can_fill_.notify_all();
    cv_can_take_.notify_all();

    reader_.request_stop();
    if (reader_.joinable())
      reader_.join();
  }

  std::optional<Lease> next() {
    std::unique_lock lk(m_);
    cv_can_take_.wait(lk, [&] {
      return stopping_ || error_ || filled_[read_idx_] ||
             (done_ && !filled_[read_idx_]);
    });

    if (error_)
      std::rethrow_exception(error_);
    if (stopping_)
      return std::nullopt;

    if (!filled_[read_idx_]) {
      return std::nullopt;
    }

    const int idx = read_idx_;
    read_idx_ ^= 1;
    return Lease{this, idx};
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
          cv_can_fill_.wait(lk,
                            [&] { return stopping_ || !filled_[write_idx_]; });
          if (stopping_ || st.stop_requested()) {
            done_ = true;
            cv_can_take_.notify_all();
            return;
          }
        }

        const bool produced = fill_(slots_[write_idx_], st);

        {
          std::lock_guard lk(m_);
          if (stopping_ || st.stop_requested()) {
            done_ = true;
            cv_can_take_.notify_all();
            return;
          }

          if (!produced) {
            done_ = true;
            cv_can_take_.notify_all();
            return;
          }

          filled_[write_idx_] = true;
          write_idx_ ^= 1;
        }

        cv_can_take_.notify_all();
      }
    } catch (...) {
      {
        std::lock_guard lk(m_);
        error_ = std::current_exception();
        done_ = true;
      }
      cv_can_take_.notify_all();
      cv_can_fill_.notify_all();
    }
  }

private:
  Slot slots_[2]{};

  std::mutex m_;
  std::condition_variable cv_can_fill_;
  std::condition_variable cv_can_take_;

  bool filled_[2]{false, false};
  bool done_ = false;
  bool stopping_ = false;

  int write_idx_ = 0;
  int read_idx_ = 0;

  std::exception_ptr error_{};

  std::jthread reader_{};

  InitFn init_{};
  FillFn fill_{};
};

} // namespace brokkr::core
