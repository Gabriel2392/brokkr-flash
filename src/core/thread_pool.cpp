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

#include <stdexcept>
#include <utility>

namespace brokkr::core {

ThreadPool::ThreadPool(std::size_t thread_count) {
    if (thread_count == 0) thread_count = 1;
    workers_.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i) {
        workers_.emplace_back([this] { worker_loop_(); });
    }
}

ThreadPool::~ThreadPool() {
    stop();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
}

void ThreadPool::submit(Task t) {
    if (!t) return;
    {
        std::lock_guard lk(mtx_);
        if (stopping_) throw std::runtime_error("ThreadPool: submit on stopping pool");
        q_.push(std::move(t));
    }
    cv_.notify_one();
}

void ThreadPool::stop() {
    {
        std::lock_guard lk(mtx_);
        stopping_ = true;
    }
    cv_.notify_all();
}

void ThreadPool::wait() {
    std::unique_lock lk(mtx_);
    cv_done_.wait(lk, [&] {
        return q_.empty() && active_.load(std::memory_order_relaxed) == 0;
    });
}

std::vector<std::exception_ptr> ThreadPool::take_exceptions() {
    std::lock_guard lk(ex_mtx_);
    auto out = std::move(exceptions_);
    exceptions_.clear();
    return out;
}

void ThreadPool::worker_loop_() {
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

        try {
            task();
        } catch (...) {
            std::lock_guard elk(ex_mtx_);
            exceptions_.push_back(std::current_exception());
        }

        {
            std::lock_guard lk(mtx_);
            active_.fetch_sub(1, std::memory_order_relaxed);
            if (q_.empty() && active_.load(std::memory_order_relaxed) == 0) {
                cv_done_.notify_all();
            }
        }
    }
}

} // namespace brokkr::core
