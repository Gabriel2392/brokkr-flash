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

#include "core/prefetcher.hpp"
#include "core/str.hpp"
#include "core/thread_pool.hpp"

#include "io/tar.hpp"
#include "platform/platform_all.hpp"
#include "third_party/md5/md5.h"
#include "third_party/xxhash/xxhash_vendor.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <cerrno>
  #include <fcntl.h>
  #include <unistd.h>
  #if defined(POSIX_FADV_SEQUENTIAL)
    #include <sys/types.h>
  #endif
#endif

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace brokkr::app {

namespace {

constexpr std::size_t kTrailerMaxBytes = 16 * 1024;
constexpr std::size_t kMd5HexChars = 32;
constexpr std::size_t kMd5Xxh3CacheMaxEntries = 100;
constexpr std::size_t kHashBufBytes = 32 * 1024 * 1024;

struct CombinedDigest {
  std::array<unsigned char, 16> md5{};
  std::uint64_t xxh3_64 = 0;
};

class HashFileReader {
 public:
  explicit HashFileReader(const std::filesystem::path& path) noexcept : path_(path) {}

  brokkr::core::Status open() noexcept {
#if defined(_WIN32)
    handle_ = CreateFileW(path_.c_str(), GENERIC_READ,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
      return brokkr::core::failf("Cannot open for hashing: {}", path_.string());
    }
    return {};
#else
    fd_ = ::open(path_.c_str(), O_RDONLY
#if defined(O_CLOEXEC)
                                   | O_CLOEXEC
#endif
    );
    if (fd_ < 0) return brokkr::core::failf("Cannot open for hashing: {}", path_.string());
#if defined(POSIX_FADV_SEQUENTIAL)
    (void)::posix_fadvise(fd_, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
#if defined(POSIX_FADV_WILLNEED)
    (void)::posix_fadvise(fd_, 0, 0, POSIX_FADV_WILLNEED);
#endif
    return {};
#endif
  }

  brokkr::core::Result<std::size_t> read_some(unsigned char* data, std::size_t want) noexcept {
#if defined(_WIN32)
    std::size_t total = 0;
    while (total < want) {
      const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(want - total, static_cast<std::size_t>(1u << 30)));
      DWORD got = 0;
      if (!ReadFile(handle_, data + total, chunk, &got, nullptr)) {
        return brokkr::core::failf("Read failed while hashing: {}", path_.string());
      }
      if (got == 0) break;
      total += static_cast<std::size_t>(got);
    }
    return total;
#else
    std::size_t total = 0;
    while (total < want) {
      const ssize_t got = ::read(fd_, data + total, want - total);
      if (got < 0) {
        if (errno == EINTR) continue;
        return brokkr::core::failf("Read failed while hashing: {}", path_.string());
      }
      if (got == 0) break;
      total += static_cast<std::size_t>(got);
    }
    return total;
#endif
  }

  ~HashFileReader() {
#if defined(_WIN32)
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
    if (fd_ >= 0) ::close(fd_);
#endif
  }

 private:
  std::filesystem::path path_;
#if defined(_WIN32)
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
  int fd_ = -1;
#endif
};

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

static bool is_md5_wrapped_tar_name(const std::filesystem::path& path) noexcept {
  return brokkr::core::ends_with_ci(path.filename().string(), ".md5");
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
    return {};
  }

  brokkr::core::Status update(const unsigned char* data, std::size_t size) noexcept {
    md5_update(&md5_, data, size);
    if (XXH3_64bits_update(state_, data, size) != XXH_OK) return brokkr::core::fail("XXH3 update failed");
    return {};
  }

  Digest finish() noexcept {
    Digest out;
    md5_final(&md5_, out.md5.data());
    out.xxh3_64 = XXH3_64bits_digest(state_);
    return out;
  }

  ~Md5Xxh3Consumer() {
    if (state_) XXH3_freeState(state_);
  }

  MD5_CTX md5_{};
  XXH3_state_t* state_ = nullptr;
};

template <class Consumer>
static brokkr::core::Result<typename Consumer::Digest> hash_prefetch(const std::filesystem::path& path,
                                                                     std::uint64_t bytes_to_hash,
                                                                     std::atomic_uint64_t& done,
                                                                     std::uint64_t total,
                                                                     const brokkr::odin::Ui& ui) noexcept {
  struct Slot {
    std::vector<unsigned char> buf;
    std::size_t n = 0;
  };

  HashFileReader reader(path);
  BRK_TRY(reader.open());

  std::uint64_t remaining = bytes_to_hash;

  brokkr::core::TwoSlotPrefetcher<Slot> pf(
      [&](Slot& s, std::stop_token st) -> brokkr::core::Result<bool> {
        if (st.stop_requested() || !remaining) return false;

        const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, kHashBufBytes));
        BRK_TRYV(got, reader.read_some(s.buf.data(), want));
        if (got != want) return brokkr::core::failf("Short read while hashing: {}", path.string());

        s.n = got;
        remaining -= static_cast<std::uint64_t>(got);
        return true;
      },
      [&](Slot& s) { s.buf.resize(kHashBufBytes); });

  Consumer consumer;
  if constexpr (Consumer::kUsesMd5) {
    spdlog::debug("MD5 start: {} bytes from {}", bytes_to_hash, path.string());
  }
  if constexpr (Consumer::kUsesXxh3) {
    spdlog::debug("XXH3 start: {} bytes from {}", bytes_to_hash, path.string());
  }
  BRK_TRY(consumer.init());

  std::uint64_t processed = 0;
  while (processed < bytes_to_hash) {
    auto lease = pf.next();
    if (!lease) break;

    auto& s = lease->get();
    if (!s.n) break;

    BRK_TRY(consumer.update(s.buf.data(), s.n));
    processed += static_cast<std::uint64_t>(s.n);

    const auto new_done = done.fetch_add(static_cast<std::uint64_t>(s.n), std::memory_order_relaxed) +
                          static_cast<std::uint64_t>(s.n);
    if (ui.on_progress) ui.on_progress(new_done, total, new_done, total);
  }

  auto pst = pf.status();
  if (!pst) return brokkr::core::fail(std::move(pst.error()));

  if (processed != bytes_to_hash) {
    return brokkr::core::failf("Hashing terminated early: {} (processed {}, expected {})", path.string(), processed,
                               bytes_to_hash);
  }

  auto digest = consumer.finish();
  if constexpr (Consumer::kUsesMd5) {
    spdlog::debug("MD5 finish: {} bytes from {}", bytes_to_hash, path.string());
  }
  if constexpr (Consumer::kUsesXxh3) {
    spdlog::debug("XXH3 finish: {} bytes from {}", bytes_to_hash, path.string());
  }
  return digest;
}

