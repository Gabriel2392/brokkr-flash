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

#include "app/md5_verify.hpp"

#include "app/md5_xxh3_cache.hpp"

#include "core/path_utf8.hpp"
#include "core/str.hpp"
#include "core/thread_pool.hpp"
#include "core/thread_priority.hpp"

#include "io/sequential_reader.hpp"
#include "io/source.hpp"
#include "io/tar.hpp"
#include "platform/platform_all.hpp"
#include "third_party/md5/md5.h"
#include "third_party/xxhash/xxhash_vendor.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <semaphore>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace brokkr::app {

namespace {

constexpr std::size_t kTrailerMaxBytes = 16 * 1024;
constexpr std::size_t kMd5HexChars = 32;
constexpr std::size_t kMd5Xxh3CacheMaxEntries = 100;

constexpr std::size_t kHashBufBytes = 4 * 1024 * 1024;

#if defined(BROKKR_PLATFORM_ANDROID)
constexpr std::size_t kMaxVerifyThreads = 2;
#else
constexpr std::size_t kMaxVerifyThreads = std::numeric_limits<std::size_t>::max();
#endif

struct CombinedDigest {
  std::array<unsigned char, 16> md5{};
  std::uint64_t xxh3_64 = 0;
};

struct SessionVerifyKey {
  std::string identity_path;
  std::uint64_t identity_size = 0;
  std::int64_t identity_write_time = 0;
  std::uint64_t bytes_to_hash = 0;
  std::array<unsigned char, 16> expected{};

