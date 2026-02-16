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

#include "core/prefetcher.hpp"
#include "core/thread_pool.hpp"

#include "crypto/md5.h"
#include "io/tar.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <spdlog/spdlog.h>

namespace brokkr::app {

namespace {

constexpr std::size_t kTrailerMaxBytes = 16 * 1024;
constexpr std::size_t kMd5HexChars = 32;

static bool is_hex(unsigned char c) noexcept {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

static int hex_nibble(unsigned char c) noexcept {
  if (c >= '0' && c <= '9')
    return (c - '0');
  if (c >= 'a' && c <= 'f')
    return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F')
    return 10 + (c - 'A');
  return -1;
}

static bool parse_md5_hex(std::string_view hex32,
                          std::array<unsigned char, 16> &out) noexcept {
  if (hex32.size() != 32)
    return false;
  for (std::size_t i = 0; i < 16; ++i) {
    const int hi = hex_nibble(static_cast<unsigned char>(hex32[2 * i + 0]));
    const int lo = hex_nibble(static_cast<unsigned char>(hex32[2 * i + 1]));
    if (hi < 0 || lo < 0)
      return false;
    out[i] = static_cast<unsigned char>((hi << 4) | lo);
  }
  return true;
}

static std::string md5_hex32(const std::array<unsigned char, 16> &d) {
  static constexpr char hex[] = "0123456789abcdef";
  std::string out(32, '0');
  for (std::size_t i = 0; i < 16; ++i) {
    out[2 * i + 0] = hex[(d[i] >> 4) & 0x0F];
    out[2 * i + 1] = hex[d[i] & 0x0F];
  }
  return out;
}

static std::optional<Md5Job> detect_md5_job(const std::filesystem::path &p) {
  const std::uint64_t file_size = std::filesystem::file_size(p);
  if (file_size < (kMd5HexChars + 2))
    return std::nullopt;

  const std::uint64_t tail_off =
      (file_size > kTrailerMaxBytes) ? (file_size - kTrailerMaxBytes) : 0;
  const std::size_t tail_len = static_cast<std::size_t>(file_size - tail_off);

  std::ifstream in(p, std::ios::binary);
  if (!in.is_open()) {
    spdlog::warn("Cannot open file for MD5 check: {}", p.string());
    return std::nullopt;
  }

  std::string tail(tail_len, '\0');
  in.seekg(static_cast<std::streamoff>(tail_off), std::ios::beg);
  if (!in.good()) {
    spdlog::error("Seek failed for MD5 check: {}", p.string());
    return std::nullopt;
  }
  in.read(tail.data(), static_cast<std::streamsize>(tail.size()));
  if (!in.good()) {
    spdlog::error("Read failed for MD5 check: {}", p.string());
    return std::nullopt;
  }

  std::int64_t delim = -1;
  for (std::int64_t i = static_cast<std::int64_t>(tail.size()) - 2; i >= 0;
       --i) {
    if (tail[static_cast<std::size_t>(i)] != ' ' ||
        tail[static_cast<std::size_t>(i) + 1] != ' ')
      continue;
    const std::int64_t start = i - static_cast<std::int64_t>(kMd5HexChars);
    if (start < 0)
      continue;

    bool ok = true;
    for (std::size_t j = 0; j < kMd5HexChars; ++j)
      if (!is_hex(static_cast<unsigned char>(
              tail[static_cast<std::size_t>(start) + j]))) {
        ok = false;
        break;
      }
    if (ok) {
      delim = i;
      break;
    }
  }
  if (delim < 0)
    return std::nullopt;

  std::array<unsigned char, 16> expected{};
  if (!parse_md5_hex(
          {tail.data() + static_cast<std::size_t>(
                             delim - static_cast<std::int64_t>(kMd5HexChars)),
           kMd5HexChars},
          expected))
    return std::nullopt;

  const std::uint64_t bytes_to_hash =
      tail_off + static_cast<std::uint64_t>(
                     delim - static_cast<std::int64_t>(kMd5HexChars));

  if (file_size - bytes_to_hash > kTrailerMaxBytes) {
    spdlog::warn(
        "MD5 trailer detected but too far from end of file, skipping: {}",
        p.string());
    return std::nullopt;
  }
  return Md5Job{p, bytes_to_hash, expected};
}

static std::optional<std::array<unsigned char, 16>>
md5_hash_prefetch(const std::filesystem::path &path,
                  std::uint64_t bytes_to_hash, std::atomic_uint64_t &done,
                  std::uint64_t total, FlashInterface &ui) {
  constexpr std::size_t kBuf = 8 * 1024 * 1024;
  struct Slot {
    std::vector<unsigned char> buf;
    std::size_t n = 0;
  };

  std::FILE *f = std::fopen(path.string().c_str(), "rb");
  if (!f) {
    spdlog::error("Cannot open file for MD5 hashing: {}", path.string());
    return std::nullopt;
  }
  std::unique_ptr<std::FILE, int (*)(std::FILE *)> g(f, &std::fclose);

  std::uint64_t remaining = bytes_to_hash;

  brokkr::core::TwoSlotPrefetcher<Slot> pf(
      [&](Slot &s, std::stop_token st) -> bool {
        if (st.stop_requested() || !remaining)
          return false;
        const std::size_t want =
            static_cast<std::size_t>(std::min<std::uint64_t>(remaining, kBuf));
        const std::size_t got = std::fread(s.buf.data(), 1, want, f);
        if (got != want) {
          spdlog::error(
              "Read failed during MD5 hashing: {} (got {}, expected {})",
              path.string(), got, want);
          return false;
        }
        s.n = got;
        remaining -= static_cast<std::uint64_t>(got);
        return true;
      },
      [&](Slot &s) { s.buf.resize(kBuf); });

  MD5_CTX ctx{};
  md5_init(&ctx);

  std::uint64_t processed = 0;
  while (processed < bytes_to_hash) {
    auto lease = pf.next();
    if (!lease)
      break;
    auto &s = lease->get();
    if (!s.n)
      break;

    md5_update(&ctx, s.buf.data(), s.n);
    processed += static_cast<std::uint64_t>(s.n);

    const auto new_done = done.fetch_add(static_cast<std::uint64_t>(s.n),
                                         std::memory_order_relaxed) +
                          static_cast<std::uint64_t>(s.n);
    ui.progress(new_done, total, new_done, total);
  }

  if (processed != bytes_to_hash) {
    spdlog::error(
        "Read failed during MD5 hashing: {} (processed {}, expected {})",
        path.string(), processed, bytes_to_hash);
    return std::nullopt;
  }

  std::array<unsigned char, 16> out{};
  md5_final(&ctx, out.data());
  return out;
}

} // namespace

std::vector<Md5Job> md5_jobs(const std::vector<std::filesystem::path> &inputs) {
  std::vector<Md5Job> jobs;
  for (const auto &p : inputs) {
    if (!brokkr::io::TarArchive::is_tar_file(p.string())) {
      spdlog::debug("Skipping non-TAR file for MD5 check: {}", p.string());
      continue;
    }
    if (auto j = detect_md5_job(p))
      jobs.push_back(std::move(*j));
  }
  return jobs;
}

bool md5_verify(const std::vector<Md5Job> &jobs, FlashInterface &ui) {
  if (jobs.empty()) {
    spdlog::debug("No MD5 jobs");
    return true;
  }

  std::uint64_t total = std::accumulate(
      jobs.begin(), jobs.end(), static_cast<std::uint64_t>(0),
      [](std::uint64_t acc, const Md5Job &j) { return acc + j.bytes_to_hash; });

  ui.stage("Checking package checksums");
  spdlog::info("Checking MD5 checksums for {} package(s), total {} bytes",
               jobs.size(), total);

  {
    brokkr::odin::PlanItem pi;
    pi.kind = brokkr::odin::PlanItem::Kind::Part;
    pi.part_id = 0;
    pi.dev_type = 0;
    pi.part_name = "Checksums";
    pi.source_base = fmt::format("{} package(s)", jobs.size());
    pi.size = total;

    ui.plan({std::move(pi)}, total);
    ui.active(0);
    ui.progress(0, total, 0, total);
  }

  std::atomic_uint64_t done{0};
  const std::size_t threads = std::min<std::size_t>(
      jobs.size(),
      std::max<std::size_t>(1, std::thread::hardware_concurrency()));

  brokkr::core::ThreadPool pool(threads);

  for (const auto &j : jobs) {
    pool.submit([&, j] {
      auto _digest =
          md5_hash_prefetch(j.path, j.bytes_to_hash, done, total, ui);
      if (!_digest) {
        spdlog::error("Failed to calculate MD5 for file: {}", j.path.string());
        throw std::runtime_error("Failed to calculate MD5");
      }

      auto digest = std::move(*_digest);
      if (std::memcmp(digest.data(), j.expected.data(), j.expected.size()) !=
          0) {
        spdlog::error("MD5 mismatch for file: {}\n  expected:   {}\n  "
                      "calculated: {}\n  byte count: {}",
                      j.path.string(), md5_hex32(j.expected), md5_hex32(digest),
                      j.bytes_to_hash);

        throw std::runtime_error("MD5 mismatch");
      }
    });
  }

  pool.wait();
  if (auto eps = pool.take_exceptions(); !eps.empty()) {
    spdlog::error("{} error(s) occurred during MD5 verification", eps.size());
    return false;
  }
  ui.done_item(0);
  spdlog::info("All MD5 checksums verified successfully");
  return true;
}

} // namespace brokkr::app