static brokkr::core::Result<std::optional<Md5Job>> detect_md5_job(const std::filesystem::path& p) noexcept {
  std::error_code ec;
  const std::uint64_t file_size = std::filesystem::file_size(p, ec);
  if (ec) return brokkr::core::failf("Cannot stat file: {}", p.string());
  if (file_size < (kMd5HexChars + 2)) return std::nullopt;

  const std::uint64_t tail_off = (file_size > kTrailerMaxBytes) ? (file_size - kTrailerMaxBytes) : 0;
  const std::size_t tail_len = static_cast<std::size_t>(file_size - tail_off);

  std::ifstream in(p, std::ios::binary);
  if (!in.is_open()) return brokkr::core::failf("Cannot open for MD5: {}", p.string());

  std::string tail(tail_len, '\0');
  in.seekg(static_cast<std::streamoff>(tail_off), std::ios::beg);
  if (!in.good()) return brokkr::core::failf("Seek failed: {}", p.string());
  in.read(tail.data(), static_cast<std::streamsize>(tail.size()));
  if (!in.good()) return brokkr::core::failf("Read failed: {}", p.string());

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

  if (file_size - bytes_to_hash > kTrailerMaxBytes) return brokkr::core::failf("MD5 trailer too large: {}", p.string());

  Md5Job j;
  j.path = p;
  j.bytes_to_hash = bytes_to_hash;
  j.expected = expected;
  return std::optional<Md5Job>(std::move(j));
}

} // namespace