  bool operator==(const SessionVerifyKey& other) const noexcept {
    return identity_path == other.identity_path && identity_size == other.identity_size &&
           identity_write_time == other.identity_write_time && bytes_to_hash == other.bytes_to_hash &&
           expected == other.expected;
  }
};

struct SessionVerifyKeyHash {
  std::size_t operator()(const SessionVerifyKey& key) const noexcept {
    auto hash_combine = [](std::size_t seed, std::size_t value) noexcept {
      return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
    };

    std::size_t seed = std::hash<std::string>{}(key.identity_path);
    seed = hash_combine(seed, std::hash<std::uint64_t>{}(key.identity_size));
    seed = hash_combine(seed, std::hash<std::int64_t>{}(key.identity_write_time));
    seed = hash_combine(seed, std::hash<std::uint64_t>{}(key.bytes_to_hash));
    for (unsigned char b : key.expected) seed = hash_combine(seed, std::hash<unsigned int>{}(b));
    return seed;
  }
};

std::unordered_set<SessionVerifyKey, SessionVerifyKeyHash>& session_verify_cache() {
  static std::unordered_set<SessionVerifyKey, SessionVerifyKeyHash> cache;
  return cache;
}

std::mutex& session_verify_cache_mutex() {
  static std::mutex mtx;
  return mtx;
}

static bool is_hex(unsigned char c) noexcept {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int hex_nibble(unsigned char c) noexcept {
  if (c >= '0' && c <= '9') return (c - '0');
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

static bool parse_md5_hex(std::string_view hex32, std::array<unsigned char, 16>& out) noexcept {
  if (hex32.size() != kMd5HexChars) return false;
  for (std::size_t i = 0; i < out.size(); ++i) {
    const int hi = hex_nibble(static_cast<unsigned char>(hex32[2 * i + 0]));
    const int lo = hex_nibble(static_cast<unsigned char>(hex32[2 * i + 1]));
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<unsigned char>((hi << 4) | lo);
  }
  return true;
}

static bool is_md5_wrapped_tar_name(std::string_view label) noexcept {
  return brokkr::core::ends_with_ci(brokkr::io::basename(label), ".md5");
}

static SessionVerifyKey make_session_verify_key(const Md5Job& job) {
  SessionVerifyKey key;
  key.identity_path = job.identity_path;
  key.identity_size = job.identity_size;
  key.identity_write_time = job.identity_write_time;
  key.bytes_to_hash = job.bytes_to_hash;
  key.expected = job.expected;
  return key;
}

static bool session_verify_cache_contains(const Md5Job& job) {
  const auto key = make_session_verify_key(job);
  std::lock_guard lk(session_verify_cache_mutex());
  return session_verify_cache().contains(key);
}

static void remember_session_verify_cache(const Md5Job& job) {
  std::lock_guard lk(session_verify_cache_mutex());
  session_verify_cache().insert(make_session_verify_key(job));
}

struct Xxh3Consumer {
  using Digest = std::uint64_t;
  static constexpr bool kUsesMd5 = false;
  static constexpr bool kUsesXxh3 = true;

  brokkr::core::Status init() noexcept {
    state_ = XXH3_createState();
    if (!state_) return brokkr::core::fail("Cannot allocate XXH3 state");
    if (XXH3_64bits_reset(state_) != XXH_OK) return brokkr::core::fail("Cannot reset XXH3 state");
    return {};
  }

  brokkr::core::Status update(const unsigned char* data, std::size_t size) noexcept {
    if (XXH3_64bits_update(state_, data, size) != XXH_OK) return brokkr::core::fail("XXH3 update failed");
    return {};
  }

  Digest finish() noexcept { return XXH3_64bits_digest(state_); }

  ~Xxh3Consumer() {
    if (state_) XXH3_freeState(state_);
  }

  XXH3_state_t* state_ = nullptr;
};

struct Md5Xxh3Consumer {
  using Digest = CombinedDigest;
  static constexpr bool kUsesMd5 = true;
  static constexpr bool kUsesXxh3 = true;

  brokkr::core::Status init() noexcept {
    md5_init(&md5_);
    state_ = XXH3_createState();
    if (!state_) return brokkr::core::fail("Cannot allocate XXH3 state");
    if (XXH3_64bits_reset(state_) != XXH_OK) return brokkr::core::fail("Cannot reset XXH3 state");
    try {
      xxh3_thread_ = std::thread([this] {
        brokkr::core::bump_thread_priority(-4);
        for (;;) {
          go_.acquire();
          if (stop_) return;
          xxh3_ok_ = XXH3_64bits_update(state_, job_data_, job_size_) == XXH_OK;
          done_.release();
        }
      });
    } catch (...) {
    }
    return {};
  }

  brokkr::core::Status update(const unsigned char* data, std::size_t size) noexcept {
    if (xxh3_thread_.joinable()) {
      job_data_ = data;
      job_size_ = size;
      go_.release();
      md5_update(&md5_, data, size);
      done_.acquire();
    } else {
      md5_update(&md5_, data, size);
      xxh3_ok_ = XXH3_64bits_update(state_, data, size) == XXH_OK;
    }
    if (!xxh3_ok_) return brokkr::core::fail("XXH3 update failed");
    return {};
  }

  Digest finish() noexcept {
    Digest out;
    md5_final(&md5_, out.md5.data());
    out.xxh3_64 = XXH3_64bits_digest(state_);
    return out;
  }

  ~Md5Xxh3Consumer() {
    if (xxh3_thread_.joinable()) {
      stop_ = true;
      go_.release();
      xxh3_thread_.join();
    }
    if (state_) XXH3_freeState(state_);
  }

  MD5_CTX md5_{};
  XXH3_state_t* state_ = nullptr;

  std::binary_semaphore go_{0};
  std::binary_semaphore done_{0};
  const unsigned char* job_data_ = nullptr;
  std::size_t job_size_ = 0;
  bool xxh3_ok_ = true;
  bool stop_ = false;
  std::thread xxh3_thread_;
};

template <class Consumer>
static brokkr::core::Result<typename Consumer::Digest> hash_prefetch(const Md5Job& job, std::uint64_t bytes_to_hash,
                                                                     std::atomic_uint64_t& done, std::uint64_t total,
                                                                     const brokkr::odin::Ui& ui,
                                                                     std::atomic<bool>* cancel) noexcept {
  auto cancelled = [&]() noexcept { return cancel != nullptr && cancel->load(std::memory_order_acquire); };

  if (cancelled()) return brokkr::core::fail("Verification cancelled");

  if (!job.source) return brokkr::core::fail("Hash source is not configured");

  BRK_TRYV(reader, brokkr::io::open_sequential_reader(job.source, bytes_to_hash, kHashBufBytes));

  Consumer consumer;
  if constexpr (Consumer::kUsesMd5) {
    spdlog::debug("MD5 start: {} bytes from {}", bytes_to_hash, job.display_name);
  }
  if constexpr (Consumer::kUsesXxh3) {
    spdlog::debug("XXH3 start: {} bytes from {}", bytes_to_hash, job.display_name);
  }
  BRK_TRY(consumer.init());

  std::uint64_t processed = 0;
  while (processed < bytes_to_hash) {
    if (cancelled()) return brokkr::core::fail("Verification cancelled");

    BRK_TRYV(chunk, reader->next());
    if (chunk.empty()) break;

    BRK_TRY(consumer.update(reinterpret_cast<const unsigned char*>(chunk.data()), chunk.size()));
    processed += static_cast<std::uint64_t>(chunk.size());

    const auto new_done = done.fetch_add(static_cast<std::uint64_t>(chunk.size()), std::memory_order_relaxed) +
                          static_cast<std::uint64_t>(chunk.size());
    if (ui.on_progress) ui.on_progress(new_done, total, new_done, total);
  }

  if (processed != bytes_to_hash) {
    return brokkr::core::failf("Hashing terminated early: {} (processed {}, expected {})", job.display_name, processed,
                               bytes_to_hash);
  }

  auto digest = consumer.finish();
  if constexpr (Consumer::kUsesMd5) {
    spdlog::debug("MD5 finish: {} bytes from {}", bytes_to_hash, job.display_name);
  }
  if constexpr (Consumer::kUsesXxh3) {
    spdlog::debug("XXH3 finish: {} bytes from {}", bytes_to_hash, job.display_name);
  }
  return digest;
}

static brokkr::core::Result<std::optional<Md5Job>> detect_md5_job(
    const brokkr::io::RandomAccessSourcePtr& source) noexcept {
  const std::uint64_t file_size = source->size();
  if (file_size < (kMd5HexChars + 2)) return std::nullopt;

  const std::uint64_t tail_off = (file_size > kTrailerMaxBytes) ? (file_size - kTrailerMaxBytes) : 0;
  const std::size_t tail_len = static_cast<std::size_t>(file_size - tail_off);

  std::string tail(tail_len, '\0');
  BRK_TRY(source->read_exact_at(tail_off, std::as_writable_bytes(std::span<char>(tail.data(), tail.size()))));

  std::int64_t delim = -1;
  for (std::int64_t i = static_cast<std::int64_t>(tail.size()) - 2; i >= 0; --i) {
    if (tail[static_cast<std::size_t>(i)] != ' ' || tail[static_cast<std::size_t>(i) + 1] != ' ') continue;
    const std::int64_t start = i - static_cast<std::int64_t>(kMd5HexChars);
    if (start < 0) continue;

    bool ok = true;
    for (std::size_t j = 0; j < kMd5HexChars; ++j) {
      if (!is_hex(static_cast<unsigned char>(tail[static_cast<std::size_t>(start) + j]))) {
        ok = false;
        break;
      }
    }
    if (ok) {
      delim = i;
      break;
    }
  }
  if (delim < 0) return std::nullopt;

  std::array<unsigned char, 16> expected{};
  if (!parse_md5_hex({tail.data() + static_cast<std::size_t>(delim - static_cast<std::int64_t>(kMd5HexChars)),
                      kMd5HexChars},
                     expected)) {
    return std::nullopt;
  }

  const std::uint64_t bytes_to_hash = tail_off +
                                      static_cast<std::uint64_t>(delim - static_cast<std::int64_t>(kMd5HexChars));

  if (file_size - bytes_to_hash > kTrailerMaxBytes) {
    return brokkr::core::failf("MD5 trailer too large: {}", source->label());
  }

  Md5Job j;
  j.display_name = source->label();
  j.identity_path = source->identity();
  j.identity_size = file_size;
  j.identity_write_time = source->write_time();
  j.bytes_to_hash = bytes_to_hash;
  j.expected = expected;
  j.source = source;
  return std::optional<Md5Job>(std::move(j));
}

} // namespace

brokkr::core::Result<std::vector<Md5Job>> md5_jobs_from_sources(
    const std::vector<brokkr::io::RandomAccessSourcePtr>& inputs) noexcept {
  std::vector<Md5Job> jobs;

  for (const auto& source : inputs) {
    if (!is_md5_wrapped_tar_name(source->label())) continue;
    if (!brokkr::io::TarArchive::is_tar_file(*source)) continue;

    auto r = detect_md5_job(source);
    if (!r) return brokkr::core::fail(std::move(r.error()));
    if (*r) jobs.push_back(std::move(**r));
  }

  return jobs;
}

brokkr::core::Result<std::vector<Md5Job>> md5_jobs(const std::vector<std::filesystem::path>& inputs) noexcept {
  std::vector<brokkr::io::RandomAccessSourcePtr> sources;
  sources.reserve(inputs.size());

  for (const auto& p : inputs) {
    if (!is_md5_wrapped_tar_name(brokkr::core::path_to_utf8(p))) continue;

    BRK_TRYV(source, brokkr::io::open_file_source(p));
    sources.push_back(std::move(source));
  }

  return md5_jobs_from_sources(sources);
}

std::string_view md5_verify_name(const std::vector<Md5Job>& jobs) noexcept {
  if (jobs.empty()) return "MD5";

  auto cache_dir = brokkr::platform::app_cache_dir();
  if (!cache_dir) return "MD5";

  auto loaded = load_md5_xxh3_cache(md5_xxh3_cache_file(*cache_dir));
  if (!loaded) return "MD5";

  const auto& cache_entries = *loaded;
  const bool all_jobs_cached = std::all_of(jobs.begin(), jobs.end(), [&](const Md5Job& job) {
    return std::any_of(cache_entries.begin(), cache_entries.end(), [&](const Md5Xxh3CacheEntry& entry) {
      return entry.md5 == job.expected && entry.bytes_to_hash == job.bytes_to_hash;
    });
  });
  return all_jobs_cached ? "XXH3" : "MD5";
}

brokkr::core::Status md5_verify(const std::vector<Md5Job>& jobs, const brokkr::odin::Ui& ui,
                                std::atomic<bool>* cancel) noexcept {
  if (jobs.empty()) return {};

  auto cancelled = [&]() noexcept { return cancel != nullptr && cancel->load(std::memory_order_acquire); };

  std::uint64_t total = 0;
  for (const auto& j : jobs) total += j.bytes_to_hash;
  std::atomic_uint64_t done{0};

  std::filesystem::path cache_file;
  std::vector<Md5Xxh3CacheEntry> cache_entries;
  std::mutex cache_mtx;
  bool cache_enabled = false;
  bool cache_dirty = false;

  auto cache_dir = brokkr::platform::app_cache_dir();
  if (!cache_dir) {
    spdlog::warn("MD5/XXH3 cache disabled: {}", cache_dir.error());
  } else {
    cache_file = md5_xxh3_cache_file(*cache_dir);
    auto loaded = load_md5_xxh3_cache(cache_file);
    if (!loaded) {
      spdlog::warn("MD5/XXH3 cache load failed ({}): {}", brokkr::core::path_to_utf8(cache_file), loaded.error());
    } else {
      cache_entries = std::move(*loaded);
      cache_enabled = true;
    }
  }

  const std::string_view verify_name = md5_verify_name(jobs);

  if (ui.on_stage) ui.on_stage(fmt::format("Checking package {}", verify_name));
  spdlog::debug("Checking {} on {} package(s), {} bytes total", verify_name, jobs.size(), total);

  if (ui.on_plan) {
    brokkr::odin::PlanItem pi;
    pi.kind = brokkr::odin::PlanItem::Kind::Part;
    pi.part_id = 0;
    pi.dev_type = 0;
    pi.part_name = verify_name;
    pi.source_base = fmt::format("{} package(s)", jobs.size());
    pi.size = total;
    ui.on_plan({std::move(pi)}, total);
  }
  if (ui.on_item_active) ui.on_item_active(0);
  if (ui.on_progress) ui.on_progress(0, total, 0, total);

  std::vector<Md5Job> pending_jobs;
  pending_jobs.reserve(jobs.size());
  for (const auto& j : jobs) {
    if (session_verify_cache_contains(j)) {
      const auto new_done = done.fetch_add(j.bytes_to_hash, std::memory_order_relaxed) + j.bytes_to_hash;
      if (ui.on_progress) ui.on_progress(new_done, total, new_done, total);
      spdlog::debug("Session verify cache hit: {}", j.display_name);
      continue;
    }
    pending_jobs.push_back(j);
  }

  if (pending_jobs.empty()) {
    if (ui.on_item_done) ui.on_item_done(0);
    spdlog::info("{} OK", verify_name);
    return {};
  }

  const std::size_t threads =
      std::min<std::size_t>(pending_jobs.size(),
                            std::min<std::size_t>(kMaxVerifyThreads,
                                                  std::max<std::size_t>(1, std::thread::hardware_concurrency())));

  brokkr::core::ThreadPool pool(threads);

  auto persist_cache_if_needed = [&]() noexcept {
    std::vector<Md5Xxh3CacheEntry> cache_snapshot;
    bool should_save_cache = false;
    if (cache_enabled) {
      std::lock_guard lk(cache_mtx);
      should_save_cache = cache_dirty;
      if (should_save_cache) cache_snapshot = cache_entries;
    }

    if (should_save_cache) {
      auto cache_st = save_md5_xxh3_cache(cache_file, std::move(cache_snapshot), kMd5Xxh3CacheMaxEntries);
      if (!cache_st) spdlog::warn("MD5/XXH3 cache save failed ({}): {}", brokkr::core::path_to_utf8(cache_file), cache_st.error());
    }
  };

  for (const auto& j : pending_jobs) {
    auto st = pool.submit([&, j]() -> brokkr::core::Status {
      if (pool.cancelled()) return {};
      if (cancelled()) return brokkr::core::fail("Verification cancelled");

      std::optional<std::uint64_t> cached_xxh3;
      if (cache_enabled) {
        std::lock_guard lk(cache_mtx);
        cached_xxh3 = lookup_md5_xxh3_cache(cache_entries, j.expected, j.bytes_to_hash);
        if (cached_xxh3) cache_dirty = true;
      }

      if (cached_xxh3) {
        auto xxh3 = hash_prefetch<Xxh3Consumer>(j, j.bytes_to_hash, done, total, ui, cancel);
        if (!xxh3) {
          if (cache_enabled) {
            std::lock_guard lk(cache_mtx);
            if (forget_md5_xxh3_cache(cache_entries, j.expected, j.bytes_to_hash)) {
              cache_dirty = true;
              spdlog::warn("Removed MD5/XXH3 cache entry after XXH3 failure: {}", j.display_name);
            }
          }
          return brokkr::core::fail(std::move(xxh3.error()));
        }

        if (*xxh3 == *cached_xxh3) {
          spdlog::debug("MD5/XXH3 cache hit: {}", j.display_name);
          if (cancelled()) return brokkr::core::fail("Verification cancelled");
          remember_session_verify_cache(j);
          return {};
        }

        spdlog::warn("MD5/XXH3 cache mismatch, falling back to full MD5");

        if (ui.on_stage) ui.on_stage("Checking package MD5");
        if (ui.on_progress) ui.on_progress(0, j.bytes_to_hash, 0, j.bytes_to_hash);

        std::atomic_uint64_t retry_done{0};
        auto retry_ui = ui;

        auto retry = hash_prefetch<Md5Xxh3Consumer>(j, j.bytes_to_hash, retry_done, j.bytes_to_hash, retry_ui, cancel);
        if (!retry) return brokkr::core::fail(std::move(retry.error()));

        if (std::memcmp(retry->md5.data(), j.expected.data(), j.expected.size()) != 0) {
          return brokkr::core::fail("MD5 mismatch: " + j.display_name + "\n  expected:   " + md5_hex32(j.expected) +
                                    "\n  calculated: " + md5_hex32(retry->md5) +
                                    "\n  byte count: " + std::to_string(j.bytes_to_hash));
        }

        if (cache_enabled) {
          std::lock_guard lk(cache_mtx);
          remember_md5_xxh3_cache(cache_entries, j.expected, j.bytes_to_hash, retry->xxh3_64,
                                  kMd5Xxh3CacheMaxEntries);
          cache_dirty = true;
        }
        if (cancelled()) return brokkr::core::fail("Verification cancelled");
        remember_session_verify_cache(j);
        return {};
      }

      auto digest = hash_prefetch<Md5Xxh3Consumer>(j, j.bytes_to_hash, done, total, ui, cancel);
      if (!digest) return brokkr::core::fail(std::move(digest.error()));

      if (std::memcmp(digest->md5.data(), j.expected.data(), j.expected.size()) != 0) {
        return brokkr::core::fail("MD5 mismatch: " + j.display_name + "\n  expected:   " + md5_hex32(j.expected) +
                                  "\n  calculated: " + md5_hex32(digest->md5) +
                                  "\n  byte count: " + std::to_string(j.bytes_to_hash));
      }

      if (cache_enabled) {
        std::lock_guard lk(cache_mtx);
        remember_md5_xxh3_cache(cache_entries, j.expected, j.bytes_to_hash, digest->xxh3_64,
                                kMd5Xxh3CacheMaxEntries);
        cache_dirty = true;
      }

      if (cancelled()) return brokkr::core::fail("Verification cancelled");
      remember_session_verify_cache(j);

      return {};
    });

    if (!st) return st;
  }

  auto wst = pool.wait();
  persist_cache_if_needed();
  if (cancelled()) return brokkr::core::fail("Verification cancelled");
  if (!wst) return wst;

  if (ui.on_item_done) ui.on_item_done(0);
  spdlog::info("{} OK", verify_name);
  return {};
}

void clear_session_verify_cache() noexcept {
  std::lock_guard lk(session_verify_cache_mutex());
  session_verify_cache().clear();
}

} // namespace brokkr::app
