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

#include "io/sequential_reader.hpp"

#include "core/thread_priority.hpp"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace brokkr::io {

namespace {

class ParallelReader final : public SequentialReader {
  static constexpr std::size_t kSlots = 8;
  static constexpr std::size_t kWorkers = 8;

  enum class State { Free, Reading, Ready };

  struct Slot {
    std::vector<std::byte> buf;
    std::size_t len = 0;
    State state = State::Free;
  };

 public:
  ParallelReader(RandomAccessSourcePtr source, std::uint64_t bytes, std::size_t chunk)
      : source_(std::move(source)), bytes_(bytes), chunk_(chunk), chunks_((bytes + chunk - 1) / chunk) {
    for (auto& s : slots_) s.buf.resize(chunk_);
  }

  ~ParallelReader() override {
    {
      std::lock_guard lk(m_);
      stop_ = true;
    }
    cv_.notify_all();
    for (auto& t : workers_)
      if (t.joinable()) t.join();
  }

  void start() {
    if (chunks_ == 0) return;
    source_->advise_sequential();
    const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(kWorkers, chunks_));
    for (std::size_t i = 0; i < n; ++i) workers_.emplace_back([this] { work_(); });
  }

  brokkr::core::Result<std::span<const std::byte>> next() noexcept override {
    std::unique_lock lk(m_);
    if (holding_) {
      slots_[cur_ % kSlots].state = State::Free;
      holding_ = false;
      ++cur_;
      cv_.notify_all();
    }
    if (cur_ >= chunks_) return std::span<const std::byte>{};

    Slot& s = slots_[cur_ % kSlots];
    cv_.wait(lk, [&] { return s.state == State::Ready || failed_; });
    if (s.state != State::Ready) return brokkr::core::fail(error_);
    holding_ = true;
    return std::span<const std::byte>(s.buf.data(), s.len);
  }

 private:
  void work_() noexcept {
    brokkr::core::bump_thread_priority(-8);
    std::unique_lock lk(m_);
    for (;;) {
      cv_.wait(lk,
               [&] { return stop_ || failed_ || issued_ >= chunks_ || slots_[issued_ % kSlots].state == State::Free; });
      if (stop_ || failed_ || issued_ >= chunks_) return;

      const std::uint64_t c = issued_++;
      Slot& s = slots_[c % kSlots];
      s.state = State::Reading;
      lk.unlock();

      const std::uint64_t off = c * chunk_;
      const auto len = static_cast<std::size_t>(std::min<std::uint64_t>(chunk_, bytes_ - off));
      auto st = source_->read_exact_at(off, std::span<std::byte>(s.buf.data(), len));

      lk.lock();
      if (!st) {
        failed_ = true;
        error_ = std::move(st.error());
        cv_.notify_all();
        return;
      }
      s.len = len;
      s.state = State::Ready;
      cv_.notify_all();
    }
  }

  RandomAccessSourcePtr source_;
  std::uint64_t bytes_ = 0;
  std::size_t chunk_ = 0;
  std::uint64_t chunks_ = 0;

  std::array<Slot, kSlots> slots_{};
  std::mutex m_;
  std::condition_variable cv_;
  bool stop_ = false;
  bool failed_ = false;
  std::string error_;
  std::uint64_t issued_ = 0;

  std::uint64_t cur_ = 0;
  bool holding_ = false;

  std::vector<std::thread> workers_;
};

} // namespace

brokkr::core::Result<std::unique_ptr<SequentialReader>> open_sequential_reader(RandomAccessSourcePtr source,
                                                                               std::uint64_t bytes,
                                                                               std::size_t chunk_bytes) noexcept {
  if (!source) return brokkr::core::fail("open_sequential_reader: no source");
  if (chunk_bytes == 0) return brokkr::core::fail("open_sequential_reader: zero chunk size");
  if (bytes > source->size()) return brokkr::core::failf("Read past end: {}", source->label());

  try {
    auto r = std::make_unique<ParallelReader>(std::move(source), bytes, chunk_bytes);
    r->start();
    return std::unique_ptr<SequentialReader>(std::move(r));
  } catch (...) {
    return brokkr::core::fail("open_sequential_reader: cannot start reader");
  }
}

} // namespace brokkr::io