brokkr::core::Result<std::vector<Md5Job>> md5_jobs(const std::vector<std::filesystem::path>& inputs) noexcept {
  std::vector<Md5Job> jobs;

  for (const auto& p : inputs) {
    if (!is_md5_wrapped_tar_name(p)) continue;
    if (!brokkr::io::TarArchive::is_tar_file(p.string())) continue;

    auto r = detect_md5_job(p);
    if (!r) return brokkr::core::fail(std::move(r.error()));
    if (*r) jobs.push_back(std::move(**r));
  }

  return jobs;
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

brokkr::core::Status md5_verify(const std::vector<Md5Job>& jobs, const brokkr::odin::Ui& ui) noexcept {
  if (jobs.empty()) return {};

  std::uint64_t total = 0;
  for (const auto& j : jobs) total += j.bytes_to_hash;

  const std::size_t threads = std::min<std::size_t>(jobs.size(),
                                                    std::max<std::size_t>(1, std::thread::hardware_concurrency()));

  brokkr::core::ThreadPool pool(threads);
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
      spdlog::warn("MD5/XXH3 cache load failed ({}): {}", cache_file.string(), loaded.error());
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
      if (!cache_st) spdlog::warn("MD5/XXH3 cache save failed ({}): {}", cache_file.string(), cache_st.error());
    }
  };

  for (const auto& j : jobs) {
    auto st = pool.submit([&, j]() -> brokkr::core::Status {
      if (pool.cancelled()) return {};

      std::optional<std::uint64_t> cached_xxh3;
      if (cache_enabled) {
        std::lock_guard lk(cache_mtx);
        cached_xxh3 = lookup_md5_xxh3_cache(cache_entries, j.expected, j.bytes_to_hash);
        if (cached_xxh3) cache_dirty = true;
      }

      if (cached_xxh3) {
        auto xxh3 = hash_prefetch<Xxh3Consumer>(j.path, j.bytes_to_hash, done, total, ui);
        if (!xxh3) {
          if (cache_enabled) {
            std::lock_guard lk(cache_mtx);
            if (forget_md5_xxh3_cache(cache_entries, j.expected, j.bytes_to_hash)) {
              cache_dirty = true;
              spdlog::warn("Removed MD5/XXH3 cache entry after XXH3 failure: {}", j.path.string());
            }
          }
          return brokkr::core::fail(std::move(xxh3.error()));
        }

        if (*xxh3 == *cached_xxh3) {
          spdlog::debug("MD5/XXH3 cache hit: {}", j.path.string());
          return {};
        }

        spdlog::warn("MD5/XXH3 cache mismatch, falling back to full MD5");

        if (ui.on_stage) ui.on_stage("Checking package MD5");
        if (ui.on_progress) ui.on_progress(0, j.bytes_to_hash, 0, j.bytes_to_hash);

        std::atomic_uint64_t retry_done{0};
        auto retry_ui = ui;

        auto retry = hash_prefetch<Md5Xxh3Consumer>(j.path, j.bytes_to_hash, retry_done, j.bytes_to_hash, retry_ui);
        if (!retry) return brokkr::core::fail(std::move(retry.error()));

        if (std::memcmp(retry->md5.data(), j.expected.data(), j.expected.size()) != 0) {
          return brokkr::core::fail("MD5 mismatch: " + j.path.string() + "\n  expected:   " + md5_hex32(j.expected) +
                                    "\n  calculated: " + md5_hex32(retry->md5) +
                                    "\n  byte count: " + std::to_string(j.bytes_to_hash));
        }

        if (cache_enabled) {
          std::lock_guard lk(cache_mtx);
          remember_md5_xxh3_cache(cache_entries, j.expected, j.bytes_to_hash, retry->xxh3_64,
                                  kMd5Xxh3CacheMaxEntries);
          cache_dirty = true;
        }
        return {};
      }

      auto digest = hash_prefetch<Md5Xxh3Consumer>(j.path, j.bytes_to_hash, done, total, ui);
      if (!digest) return brokkr::core::fail(std::move(digest.error()));

      if (std::memcmp(digest->md5.data(), j.expected.data(), j.expected.size()) != 0) {
        return brokkr::core::fail("MD5 mismatch: " + j.path.string() + "\n  expected:   " + md5_hex32(j.expected) +
                                  "\n  calculated: " + md5_hex32(digest->md5) +
                                  "\n  byte count: " + std::to_string(j.bytes_to_hash));
      }

      if (cache_enabled) {
        std::lock_guard lk(cache_mtx);
        remember_md5_xxh3_cache(cache_entries, j.expected, j.bytes_to_hash, digest->xxh3_64,
                                kMd5Xxh3CacheMaxEntries);
        cache_dirty = true;
      }

      return {};
    });

    if (!st) return st;
  }

  auto wst = pool.wait();
  persist_cache_if_needed();
  if (!wst) return wst;

  if (ui.on_item_done) ui.on_item_done(0);
  spdlog::info("{} OK", verify_name);
  return {};
}

} // namespace brokkr::app
