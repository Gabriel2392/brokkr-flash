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

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <vector>

namespace brokkr::core {

class ThreadPool {
public:
  using Task = std::function<void()>;

  explicit ThreadPool(std::size_t thread_count);
  ~ThreadPool();

  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;

  void submit(Task t);
  void stop();
  void wait();

  std::size_t active() const noexcept {
    return active_.load(std::memory_order_relaxed);
  }
  std::vector<std::exception_ptr> take_exceptions();

private:
  void worker_loop_();

  std::vector<std::thread> workers_;

  mutable std::mutex mtx_;
  std::condition_variable cv_;
  std::condition_variable cv_done_;

  std::queue<Task> q_;
  bool stopping_ = false;

  std::atomic<std::size_t> active_{0};

  std::mutex ex_mtx_;
  std::vector<std::exception_ptr> exceptions_;
};

} // namespace brokkr::core
