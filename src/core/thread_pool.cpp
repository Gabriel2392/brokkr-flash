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

#include "core/thread_pool.hpp"

#include <exception>
#include <utility>

#include <spdlog/spdlog.h>

namespace brokkr::core {

ThreadPool::ThreadPool(std::size_t thread_count) {
  if (thread_count == 0) thread_count = 1;
  workers_.reserve(thread_count);
  for (std::size_t i = 0; i < thread_count; ++i) workers_.emplace_back([this] { worker_loop_(); });
}

ThreadPool::~ThreadPool() {
  stop();
  for (auto& t : workers_) if (t.joinable()) t.join();
}

Status ThreadPool::submit(Task t) noexcept {
  if (!t) return Status::Ok();
  {
    std::lock_guard lk(mtx_);
    if (stopping_) return Status::Fail("ThreadPool: submit on stopping pool");
    q_.push(std::move(t));
  }
  cv_.notify_one();
  return Status::Ok();
}

void ThreadPool::stop() noexcept {
  {
    std::lock_guard lk(mtx_);
    stopping_ = true;
  }
  cv_.notify_all();
}

Status ThreadPool::wait() noexcept {
  {
    std::unique_lock lk(mtx_);
    cv_done_.wait(lk, [&] {
      return q_.empty() && active_.load(std::memory_order_relaxed) == 0;
    });
  }

  std::lock_guard elk(err_mtx_);
  return has_error_ ? first_error_ : Status::Ok();
}

void ThreadPool::set_error_(Status st) noexcept {
  if (st.ok) return;

  cancel_.store(true, std::memory_order_relaxed);

  std::lock_guard lk(err_mtx_);
  if (has_error_) return;
  has_error_ = true;
  first_error_ = std::move(st);
}

void ThreadPool::worker_loop_() noexcept {
  for (;;) {
    Task task;

    {
      std::unique_lock lk(mtx_);
      cv_.wait(lk, [&] { return stopping_ || !q_.empty(); });

      if (q_.empty()) {
        if (stopping_) return;
        continue;
      }

      task = std::move(q_.front());
      q_.pop();
      active_.fetch_add(1, std::memory_order_relaxed);
    }

    if (!cancel_.load(std::memory_order_relaxed)) {
      try {
        Status st = task();
        if (!st.ok) {
          spdlog::debug("ThreadPool task failed: {}", st.msg);
          set_error_(std::move(st));
        }
      } catch (const std::exception& e) {
        spdlog::debug("ThreadPool task threw: {}", e.what());
        set_error_(Status::Fail(e.what()));
      } catch (...) {
        spdlog::debug("ThreadPool task threw unknown exception");
        set_error_(Status::Fail("Unknown exception in ThreadPool task"));
      }
    }

    {
      std::lock_guard lk(mtx_);
      active_.fetch_sub(1, std::memory_order_relaxed);
      if (q_.empty() && active_.load(std::memory_order_relaxed) == 0) cv_done_.notify_all();
    }
  }
}

} // namespace brokkr::core
