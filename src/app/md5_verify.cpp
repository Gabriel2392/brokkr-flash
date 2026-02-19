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

#include "io/tar.hpp"
#include "third_party/md5/md5.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace brokkr::app {

namespace {

constexpr std::size_t kTrailerMaxBytes = 16 * 1024;
constexpr std::size_t kMd5HexChars = 32;

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
  if (hex32.size() != 32) return false;
  for (std::size_t i = 0; i < 16; ++i) {
    const int hi = hex_nibble(static_cast<unsigned char>(hex32[2 * i + 0]));
    const int lo = hex_nibble(static_cast<unsigned char>(hex32[2 * i + 1]));
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<unsigned char>((hi << 4) | lo);
  }
  return true;
}

static std::string md5_hex32(const std::array<unsigned char, 16>& d) {
  static constexpr char hex[] = "0123456789abcdef";
  std::string out(32, '0');
  for (std::size_t i = 0; i < 16; ++i) {
    out[2 * i + 0] = hex[(d[i] >> 4) & 0x0F];
    out[2 * i + 1] = hex[d[i] & 0x0F];
  }
  return out;
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

static brokkr::core::Result<std::array<unsigned char, 16>> md5_hash_prefetch(const std::filesystem::path& path,
                                                                             std::uint64_t bytes_to_hash,
                                                                             std::atomic_uint64_t& done,
                                                                             std::uint64_t total,
                                                                             const brokkr::odin::Ui& ui) noexcept {
  constexpr std::size_t kBuf = 8 * 1024 * 1024;
  struct Slot {
    std::vector<unsigned char> buf;
    std::size_t n = 0;
  };

  std::FILE* f = std::fopen(path.string().c_str(), "rb");
  if (!f) return brokkr::core::failf("Cannot open for MD5: {}", path.string());
  std::unique_ptr<std::FILE, int (*)(std::FILE*)> g(f, &std::fclose);

  std::uint64_t remaining = bytes_to_hash;

  brokkr::core::TwoSlotPrefetcher<Slot> pf(
      [&](Slot& s, std::stop_token st) -> brokkr::core::Result<bool> {
        if (st.stop_requested() || !remaining) return false;

        const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, kBuf));
        const std::size_t got = std::fread(s.buf.data(), 1, want, f);
        if (got != want) return brokkr::core::failf("Short read while hashing: {}", path.string());

        s.n = got;
        remaining -= static_cast<std::uint64_t>(got);
        return true;
      },
      [&](Slot& s) { s.buf.resize(kBuf); });

  MD5_CTX ctx{};
  md5_init(&ctx);

  std::uint64_t processed = 0;
  while (processed < bytes_to_hash) {
    auto lease = pf.next();
    if (!lease) break;

    auto& s = lease->get();
    if (!s.n) break;

    md5_update(&ctx, s.buf.data(), s.n);
    processed += static_cast<std::uint64_t>(s.n);

    const auto new_done = done.fetch_add(static_cast<std::uint64_t>(s.n), std::memory_order_relaxed) +
                          static_cast<std::uint64_t>(s.n);

    if (ui.on_progress) ui.on_progress(new_done, total, new_done, total);
  }

  auto pst = pf.status();
  if (!pst) return brokkr::core::fail(std::move(pst.error()));

  if (processed != bytes_to_hash) {
    return brokkr::core::failf("MD5 hashing terminated early: {} (processed {}, expected {})", path.string(), processed,
                               bytes_to_hash);
  }

  std::array<unsigned char, 16> out{};
  md5_final(&ctx, out.data());
  return out;
}

} // namespace

brokkr::core::Result<std::vector<Md5Job>> md5_jobs(const std::vector<std::filesystem::path>& inputs) noexcept {
  std::vector<Md5Job> jobs;

  for (const auto& p : inputs) {
    if (!brokkr::io::TarArchive::is_tar_file(p.string())) continue;

    auto r = detect_md5_job(p);
    if (!r) return brokkr::core::fail(std::move(r.error()));
    if (*r) jobs.push_back(std::move(**r));
  }

  return jobs;
}

brokkr::core::Status md5_verify(const std::vector<Md5Job>& jobs, const brokkr::odin::Ui& ui) noexcept {
  if (jobs.empty()) return {};

  std::uint64_t total = 0;
  for (const auto& j : jobs) total += j.bytes_to_hash;

  if (ui.on_stage) ui.on_stage("Checking package checksums");
  spdlog::info("Checking MD5 on {} package(s), {} bytes total", jobs.size(), total);

  if (ui.on_plan) {
    brokkr::odin::PlanItem pi;
    pi.kind = brokkr::odin::PlanItem::Kind::Part;
    pi.part_id = 0;
    pi.dev_type = 0;
    pi.part_name = "Checksums";
    pi.source_base = fmt::format("{} package(s)", jobs.size());
    pi.size = total;
    ui.on_plan({std::move(pi)}, total);
  }
  if (ui.on_item_active) ui.on_item_active(0);
  if (ui.on_progress) ui.on_progress(0, total, 0, total);

  const std::size_t threads = std::min<std::size_t>(jobs.size(),
                                                    std::max<std::size_t>(1, std::thread::hardware_concurrency()));

  brokkr::core::ThreadPool pool(threads);
  std::atomic_uint64_t done{0};

  for (const auto& j : jobs) {
    auto st = pool.submit([&, j]() -> brokkr::core::Status {
      if (pool.cancelled()) return {};

      auto r = md5_hash_prefetch(j.path, j.bytes_to_hash, done, total, ui);
      if (!r) return brokkr::core::fail(std::move(r.error()));

      const auto& digest = *r;
      if (std::memcmp(digest.data(), j.expected.data(), j.expected.size()) != 0) {
        return brokkr::core::fail("MD5 mismatch: " + j.path.string() + "\n  expected:   " + md5_hex32(j.expected) +
                                  "\n  calculated: " + md5_hex32(digest) +
                                  "\n  byte count: " + std::to_string(j.bytes_to_hash));
      }
      return {};
    });

    if (!st) return st;
  }

  auto wst = pool.wait();
  if (!wst) return wst;

  if (ui.on_item_done) ui.on_item_done(0);
  spdlog::info("MD5 OK");
  return {};
}

} // namespace brokkr::app
